# AlertDrive — Descripción completa del proyecto

> Documento elaborado a partir del código y del historial de git del repositorio
> (`main`, último commit `46ae535`, 13/07/2026). Cada dato indica de qué archivo
> o commit sale. Donde el repositorio no puede confirmar algo (p. ej. si una
> prueba física se hizo), se dice explícitamente.

---

## 1. Resumen

AlertDrive es un prototipo IoT de seguridad vial (curso Desarrollo de Soluciones
de IoT, ESAN 2026-1) que vigila tres riesgos del conductor:

1. **Somnolencia / microsueño**: visión por computadora (MediaPipe Face Mesh +
   CNN en PyTorch) que clasifica los ojos como abiertos o cerrados. Si están
   cerrados **3 s seguidos**, se activa la alarma.
2. **Alcoholemia**: sensor **MQ-3** leído mediante un ADC **ADS1115**, dentro de
   una ventana de prueba de **7 s** con ventilador de succión.
3. **Calidad del aire en la cabina**: sensor **MQ-135** monitoreado de forma
   continua cada **500 ms**.

Las alertas se emiten localmente (buzzer con un patrón distinto por tipo de
alerta, LEDs) y se muestran en un **dashboard web propio** en tiempo real
(reemplaza a Blynk).

---

## 2. Arquitectura actual (la que funciona hoy)

```
 Webcam de la laptop
        │ cv2.VideoCapture(0)
        ▼
 detection/deteccion_dashboard.py  (PC: PyTorch .pth + MediaPipe)
        │                     │                         │
        │ GET /alarma, /ok    │ POST /api/drowsiness     │ POST /api/frame (JPEG ~10 fps)
        ▼                     ▼                         ▼
 ESP32 DevKit (puerto 80) ◄── backend/ (Node + Express + Socket.IO, :3000)
   firmware funcional_       │   poll GET /status cada 1,5 s
   prototipo_ultimo.ino      │   historial de alertas (JSON en volumen)
                             ▼
                      frontend/ (HTML/CSS/JS, nginx en :8080 con Docker)
                             │ WebSocket + REST + /video (MJPEG)
                             ▼
                         Navegador
```

Puntos clave:

