const express = require("express");
const frameBuffer = require("../state/frameBuffer");

const router = express.Router();

// POST /api/frame - empujado por detection/deteccion_dashboard.py con cada
// frame anotado de la webcam local (JPEG crudo en el body). Se retransmite
// en GET /video como stream MJPEG para el dashboard.
router.post("/frame", express.raw({ type: "image/jpeg", limit: "3mb" }), (req, res) => {
  if (!Buffer.isBuffer(req.body) || req.body.length === 0) {
    return res.status(400).json({ ok: false, error: "empty body" });
  }
  frameBuffer.setFrame(req.body);
  res.json({ ok: true });
});

module.exports = router;
