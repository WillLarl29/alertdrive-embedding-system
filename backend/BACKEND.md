# Backend de AlertDrive — descripción actual

Servidor Node.js/Express + Socket.IO que actúa como intermediario entre el
ESP32-CAM (hardware), el script Python de detección de somnolencia
(`detection/deteccion_esp32cam.py`) y el dashboard web (`frontend/`). Reemplaza
a Blynk como plataforma de monitoreo remoto del prototipo.

## Qué hace, en una frase

Cada 1.5s le pregunta al ESP32-CAM su estado (`/status`), lo fusiona con el
flag de somnolencia que le empuja el script Python, guarda un historial de
alertas, y transmite todo en vivo a quien tenga el dashboard abierto por
WebSocket — además de servirle el video de la cámara sin que el navegador
tenga que hablarle directo al ESP32.

## Estructura de archivos

```
backend/
├── package.json          # deps: express, socket.io, dotenv
├── .env.example           # variables de entorno (ver abajo)
├── Dockerfile              # imagen Node 22-alpine (ver docker-compose.yml en la raíz)
├── .dockerignore
├── data/                   # (se crea sola) alerts.json persistido; volumen en Docker
└── src/
    ├── server.js                    # entrypoint: arma Express + Socket.IO y arranca todo
    ├── config.js                    # variables de entorno con defaults
    ├── state/
    │   └── systemState.js           # el "estado actual" en memoria (single source of truth)
    ├── services/
    │   ├── esp32Poller.js           # sondea /status del ESP32 cada POLL_INTERVAL_MS
    │   └── alertStore.js            # historial de alertas (memoria + data/alerts.json)
    └── routes/
        ├── status.js                 # GET  /api/status
        ├── alerts.js                 # GET  /api/alerts
        ├── drowsiness.js             # POST /api/drowsiness  (lo llama el script Python)
        ├── control.js                # POST /api/test-mq3    (proxy al ESP32)
        └── stream.js                 # GET  /video            (proxy del stream MJPEG del ESP32)
```

## Piezas, una por una

### `config.js`
Lee de `.env` (o usa defaults) y arma las URLs del ESP32 una sola vez:

| Variable | Default | Uso |
|---|---|---|
| `ESP32_IP` | `192.168.1.50` | IP del ESP32-CAM en la red |
| `ESP32_STREAM_PORT` | `81` | Puerto del stream MJPEG |
| `ESP32_CONTROL_PORT` | `82` | Puerto de `/status`, `/alarma`, `/ok`, `/test-mq3` |
| `POLL_INTERVAL_MS` | `1500` | Cada cuánto se consulta `/status` |
| `PORT` | `3000` | Puerto donde escucha este backend |

### `state/systemState.js`
Un objeto en memoria (no hay base de datos) con el último estado conocido del
sistema completo: `esp32_online`, `system_on`, `buzzer_on`, `led_red`,
`led_green`, `fan_on`, `drowsy_alert` (+ `drowsy_confidence`), `alcohol_raw`,
`alcohol_alert`, `air_raw`, `air_alert`, `mq3_test_active`, `uptime_ms`,
`wifi_rssi`, `last_update`. Expone tres mutadores: `updateFromEsp32()`,
`markEsp32Offline()`, `updateDrowsiness()`.

### `services/esp32Poller.js`
Un `setInterval` que:
1. Le hace `GET /status` al ESP32 (timeout 2s).
2. Si responde, actualiza `systemState`, detecta transiciones OK→ALERTA
   (alcohol, aire, somnolencia) y las registra en `alertStore`.
3. Si falla (ESP32 apagado/inalcanzable), marca `esp32_online:false`, registra
   una alerta de "conexión perdida" (solo la primera vez que pasa, no en cada
   intento fallido) y sigue reintentando solo — **nunca tira el proceso**.
4. Emite el estado (y cualquier alerta nueva) por Socket.IO a todos los
   clientes conectados.

### `services/alertStore.js`
Historial de alertas: array en memoria (tope 200) + persistido en
`backend/data/alerts.json` en cada cambio. `data/` es la carpeta que se monta
como volumen Docker (`alertdrive_alerts`) para que el historial sobreviva a
`docker compose down`. Expone `addAlert(type, detail)` y `getRecent(n)`.

### Rutas (`src/routes/`)
- **`GET /api/status`** — snapshot actual de `systemState` (para la carga
  inicial de la página, antes de que conecte el WebSocket).
- **`GET /api/alerts?limit=N`** — últimas N alertas (default 50).
- **`POST /api/drowsiness`** — la usa `deteccion_esp32cam.py` para avisar
  `{status: "cerrado"|"abierto", confidence}`; actualiza el estado, registra
  alerta en la transición a "cerrado", y emite por socket.
- **`POST /api/test-mq3`** — reenvía al ESP32 (`/test-mq3`) para disparar
  remotamente la ventana de prueba de alcoholemia desde el botón del
  dashboard.
- **`GET /video`** — hace de proxy del stream MJPEG del ESP32 (puerto 81) para
  que el navegador nunca le hable directo al ESP32 (mismo origen, sin CORS).
  Tiene un timeout de conexión de 3s: si el ESP32 no responde, corta rápido
  con `502` en vez de dejar la conexión colgada indefinidamente.

### `server.js`
Arma Express, monta las rutas bajo `/api` (más `/video` en la raíz), sirve
`../frontend` como estático (solo relevante corriendo **fuera** de Docker —
dentro del contenedor Docker esa carpeta no existe, porque ahí el frontend
corre en su propio contenedor nginx que le hace de proxy a este backend), y
arranca el poller del ESP32. Al conectar, cada cliente de Socket.IO recibe de
entrada el estado actual y las últimas 50 alertas.

## Flujo de datos (resumen)

```
ESP32-CAM :82/status ──(poll 1.5s)──► esp32Poller ──► systemState ──┐
detection/*.py ──POST /api/drowsiness──────────────► systemState ──┼──► io.emit("state"/"alert")
navegador ──GET /video, /api/status, /api/alerts────────────────────┘         │
                                                                                ▼
                                                                         dashboard (frontend/)
```

## Cómo correr

**Local (sin Docker):**
```
cd backend
pnpm install        # o: npm install
cp .env.example .env
pnpm start           # node src/server.js — sirve el dashboard en :3000
```

**Docker:** ver `docker-compose.yml` en la raíz — este servicio se llama
`backend`, expone `:3000` y persiste `data/` en el volumen `alertdrive_alerts`.

## Qué NO hace (todavía)

- No tiene autenticación ni control de acceso — pensado para red local/demo.
- No tiene tests automatizados.
- El historial de alertas es un archivo JSON plano, no una base de datos real
  (suficiente para el volumen de datos de un prototipo).