- **La cámara ya no es el ESP32-CAM.** En el commit `02b2cd5` el firmware se
  separó en ESP32-CAM (solo cámara) + ESP32 (sensores y actuadores), y en
  `0e6ce6a` se eliminó el firmware del ESP32-CAM ("limpiar firmware ESP32-CAM
  obsoleto"). Hoy el script de detección usa la **webcam local de la PC**
  (`cv2.VideoCapture(0, cv2.CAP_DSHOW)`, [detection/deteccion_dashboard.py](../detection/deteccion_dashboard.py)).
  El video llega al dashboard porque el script envía cada frame anotado al
  backend (`POST /api/frame`), que lo sirve como MJPEG en `/video`.
- **La detección no corre en Docker**: necesita la webcam de Windows
  ([docker-compose.yml](../docker-compose.yml) solo levanta `backend` y `frontend`).
- El ESP32 expone **un solo servidor HTTP en el puerto 80** (`WebServer.h`),
  no los puertos 81/82 que aún menciona el README.

---

## 3. Respuestas puntuales

### 3.1 ¿En la PC corre el modelo PyTorch o el TFLite INT8?

**Corre el modelo PyTorch.** El script en tiempo real
([detection/deteccion_dashboard.py](../detection/deteccion_dashboard.py)) importa
`torch`, define la clase `EyeCNN` y carga los pesos con
`torch.load("model/eye_cnn_best.pth")`; la inferencia es
`torch.softmax(model(x), dim=1)` sobre CPU (o CUDA si la hay). No hay ninguna
dependencia de TensorFlow/TFLite en [detection/requirements.txt](../detection/requirements.txt)
(`torch`, `torchvision`, `opencv-python`, `mediapipe<=0.10.21`, `requests`, `numpy`).

### 3.2 ¿El INT8 llegó a probarse en algún dispositivo?

**No. El modelo INT8 nunca llegó a generarse**, así que tampoco se probó en
ningún dispositivo (ni en PC ni en ESP32). Evidencia en el notebook de
entrenamiento `deteccion_somnolencia.ipynb` (presente en el commit inicial
`7d598df`, eliminado después):

| Paso de la ruta PyTorch → ONNX → TF → TFLite INT8 → `model.h` | Resultado registrado en el notebook |
|---|---|
| PyTorch → ONNX (`eye_model.onnx`, opset 13) | ✅ Exportado, "ONNX válido ✓" |
| ONNX → TensorFlow SavedModel (`onnx-tf`) | ❌ Falló: `ImportError: cannot import name 'mapping' from 'onnx'` |
| Dataset representativo para cuantización INT8 | ⛔ Celda nunca ejecutada |
| TF → TFLite INT8 (`eye_model_int8.tflite`) | ⛔ Celda nunca ejecutada |
| Sanity check con `tf.lite.Interpreter` | ⛔ Celda nunca ejecutada |
| TFLite → `model.h` (array C para el ESP32) | ⛔ Celda nunca ejecutada |

Además, en todo el historial de git no existe ningún archivo `.tflite` ni
`model.h`. El propio notebook advertía que la `EyeCNN` tiene **2 191 042
parámetros** (la capa `Linear(8192, 256)` concentra casi todos), lo que la hace
demasiado grande para TFLite Micro en un ESP32 sin rediseñar la red.

**Redacción sugerida para el punto central:** *"La inferencia se ejecuta en la
PC con el modelo PyTorch original (FP32). Se planteó una ruta de conversión a
TFLite INT8 para ejecutarlo en el ESP32 (TinyML); se logró la exportación a
ONNX, pero la conversión ONNX → TensorFlow falló por incompatibilidad de
versiones de `onnx-tf`, por lo que el modelo cuantizado no se generó ni se
probó en hardware. Queda como trabajo futuro, junto con reducir el tamaño de la
red (~2,19 M parámetros)."*

### 3.3 ¿El GPIO16 (y los pines 3, 4 y 12) se probó en la placa física?

**Ese pinout no es el que está en uso**, y el repositorio indica que fue
reemplazado justamente por no corresponder al hardware real:

- GPIO16 (botón encendido), GPIO3 (base TIP142/fan), GPIO4 (botón MQ-3) y
  GPIO12 (LED rojo) pertenecen al diseño original sobre **ESP32-CAM**
  (commit inicial `7d598df`) y a su copia en `02b2cd5`.
- El commit `fb4799d` se titula **"Update ESP32 firmware (WebServer + real
  pinout)"** y cambia todos esos pines por los actuales (tabla 4.2).
- El código cuenta con comentarios que reconocen que el mapeo original
  "difiere del PDF original" (canales A0/A1 invertidos).

Lo que **no se puede afirmar desde el código** es si alguien llegó a cablear y
probar físicamente el pinout 16/3/4/12 antes de abandonarlo. Lo verificable es
que el firmware que hoy funciona con el dashboard usa los pines 18, 19, 32, 25,
33, 23, 21 y 22. Hay además razones técnicas para descartarlo en un ESP32-CAM:
GPIO16 lo usa la PSRAM, GPIO3 es el RX del UART0 (Serial), GPIO4 está cableado
al LED flash y GPIO12 es un pin de *strapping* (si está en alto al arrancar,
cambia el voltaje de la flash y la placa no bootea).

**Redacción sugerida:** *"El pinout inicial (GPIO 2, 3, 4, 12–16) correspondía
al ESP32-CAM y se descartó al separar cámara y sensores; el pinout validado en
la placa es el del ESP32 DevKit indicado en la tabla de conexiones."* (Si tú
recuerdas haberlo probado físicamente, ajusta esa frase; el repo no lo registra.)

### 3.4 Valores correctos del firmware

