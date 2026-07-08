const express = require("express");
const http = require("http");
const config = require("../config");

const router = express.Router();

// GET /video - reenvia el stream MJPEG del ESP32-CAM para que el frontend
// solo hable con el backend (mismo origen, sin problemas de CORS/mixed content).
router.get("/video", (req, res) => {
  // Sin timeout, un ESP32_IP inalcanzable (no "connection refused", sino sin
  // respuesta) cuelga la conexion TCP por decenas de segundos.
  const upstream = http.get(config.esp32StreamUrl, { timeout: 3000 }, (upstreamRes) => {
    res.writeHead(upstreamRes.statusCode, upstreamRes.headers);
    upstreamRes.pipe(res);
  });

  upstream.on("timeout", () => upstream.destroy(new Error("timeout")));

  upstream.on("error", () => {
    if (!res.headersSent) res.status(502).send("ESP32-CAM no disponible");
  });

  req.on("close", () => upstream.destroy());
});

module.exports = router;
