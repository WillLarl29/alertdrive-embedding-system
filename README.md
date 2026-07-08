# AlertDrive — Prototipo IoT de Detección de Somnolencia y Alcoholemia

MVP del curso Desarrollo de Soluciones de IoT (ESAN 2026-1). Dispositivo portátil
basado en ESP32-CAM que detecta somnolencia, nivel de alcoholemia y calidad de
aire en cabina, con una página web propia de monitoreo en tiempo real (reemplaza
a Blynk como plataforma de monitoreo remoto).

## Arquitectura

```
ESP32-CAM (firmware/esp32-cam)
  :81  stream MJPEG de la camara
  :82  /alarma /ok /estado (buzzer)  /status (JSON sensores)  /test-mq3
          ▲                                    ▲
          │ poll HTTP /status cada 1.5s         │ push POST /api/drowsiness
          │                                     │
   backend/ (Node + Express + Socket.IO)  ◄──── detection/deteccion_esp32cam.py
          │                                     (PyTorch + MediaPipe, corre en la laptop)
          │ WebSocket (estado en vivo) + REST (histórico de alertas)
          ▼
   frontend/ (dashboard HTML/CSS/JS, servido por el backend en http://localhost:3000)
```

## Estructura del repositorio

- `firmware/esp32-cam/` — firmware del ESP32-CAM (cámara, buzzer, LEDs, botones, ADS1115, fan/TIP142).
- `detection/` — script de detección de microsueño (PyTorch + MediaPipe) y los pesos del modelo (`model/eye_cnn_best.pth`).
- `backend/` — servidor Node/Express + Socket.IO: agrega el estado del ESP32 y de la detección de somnolencia, guarda el historial de alertas, y sirve el dashboard.
- `frontend/` — dashboard web (HTML/CSS/JS plano, sin framework).
- `docs/` — documentación de conexiones/hardware del prototipo.
- `deteccion_somnolencia.ipynb` — notebook de **entrenamiento** del modelo (no se usa en runtime, solo generó `eye_cnn_best.pth`).

## Cómo correr cada pieza

### 1. Firmware (`firmware/esp32-cam/AlertDrive_ESP32CAM.ino`)

Librerías necesarias en Arduino IDE (Library Manager): **Adafruit ADS1X15** (y su
dependencia Adafruit BusIO). Selecciona la placa "AI Thinker ESP32-CAM", ajusta
`ssid`/`password` en el sketch, y flashea. Al arrancar, el Serial Monitor imprime
la IP asignada — esa es la que va en `ESP32_IP` del backend y del script de detección.

### 2. Backend (`backend/`)

```
cd backend
npm install          # o: pnpm install
cp .env.example .env # y edita ESP32_IP con la IP real del ESP32-CAM
npm start
```

Abre `http://localhost:3000` para ver el dashboard.

Variables de entorno (`.env`):

| Variable | Default | Descripción |
|---|---|---|
| `ESP32_IP` | `192.168.1.50` | IP del ESP32-CAM en la red WiFi |
| `ESP32_STREAM_PORT` | `81` | Puerto del stream MJPEG |
| `ESP32_CONTROL_PORT` | `82` | Puerto de `/status`, `/alarma`, `/ok`, `/test-mq3` |
| `POLL_INTERVAL_MS` | `1500` | Cada cuánto se consulta `/status` |
| `PORT` | `3000` | Puerto del propio backend/dashboard |

### 3. Detección de microsueño (`detection/`)

> **Importante:** `mediapipe` eliminó su API legacy `solutions` (la que usa este
> script) en versiones `>=0.10.30`, y esa API **nunca** se compiló para Python
> `>=3.13`. Por eso el script necesita un entorno con Python 3.9–3.12 y
> `mediapipe<=0.10.21` (ya fijado en `requirements.txt`) — no lo instales en un
> entorno con Python 3.13 (p. ej. un Anaconda base reciente) o los imports de
> `mediapipe`/`cv2`/`torch` no se resolverán y `mp.solutions.face_mesh` no existirá.