Los dos sketches actuales coinciden en estos valores
([funcional_prototipo_ultimo.ino](../firmware/esp32/funcional_prototipo_ultimo/funcional_prototipo_ultimo.ino) y
[AlertDrive_ESP32.ino](../firmware/esp32/AlertDrive_ESP32/AlertDrive_ESP32.ino)):

| Parámetro | Valor correcto (firmware actual) | Valor obsoleto (docs / firmware ESP32-CAM) |
|---|---|---|
| Canal ADS1115 del **MQ-135** (aire) | **A0** (`readADC_SingleEnded(0)`) | A1 en `docs/ARQUITECTURA_Y_FLUJOS.md` |
| Canal ADS1115 del **MQ-3** (alcohol) | **A1** (`readADC_SingleEnded(1)`) | A0 en `docs/ARQUITECTURA_Y_FLUJOS.md` |
| Ventana de prueba de alcoholemia | **7 s** (`millis() - inicioVentana < 7000`) | 8 s (`VENTANA_MQ3_MS 8000`) |
| Muestreo dentro de la ventana MQ-3 | **250 ms** (`delay(250)`) → ~28 lecturas | 500 ms |
| Muestreo continuo del MQ-135 | **500 ms** (`intervaloLectura = 500`) | 500 ms (igual) |
| Parpadeo LED rojo por aire degradado | **250 ms** | 400 ms (`PARPADEO_MS`) |

Es decir: **A0 = MQ-135, A1 = MQ-3; ventana de 7 s; muestreo de 250 ms durante
la prueba de alcohol y de 500 ms en el monitoreo continuo de aire.** El 8 s y el
mapeo A0 = MQ-3 son restos del firmware del ESP32-CAM.

---

## 4. Hardware y conexiones

### 4.1 Componentes

| Componente | Función |
|---|---|
| ESP32 DevKit (WiFi) | Controlador: lee sensores y botones, maneja actuadores, servidor HTTP |
| ADS1115 (I²C, dirección `0x48`) | ADC de 16 bits para los sensores analógicos; ganancia 1x → 0,125 mV/unidad |
| MQ-135 | Calidad de aire en cabina (canal A0) |
| MQ-3 | Alcohol en aliento (canal A1) |
| Mini ventilador + transistor TIP142 (Darlington) | Succiona aire hacia el MQ-3 durante la prueba |
| Buzzer pasivo | Alarmas sonoras (`tone()`), patrón distinto por tipo de alerta |
| LED verde | Sistema encendido |
| LED rojo | Prueba de alcohol en curso / parpadeo por aire degradado |
| Botón general | Enciende/apaga el sistema |
| Botón MQ-3 | Inicia la prueba de alcoholemia |
| Webcam de la laptop | Captura del rostro para la detección de somnolencia |

### 4.2 Mapa de pines (firmware actual)

| Pin ESP32 | Conexión | Modo |
|---|---|---|
| GPIO 18 | Botón general (sistema ON/OFF) | `INPUT`, activo en LOW (flanco 1→0) |
| GPIO 19 | Botón prueba MQ-3 | `INPUT`, activo en LOW (flanco 1→0) |
| GPIO 32 | LED verde | `OUTPUT` |
| GPIO 25 | LED rojo | `OUTPUT` |
| GPIO 33 | Buzzer pasivo | `OUTPUT` (`tone`) |
| GPIO 23 | Base del TIP142 → ventilador | `OUTPUT` |
| GPIO 21 | SDA del ADS1115 | I²C |
| GPIO 22 | SCL del ADS1115 | I²C |
| ADS1115 A0 | Salida analógica MQ-135 | — |
| ADS1115 A1 | Salida analógica MQ-3 | — |

> Los botones se leen con `INPUT` (sin pull-up interno) y se detecta la
> transición de 1 a 0, así que necesitan una resistencia de **pull-up externa**
> y el botón a GND.

### 4.3 Umbrales (escala ADC cruda del ADS1115)

