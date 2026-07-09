const express = require("express");
const systemState = require("../state/systemState");
const alertStore = require("../services/alertStore");

// POST /api/drowsiness - empujado por detection/deteccion_esp32cam.py
// body: { status: "cerrado" | "abierto", confidence: number }
module.exports = function drowsinessRouter(io) {
  const router = express.Router();

  router.post("/drowsiness", express.json(), (req, res) => {
    const { status, confidence, eye_close_seconds } = req.body || {};
    const drowsy = status === "cerrado";
    const wasDrowsy = systemState.getState().drowsy_alert;

    systemState.updateDrowsiness(drowsy, confidence ?? null, eye_close_seconds ?? 0);

    if (drowsy && !wasDrowsy) {
      const alert = alertStore.addAlert("drowsy_alert", {
        label: "Somnolencia detectada",
        confidence: confidence ?? null,
      });
      io.emit("alert", alert);
    }

    io.emit("state", systemState.getState());
    res.json({ ok: true });
  });

  return router;
};
