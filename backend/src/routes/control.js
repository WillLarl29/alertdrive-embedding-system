const express = require("express");
const config = require("../config");

const router = express.Router();

// POST /api/test-mq3 - reenvia al ESP32 para disparar la ventana de prueba de alcoholemia
router.post("/test-mq3", async (req, res) => {
  try {
    const upstream = await fetch(`${config.esp32ControlUrl}/test-mq3`, {
      method: "POST",
      signal: AbortSignal.timeout(2000),
    });
    if (!upstream.ok) throw new Error(`HTTP ${upstream.status}`);
    res.json({ ok: true });
  } catch (err) {
    res.status(502).json({ ok: false, error: "ESP32-CAM no disponible" });
  }
});

module.exports = router;