| Sketch | MQ-135 (aire) | MQ-3 (alcohol) | Precalentamiento | `/status` |
|---|---|---|---|---|
| `funcional_prototipo_ultimo.ino` (**el que funciona con el dashboard**) | > 3500 (≈ 437 mV) | > 2000 (≈ 250 mV) | 10 s al arrancar | Sí |
| `AlertDrive_ESP32.ino` | > 3000 (≈ 375 mV) | > 1500 (≈ 187 mV) | No | No |

El backend necesita `GET /status`, que solo existe en
`funcional_prototipo_ultimo.ino`; por eso ese es el sketch de referencia. Los
comentarios de `backend/src/state/systemState.js` todavía mencionan 3000/1500,
pero el backend no recalcula: usa los flags `air_alert`/`alcohol_alert` que
envía el ESP32.

---

## 5. Funcionamiento del firmware

1. **Arranque**: todos los actuadores en LOW → precalentamiento de 10 s de los
   sensores químicos → inicia I²C y ADS1115 → conecta al WiFi → levanta el
   servidor HTTP en el puerto 80. El sistema queda **APAGADO** hasta presionar
   el botón general.
2. **Botón general (GPIO 18)**: alterna encendido/apagado. Encendido → LED verde
   ON. Apagado → todo OFF.
3. **Monitoreo continuo de aire** (solo con el sistema encendido): cada 500 ms
   lee A0 (MQ-135). Si supera el umbral → buzzer de aire (800 Hz, 2 ciclos) y
   el LED rojo parpadea cada 250 ms.
4. **Prueba de alcoholemia (GPIO 19)**, solo con el sistema encendido:
   LED rojo + ventilador ON → durante **7 s** lee A0 y A1 cada **250 ms** → si
   alguna lectura del MQ-3 supera el umbral, se marca alcohol detectado → al
   terminar apaga ventilador y LED rojo y, si hubo detección, suena el buzzer
   de alcohol (900 Hz, 2 ciclos).
5. **Somnolencia (vía HTTP)**: `GET /alarma` → si el sistema está encendido,
   suena el buzzer de microsueño (1600 Hz, 6 ciclos de doble pitido);
   `GET /ok` → apaga buzzer y LED rojo.

Endpoints HTTP (puerto 80):

| Endpoint | Llamado por | Acción |
|---|---|---|
| `GET /` | — | Texto "ESP32 AlertDrive OK" |
| `GET /alarma` | Script de detección | Buzzer de microsueño |
| `GET /ok` | Script de detección | Resetea buzzer y LED rojo |
| `GET /status` | Backend (cada 1,5 s) | JSON: `system_on`, `buzzer_on`, `led_red`, `led_green`, `fan_on`, `uptime_ms`, `wifi_rssi`, `air_raw`, `air_alert`, `alcohol_raw`, `alcohol_alert`, `mq3_test_active` |

Las lecturas se guardan en variables globales y `/status` solo devuelve esos
valores, así responde al instante sin leer el ADS1115 dentro del handler.

---

## 6. Detección de somnolencia (PC)

