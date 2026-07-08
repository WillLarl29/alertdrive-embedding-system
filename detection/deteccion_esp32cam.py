"""
deteccion_esp32cam.py

Captura el stream del ESP32-CAM, corre el modelo de deteccion de ojos
con MediaPipe + CNN, y cuando detecta 3 segundos de ojos cerrados,
manda una señal HTTP al ESP32 para activar el buzzer y notifica al
backend de monitoreo (AlertDrive dashboard) para reflejarlo en la web.

Uso:
    Ejecutar dentro del entorno 'capstone' donde tienes PyTorch + MediaPipe.
    Asegurate que el ESP32-CAM este corriendo el firmware AlertDrive_ESP32CAM.ino
    y conectado al mismo WiFi, y que el backend (backend/) este corriendo
    si quieres ver el estado reflejado en el dashboard.
"""

import os
import time
import cv2
import numpy as np
import torch
import torch.nn as nn
import mediapipe as mp
import requests
import urllib.request
from torchvision import transforms

# =========================================================
# CONFIGURACION - se puede sobreescribir con variables de entorno (Docker)
# =========================================================
ESP32_IP        = os.environ.get("ESP32_IP", "172.20.10.3")
STREAM_URL      = f"http://{ESP32_IP}:81/"
ALARMA_URL      = f"http://{ESP32_IP}:82/alarma"
OK_URL          = f"http://{ESP32_IP}:82/ok"
BACKEND_URL     = os.environ.get("BACKEND_URL", "http://localhost:3000/api/drowsiness")

# En Docker no hay pantalla para la ventana de depuracion (cv2.imshow); el video
# en vivo se sigue viendo igual en el dashboard web (http://localhost:3000).
HEADLESS        = os.environ.get("HEADLESS", "0") == "1"

UMBRAL_SEGUNDOS = 3.0   # tiempo continuo de ojos cerrados para alarma
UMBRAL_ABIERTO  = 0.75  # confianza minima para declarar "Abierto"
IMG_SIZE        = 64    # mismo que usaste en entrenamiento
MODEL_PATH      = os.path.join(os.path.dirname(os.path.abspath(__file__)), "model", "eye_cnn_best.pth")

DEVICE = torch.device("cuda" if torch.cuda.is_available() else "cpu")
print(f"Usando: {DEVICE}")

# =========================================================
# MODELO - misma arquitectura que entrenaste en PyTorch
# =========================================================
class EyeCNN(nn.Module):
    def __init__(self):
        super().__init__()
        self.features = nn.Sequential(
            nn.Conv2d(1, 32, 3, padding=1), nn.BatchNorm2d(32), nn.ReLU(), nn.MaxPool2d(2),
            nn.Conv2d(32, 64, 3, padding=1), nn.BatchNorm2d(64), nn.ReLU(), nn.MaxPool2d(2),
            nn.Conv2d(64, 128, 3, padding=1), nn.BatchNorm2d(128), nn.ReLU(), nn.MaxPool2d(2),
        )
        self.classifier = nn.Sequential(
            nn.Flatten(),
            nn.Linear(128 * 8 * 8, 256), nn.ReLU(), nn.Dropout(0.5),
            nn.Linear(256, 2)
        )
    def forward(self, x):
        return self.classifier(self.features(x))

# Cargar pesos
model = EyeCNN().to(DEVICE)
model.load_state_dict(torch.load(MODEL_PATH, map_location=DEVICE))
model.eval()
print("Modelo PyTorch cargado")

CLASSES = ["Cerrado", "Abierto"]

# =========================================================
# MEDIAPIPE - deteccion de landmarks de la cara
# =========================================================
LEFT_EYE  = [33, 160, 158, 133, 153, 144]
RIGHT_EYE = [362, 385, 387, 263, 373, 380]

mp_face_mesh = mp.solutions.face_mesh
face_mesh = mp_face_mesh.FaceMesh(
    max_num_faces=1, refine_landmarks=True,
    min_detection_confidence=0.5, min_tracking_confidence=0.5
)

infer_tf = transforms.Compose([
    transforms.ToPILImage(),
    transforms.Grayscale(num_output_channels=1),
    transforms.Resize((IMG_SIZE, IMG_SIZE)),
    transforms.ToTensor(),
    transforms.Normalize(mean=[0.5], std=[0.5]),
])

def crop_eye(frame, landmarks, eye_idx, w, h, margin=0.6):
    xs = [landmarks[i].x * w for i in eye_idx]
    ys = [landmarks[i].y * h for i in eye_idx]
    x1, x2 = min(xs), max(xs)
    y1, y2 = min(ys), max(ys)
    mx = (x2 - x1) * margin
    my = (y2 - y1) * margin * 1.5
    x1, x2 = int(max(0, x1 - mx)), int(min(w, x2 + mx))
    y1, y2 = int(max(0, y1 - my)), int(min(h, y2 + my))
    if x2 - x1 < 10 or y2 - y1 < 10:
        return None, None
    return frame[y1:y2, x1:x2], (x1, y1, x2, y2)

