const config = require("../config");
const systemState = require("../state/systemState");
const alertStore = require("../services/alertStore");

function checkTransition(io, field, label, prev, next, detailFn) {
  if (!prev[field] && next[field]) {
    const alert = alertStore.addAlert(field, { label, ...detailFn(next) });
    io.emit("alert", alert);
  }
}

function startEsp32Poller(io) {
  setInterval(async () => {
    const prev = { ...systemState.getState() };

    try {
      const res = await fetch(`${config.esp32ControlUrl}/status`, {
        signal: AbortSignal.timeout(2000),
      });
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      const status = await res.json();

      // Pasamos la IP del ESP32 para mostrarla en el dashboard
      systemState.updateFromEsp32(status, config.esp32Ip);
      const next = systemState.getState();

      checkTransition(io, "alcohol_alert", "Alcohol sobre umbral", prev, next, (s) => ({
        alcohol_raw: s.alcohol_raw,
        alcohol_mv: s.alcohol_mv,
      }));
      checkTransition(io, "air_alert", "Calidad de aire degradada", prev, next, (s) => ({
        air_raw: s.air_raw,
        air_mv: s.air_mv,
      }));
      checkTransition(io, "drowsy_alert", "Somnolencia detectada", prev, next, (s) => ({
        confidence: s.drowsy_confidence,
      }));

      io.emit("state", next);
    } catch (err) {
      if (prev.esp32_online) {
        const alert = alertStore.addAlert("esp32_offline", { label: "Se perdió conexión con el ESP32" });
        io.emit("alert", alert);
      }
      systemState.markEsp32Offline();
      io.emit("state", systemState.getState());
    }
  }, config.pollIntervalMs);
}

module.exports = { startEsp32Poller };