- **Dataset**: Kaggle, clases *Closed*/*Open*, 1452 imágenes (726 + 726);
  división 70/15/15 → 1016 / 217 / 219.
- **Modelo `EyeCNN`**: entrada en escala de grises de 64×64; 3 bloques
  Conv(3×3) + BatchNorm + ReLU + MaxPool (32 → 64 → 128 filtros) →
  Linear(8192, 256) + Dropout(0,5) → Linear(256, 2). **2 191 042 parámetros**.
- **Entrenamiento**: Adam (lr 1e-3), CrossEntropy, ReduceLROnPlateau, 15 épocas.
  Mejor *val accuracy* **99,08 %**; **test accuracy 99,54 %** (matriz de
  confusión [[121, 0], [1, 97]]: un solo ojo abierto clasificado como cerrado).
- **Pipeline en tiempo real**: webcam → MediaPipe Face Mesh (landmarks
  33/160/158/133/153/144 y 362/385/387/263/373/380) → recorte de cada ojo con
  margen → CNN → probabilidad de "abierto" promediada entre ambos ojos →
  `< 0,50` = cerrado.
- **Regla de alarma**: ojos cerrados de forma continua **≥ 3,0 s** → `GET /alarma`
  al ESP32 + `POST /api/drowsiness` al backend. Al reabrir los ojos → `GET /ok`
  + estado "abierto".
- **Formato del modelo en ejecución**: PyTorch FP32 (`eye_cnn_best.pth`, ver 3.1).

---

## 7. Backend y dashboard

**Backend** (`backend/`, Node + Express + Socket.IO, puerto 3000):

- `esp32Poller.js`: consulta `GET /status` cada 1,5 s (timeout de 2 s). Detecta
  transiciones (OK → alerta) de alcohol, aire y somnolencia y las registra en el
  historial; si el ESP32 no responde, marca `esp32_online:false` y registra la
  alerta "Se perdió conexión con el ESP32" sin caerse.
- `systemState.js`: estado en memoria que une ESP32 + detección; calcula mV
  (raw × 0,125) y el veredicto del MQ-3 (`waiting` → `testing` → `pass`/`fail`,
  con el pico máximo de mV de la prueba).
- Rutas: `/api/status`, `/api/alerts`, `/api/drowsiness`, `/api/frame`,
  `/video` (MJPEG), `/api/test-mq3`.
- Historial de alertas persistido en JSON (volumen Docker `alertdrive_alerts`).

**Dashboard** (`frontend/`, HTML/CSS/JS sin framework, nginx en :8080):
banner de estado general, módulo de somnolencia (semáforo + temporizador de ojos
cerrados), MQ-135 (mV + sparkline), MQ-3 (veredicto + mV máximo), video en vivo,
estado de LEDs/actuadores, IP y *uptime* del dispositivo, e historial de alertas.

---

## 8. Inconsistencias conocidas en el repositorio

Para no arrastrarlas al informe:

1. **README y `docs/ARQUITECTURA_Y_FLUJOS.md` están desactualizados**: describen
   ESP32-CAM, puertos 81/82, `deteccion_esp32cam.py`, MQ-3 en A0, ventana de 8 s
   y GPIO 4. Lo vigente es lo de este documento.
2. **`/test-mq3` no existe en el firmware actual**: el botón "Probar
   alcoholemia" del dashboard llama a `POST /api/test-mq3` → el backend reenvía
   al ESP32 → 404 → el backend responde 502. Hoy la prueba de alcohol solo se
   inicia con el **botón físico (GPIO 19)**.
3. **Dos sketches con umbrales distintos** (3500/2000 vs 3000/1500); el que
   funciona con el dashboard es `funcional_prototipo_ultimo.ino`.
4. **Credenciales WiFi escritas en el código** de ambos sketches.

---

## 9. Resumen de datos para el informe

| Pregunta | Respuesta |
|---|---|
| Modelo en la PC | PyTorch FP32 (`eye_cnn_best.pth`) |
| ¿INT8 probado en un dispositivo? | No; la conversión se detuvo en ONNX → TF, nunca se generó el `.tflite` |
| ¿GPIO 16/3/4/12 en uso? | No; pinout del ESP32-CAM, reemplazado por el "real pinout" (`fb4799d`) |
| Pines actuales | Botones 18/19, LED verde 32, LED rojo 25, buzzer 33, fan 23, I²C 21/22 |
| Canales ADS1115 | A0 = MQ-135 (aire), A1 = MQ-3 (alcohol) |
| Ventana MQ-3 | 7 s |
| Muestreo | 250 ms en la ventana MQ-3; 500 ms en el monitoreo continuo del MQ-135 |
| Umbral somnolencia | 3 s de ojos cerrados, prob. "abierto" < 0,5 |
| Precisión del modelo | 99,54 % en test (219 imágenes) |
