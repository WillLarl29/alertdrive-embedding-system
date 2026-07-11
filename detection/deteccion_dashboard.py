"""
deteccion_dashboard.py

Captura la webcam local, corre el modelo de deteccion de ojos (MediaPipe +
CNN) y:
  1. Dispara el buzzer del ESP32 por HTTP directo (/alarma, /ok), igual que
     el notebook deteccion_esp32_http.ipynb.
  2. Empuja el estado de somnolencia al backend (POST /api/drowsiness) para
     que se vea en vivo en el dashboard web.
  3. Empuja cada frame anotado al backend (POST /api/frame) para que el
     panel "Camara en vivo" del dashboard lo muestre como stream MJPEG.

Correr directo en Windows (mismo entorno que el notebook, ej. conda
"capstone"), NO dentro de Docker: el contenedor del backend no tiene forma
de acceder a la webcam de la laptop.
"""

import os
import time
import cv2
import torch
import torch.nn as nn
import mediapipe as mp
import requests
from torchvision import transforms

ESP32_IP    = os.environ.get("ESP32_IP", "10.166.177.207")
BACKEND_URL = os.environ.get("BACKEND_URL", "http://localhost:3000")

ALARMA_URL = f"http://{ESP32_IP}/alarma"
OK_URL     = f"http://{ESP32_IP}/ok"
DROWSY_URL = f"{BACKEND_URL}/api/drowsiness"
FRAME_URL  = f"{BACKEND_URL}/api/frame"

UMBRAL_SEGUNDOS = 3.0   # tiempo continuo de ojos cerrados para alarma
UMBRAL_ABIERTO  = 0.50  # <0.5 -> cerrados; >=0.5 -> abiertos
IMG_SIZE        = 64
MODEL_PATH      = os.path.join(os.path.dirname(os.path.abspath(__file__)), "model", "eye_cnn_best.pth")
FRAME_INTERVAL  = 0.1   # ~10 fps hacia el dashboard, no satura el backend

DEVICE = torch.device("cuda" if torch.cuda.is_available() else "cpu")
print(f"Usando: {DEVICE}")


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


model = EyeCNN().to(DEVICE)
model.load_state_dict(torch.load(MODEL_PATH, map_location=DEVICE))
model.eval()
print(f"Modelo cargado desde {MODEL_PATH}")

CLASSES = ["Cerrado", "Abierto"]

LEFT_EYE  = [33, 160, 158, 133, 153, 144]
RIGHT_EYE = [362, 385, 387, 263, 373, 380]

mp_face_mesh = mp.solutions.face_mesh
face_mesh = mp_face_mesh.FaceMesh(
    max_num_faces=1, refine_landmarks=True,
    min_detection_confidence=0.5, min_tracking_confidence=0.5,
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


def enviar_alarma():
    try:
        requests.get(ALARMA_URL, timeout=1)
    except Exception as e:
        print(f"Error enviando /alarma: {e}")


def enviar_ok():
    try:
        requests.get(OK_URL, timeout=1)
    except Exception as e:
        print(f"Error enviando /ok: {e}")


def enviar_estado_backend(status, confidence, eye_close_seconds):
    try:
        requests.post(DROWSY_URL, json={
            "status": status,
            "confidence": confidence,
            "eye_close_seconds": eye_close_seconds,
        }, timeout=1)
    except Exception as e:
        print(f"Error mandando estado al backend: {e}")


def enviar_frame(frame_bgr):
    ok, buf = cv2.imencode(".jpg", frame_bgr, [cv2.IMWRITE_JPEG_QUALITY, 70])
    if not ok:
        return
    try:
        requests.post(FRAME_URL, data=buf.tobytes(),
                      headers={"Content-Type": "image/jpeg"}, timeout=1)
    except Exception as e:
        print(f"Error mandando frame al backend: {e}")


def main():
    print(f"ESP32: {ESP32_IP}  |  Backend: {BACKEND_URL}")
    cap = cv2.VideoCapture(0, cv2.CAP_DSHOW)
    if not cap.isOpened():
        print("ERROR: no se pudo abrir la webcam.")
        return

    print("Camara abierta. Presiona Q en la ventana para salir.")
    closed_since = None
    alarma_activa = False
    ultimo_envio_frame = 0.0

    try:
        while True:
            ok, frame = cap.read()
            if not ok:
                print("No se pudo leer frame")
                break

            h, w = frame.shape[:2]
            results = face_mesh.process(cv2.cvtColor(frame, cv2.COLOR_BGR2RGB))
            estado = "SIN ROSTRO"
            color = (200, 200, 200)
            eye_close_seconds = 0.0

            if results.multi_face_landmarks:
                lms = results.multi_face_landmarks[0].landmark
                probs_abierto = []

                for eye_idx in (LEFT_EYE, RIGHT_EYE):
                    crop, box = crop_eye(frame, lms, eye_idx, w, h)
                    if crop is None:
                        continue

                    x = infer_tf(cv2.cvtColor(crop, cv2.COLOR_BGR2RGB)).unsqueeze(0).to(DEVICE)
                    with torch.no_grad():
                        prob = torch.softmax(model(x), dim=1)[0]
                    p_abierto = prob[1].item()
                    probs_abierto.append(p_abierto)

                    pred_visual = 1 if p_abierto >= UMBRAL_ABIERTO else 0
                    x1, y1, x2, y2 = box
                    c = (0, 0, 255) if pred_visual == 0 else (0, 255, 0)
                    cv2.rectangle(frame, (x1, y1), (x2, y2), c, 2)
                    cv2.putText(frame, f"{CLASSES[pred_visual]} {p_abierto:.2f}",
                                (x1, y1 - 8), cv2.FONT_HERSHEY_SIMPLEX, 0.5, c, 1)

                if probs_abierto:
                    p_promedio = sum(probs_abierto) / len(probs_abierto)
                    ojos_cerrados = p_promedio < UMBRAL_ABIERTO
                    confianza = (1 - p_promedio) if ojos_cerrados else p_promedio

                    if ojos_cerrados:
                        if closed_since is None:
                            closed_since = time.time()
                        eye_close_seconds = time.time() - closed_since
                        estado = f"OJOS CERRADOS {eye_close_seconds:.1f}s (avg {p_promedio:.2f})"
                        color = (0, 0, 255)
                        if eye_close_seconds >= UMBRAL_SEGUNDOS and not alarma_activa:
                            alarma_activa = True
                            enviar_alarma()
                            print(">>> ALARMA ENVIADA AL ESP32")
                    else:
                        closed_since = None
                        if alarma_activa:
                            alarma_activa = False
                            enviar_ok()
                            print(">>> OK enviado (alarma apagada)")
                        estado = f"OJOS ABIERTOS (avg {p_promedio:.2f})"
                        color = (0, 255, 0)

                    enviar_estado_backend("cerrado" if ojos_cerrados else "abierto",
                                          confianza, eye_close_seconds)

            cv2.putText(frame, estado, (10, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.8, color, 2)
            cv2.imshow("Deteccion Somnolencia (dashboard) - Q para salir", frame)

            ahora = time.time()
            if ahora - ultimo_envio_frame >= FRAME_INTERVAL:
                enviar_frame(frame)
                ultimo_envio_frame = ahora

            if cv2.waitKey(1) & 0xFF == ord('q'):
                break
    except KeyboardInterrupt:
        print("Interrumpido por el usuario")
    finally:
        cap.release()
        cv2.destroyAllWindows()
        face_mesh.close()
        enviar_ok()
        print("Finalizado")


if __name__ == "__main__":
    main()
