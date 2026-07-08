// Renderiza el estado del sistema (banner, KPIs, meters, indicadores de actuadores).
// UMBRAL_* y METER_MAX deben coincidir con las constantes del firmware
// (AlertDrive_ESP32CAM.ino: UMBRAL_ALCOHOL, UMBRAL_AIRE).
const UMBRAL_ALCOHOL = 9000;
const UMBRAL_AIRE = 9000;
const METER_MAX = 20000; // rango visual del meter; ajustar tras calibrar los sensores

const ICON_OK = '<svg viewBox="0 0 24 24" width="26" height="26" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="m20 6-11 11-5-5"/></svg>';
const ICON_ALERT = '<svg viewBox="0 0 24 24" width="26" height="26" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M12 9v4"/><path d="m10.3 3.6-8 14A1.5 1.5 0 0 0 3.6 20h16.8a1.5 1.5 0 0 0 1.3-2.4l-8-14a1.5 1.5 0 0 0-2.6 0Z"/><path d="M12 17h.01"/></svg>';
const ICON_IDLE = '<svg viewBox="0 0 24 24" width="26" height="26" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="9"/><path d="M12 8v5"/><path d="M12 16h.01"/></svg>';

function fmtBool(value, onText, offText) {
  return value ? onText : offText;
}

function renderMeter(prefix, raw, alert, max) {
  const pct = Math.max(0, Math.min(100, (raw / max) * 100));
  const fill = document.getElementById(`meter-${prefix}-fill`);
  const value = document.getElementById(`meter-${prefix}-value`);
  const maxEl = document.getElementById(`meter-${prefix}-max`);
  const status = document.getElementById(`meter-${prefix}-status`);

  value.textContent = raw;
  maxEl.textContent = max;
  fill.style.width = `${pct}%`;

  let severity = "normal";
  if (alert) severity = "critical";
  else if (pct >= 70) severity = "warning";
  fill.dataset.severity = severity;

  status.dataset.status = alert ? "critical" : "good";
  status.textContent = alert ? "Alerta: sobre el umbral" : "Normal";
}

function renderIndicator(id, stateId, on) {
  document.getElementById(id).dataset.on = on ? "true" : "false";
  const stateEl = document.getElementById(stateId);
  stateEl.dataset.on = on ? "true" : "false";
  stateEl.textContent = on ? "Encendido" : "Apagado";
}

function renderVideoAvailability(online) {
  document.getElementById("video-placeholder").hidden = online;
  document.getElementById("live-badge").hidden = !online;
}

function renderHero(state) {
  const hero = document.getElementById("hero-status");
  const icon = document.getElementById("hero-icon");
  const title = document.getElementById("hero-title");
  const sub = document.getElementById("hero-sub");

  if (!state.system_on) {
    hero.dataset.status = "idle";
    icon.innerHTML = ICON_IDLE;
    title.textContent = "Sistema apagado";
    sub.textContent = "Encendé el sistema en el dispositivo para empezar a monitorear";
    return;
  }

  const riesgos = [];
  if (state.drowsy_alert) riesgos.push("somnolencia");
  if (state.alcohol_alert) riesgos.push("nivel de alcohol");
  if (state.air_alert) riesgos.push("calidad de aire");

  if (riesgos.length > 0) {
    hero.dataset.status = "critical";
    icon.innerHTML = ICON_ALERT;
    title.textContent = "¡Atención! Riesgo detectado";
    sub.textContent = `Revisar: ${riesgos.join(", ")}`;
  } else {
    hero.dataset.status = "good";
    icon.innerHTML = ICON_OK;
    title.textContent = "Todo en orden";
    sub.textContent = "Sistema encendido, sin alertas activas";
  }
}

function renderState(state) {
  // Conexion con el ESP32-CAM (warning, no critical: significa "sin datos", no un riesgo detectado)
  const connChip = document.getElementById("conn-chip");
  const connLabel = document.getElementById("conn-label");
  connChip.dataset.status = state.esp32_online ? "good" : "warning";
  connLabel.textContent = state.esp32_online ? "ESP32-CAM conectado" : "ESP32-CAM sin conexión";

  renderVideoAvailability(state.esp32_online);
  renderHero(state);

  // KPIs
  const tileSystem = document.getElementById("tile-system");
  tileSystem.dataset.status = state.system_on ? "good" : "";
  document.getElementById("val-system").textContent = fmtBool(state.system_on, "Encendido", "Apagado");

  const tileDrowsy = document.getElementById("tile-drowsy");
  tileDrowsy.dataset.status = state.drowsy_alert ? "critical" : "good";
  document.getElementById("val-drowsy").textContent = fmtBool(state.drowsy_alert, "Detectado", "Normal");

  const tileAlcohol = document.getElementById("tile-alcohol");
  tileAlcohol.dataset.status = state.alcohol_alert ? "critical" : "good";
  document.getElementById("val-alcohol").textContent = fmtBool(state.alcohol_alert, "Sobre umbral", "Normal");

  const tileAir = document.getElementById("tile-air");
  tileAir.dataset.status = state.air_alert ? "critical" : "good";
  document.getElementById("val-air").textContent = fmtBool(state.air_alert, "Degradada", "Normal");

  // Meters
  renderMeter("alcohol", state.alcohol_raw || 0, state.alcohol_alert, METER_MAX);
  renderMeter("air", state.air_raw || 0, state.air_alert, METER_MAX);

  // Actuadores
  renderIndicator("ind-led-red", "state-led-red", state.led_red);
  renderIndicator("ind-led-green", "state-led-green", state.led_green);
  renderIndicator("ind-fan", "state-fan", state.fan_on);
  renderIndicator("ind-buzzer", "state-buzzer", state.buzzer_on);

  // Boton de prueba: deshabilitado si el sistema esta apagado o ya hay una ventana activa
  const btn = document.getElementById("btn-test-mq3");
  btn.disabled = !state.system_on || state.mq3_test_active;
  btn.textContent = state.mq3_test_active ? "En curso…" : "Probar";
}