```
cd detection
python -m venv .venv          # usa un Python 3.9-3.12 (ej. python.exe de Microsoft Store)
.venv\Scripts\activate         # PowerShell: .venv\Scripts\Activate.ps1
pip install -r requirements.txt
python deteccion_esp32cam.py
```

En VS Code, selecciona `detection/.venv/Scripts/python.exe` como intérprete
(Ctrl+Shift+P → "Python: Select Interpreter") para que los imports se resuelvan
en el editor.

Antes de correrlo, ajusta `ESP32_IP` dentro del script (misma IP que en el backend).
Si el backend (`backend/`) está corriendo en `localhost:3000`, cada cambio de
estado (ojos cerrados/abiertos) se refleja automáticamente en el dashboard vía
`POST /api/drowsiness`.

## Docker (backend + frontend + detección juntos)

El firmware **no** se dockeriza (se flashea al hardware, ver arriba). Los otros
tres componentes sí, con un solo comando:

```
cp .env.example .env   # opcional, para fijar ESP32_IP (default 192.168.1.50)
docker compose up --build
```

Esto levanta:
- **`frontend`** — dashboard en `http://localhost:8080` (nginx sirviendo los
  estáticos de `frontend/`), que le hace de proxy a `backend` para `/api/*`,
  `/video` (con `proxy_buffering off`, si no el video queda "congelado") y
  `/socket.io/*` (con upgrade a WebSocket). Es la puerta de entrada a usar.
- **`backend`** — API/WebSocket en `http://localhost:3000` (también accesible
  directo, útil para depurar), con el historial de alertas persistido en un
  volumen (`alertdrive_alerts`) para que sobreviva a `docker compose down`.
- **`detection`** — el script de somnolencia corriendo en modo `HEADLESS=1`
  (sin la ventana de depuración `cv2.imshow`, ya que el contenedor no tiene
  pantalla); el video en vivo se sigue viendo igual en el dashboard. Se
  reconecta solo si pierde el stream del ESP32-CAM. Le habla al backend por
  el nombre del servicio (`http://backend:3000/...`), no por `localhost`.

`ESP32_IP` se define una sola vez (variable de entorno `ESP32_IP` en un `.env`
en la raíz, o `ESP32_IP=... docker compose up`) y se propaga a `backend` y
`detection`.

Para correr solo uno: `docker compose up backend`, `docker compose up frontend`
o `docker compose up detection` (aunque `frontend` y `detection` dependen de
que `backend` esté arriba).

> Nota: fuera de Docker (`npm start` en `backend/`), el backend sigue sirviendo
> el dashboard directamente en `http://localhost:3000` como antes — el
> contenedor `frontend`/nginx es exclusivo del flujo con Docker Compose.

## Verificación end-to-end

1. Firmware: `curl http://<ESP32_IP>:82/status` debe devolver JSON válido; los
   LEDs y el fan deben reaccionar a los botones físicos.
2. Backend: al iniciar sin el ESP32 disponible, debe loguear e indicar
   `esp32_online:false` sin caerse (degradación esperada).
3. Dashboard: con el ESP32 encendido, el video debe cargar en `http://localhost:3000`,
   los meters de alcohol/aire deben moverse con los sensores, y el botón
   "Probar alcoholemia" debe disparar la ventana de prueba remota.
4. Detección: al cerrar los ojos ~3s frente a la cámara, se debe activar el buzzer
   físico y el dashboard debe mostrar "Somnolencia: Detectado" + una fila nueva en
   el historial de alertas.

## Nota sobre `docs/`

El PDF `Descripcion_Conexiones_AlertDrive.pdf` (mapa de pines y diagramas de
conexión) se referenció como contexto de este proyecto pero no existía como
archivo en el repositorio; si lo tienes disponible, colócalo en `docs/` para
mantener la documentación completa junto al código.