@torch.no_grad()
def predict_eye(crop_bgr):
    x = infer_tf(cv2.cvtColor(crop_bgr, cv2.COLOR_BGR2RGB)).unsqueeze(0).to(DEVICE)
    out = model(x)
    prob = torch.softmax(out, dim=1)[0]
    p_abierto = prob[1].item()
    pred = 1 if p_abierto >= UMBRAL_ABIERTO else 0
    conf = p_abierto if pred == 1 else (1 - p_abierto)
    return pred, conf

# =========================================================
# HTTP - señales al ESP32 y al backend de monitoreo
# =========================================================
def enviar_estado_backend(status, confidence):
    try:
        requests.post(BACKEND_URL, json={"status": status, "confidence": confidence}, timeout=1)
    except Exception as e:
        print(f"Error mandando estado al backend: {e}")

def enviar_alarma(confidence=None):
    try:
        requests.get(ALARMA_URL, timeout=1)
    except Exception as e:
        print(f"Error mandando /alarma: {e}")
    enviar_estado_backend("cerrado", confidence)

def enviar_ok(confidence=None):
    try:
        requests.get(OK_URL, timeout=1)
    except Exception as e:
        print(f"Error mandando /ok: {e}")
    enviar_estado_backend("abierto", confidence)

# =========================================================
# STREAM MJPEG del ESP32-CAM
# =========================================================
def leer_stream_mjpeg(url):
    """
    Generador que recibe frames JPEG del stream MJPEG del ESP32-CAM.
    Cada frame es un np.array BGR (matriz de pixeles) listo para usar.
    """
    stream = urllib.request.urlopen(url, timeout=5)
    bytes_buffer = b""
    while True:
        chunk = stream.read(1024)
        if not chunk:
            break
        bytes_buffer += chunk
        # Buscar delimitadores JPEG: 0xFFD8 (inicio) y 0xFFD9 (fin)
        a = bytes_buffer.find(b'\xff\xd8')
        b = bytes_buffer.find(b'\xff\xd9')
        if a != -1 and b != -1 and b > a:
            jpg = bytes_buffer[a:b + 2]
            bytes_buffer = bytes_buffer[b + 2:]
            # Decodificar JPEG -> matriz numpy
            frame = cv2.imdecode(np.frombuffer(jpg, dtype=np.uint8),
                                 cv2.IMREAD_COLOR)
            if frame is not None:
                yield frame

# =========================================================
# BUCLE PRINCIPAL
# =========================================================
def procesar_frames(frames):
    """Consume el generador de frames hasta que se agote o se pida salir (Q)."""
    closed_since = None
    alarma_activa = False

    for frame in frames:
        h, w = frame.shape[:2]
        results = face_mesh.process(cv2.cvtColor(frame, cv2.COLOR_BGR2RGB))

        estado = "SIN ROSTRO"
        color = (200, 200, 200)

        if results.multi_face_landmarks:
            lms = results.multi_face_landmarks[0].landmark
            preds = []
            confs = []
            for eye_idx in (LEFT_EYE, RIGHT_EYE):
                crop, box = crop_eye(frame, lms, eye_idx, w, h)
                if crop is None:
                    continue
                pred, conf = predict_eye(crop)
                preds.append(pred)
                confs.append(conf)
                x1, y1, x2, y2 = box
                c = (0, 0, 255) if pred == 0 else (0, 255, 0)
                cv2.rectangle(frame, (x1, y1), (x2, y2), c, 2)
                cv2.putText(frame, f"{CLASSES[pred]} {conf:.2f}",
                            (x1, y1 - 8),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.5, c, 1)

            avg_conf = sum(confs) / len(confs) if confs else None

            if preds and all(p == 0 for p in preds):
                if closed_since is None:
                    closed_since = time.time()
                elapsed = time.time() - closed_since
                estado = f"OJOS CERRADOS {elapsed:.1f}s"
                color = (0, 0, 255)
                if elapsed >= UMBRAL_SEGUNDOS and not alarma_activa:
                    alarma_activa = True
                    enviar_alarma(avg_conf)
                    print(">>> ALARMA ENVIADA AL ESP32")
            else:
                closed_since = None
                if alarma_activa:
                    alarma_activa = False
                    enviar_ok(avg_conf)
                    print(">>> OK enviado (alarma apagada)")
                if preds:
                    estado = "OJOS ABIERTOS"
                    color = (0, 255, 0)

        if not HEADLESS:
            cv2.putText(frame, estado, (10, 30),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.8, color, 2)
            cv2.imshow("ESP32-CAM Deteccion - Q para salir", frame)
            if cv2.waitKey(1) & 0xFF == ord('q'):
                break


def main():
    print(f"Conectando al stream: {STREAM_URL}")
    print(f"Modo: {'headless (sin ventana, para Docker)' if HEADLESS else 'con ventana de depuracion'}")

    try:
        while True:
            try:
                procesar_frames(leer_stream_mjpeg(STREAM_URL))
                break  # el stream termino normalmente (sin excepcion) -> salir
            except Exception as e:
                print(f"Stream interrumpido ({e}); reintentando en 5s...")
                time.sleep(5)
    except KeyboardInterrupt:
        print("Interrumpido por el usuario")
    finally:
        if not HEADLESS:
            cv2.destroyAllWindows()
        face_mesh.close()
        enviar_ok()   # asegurar que el buzzer quede apagado al salir
        print("Finalizado")

if __name__ == "__main__":
    main()
