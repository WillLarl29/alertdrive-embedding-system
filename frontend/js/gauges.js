// gauges.js — Renderiza el estado del sistema en el dashboard.
// Umbrales deben coincidir con el firmware AlertDrive_ESP32.ino
const UMBRAL_ALCOHOL_RAW = 1500;   // unidades ADC
const UMBRAL_AIRE_RAW    = 3000;   // unidades ADC
const METER_MAX_RAW      = 20000;  // escala visual de las barras (no es el maximo fisico del ADC)

const ICON_OK    = '<svg viewBox="0 0 24 24" width="22" height="22" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="m20 6-11 11-5-5"/></svg>';
const ICON_ALERT = '<svg viewBox="0 0 24 24" width="22" height="22" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M12 9v4"/><path d="m10.3 3.6-8 14A1.5 1.5 0 0 0 3.6 20h16.8a1.5 1.5 0 0 0 1.3-2.4l-8-14a1.5 1.5 0 0 0-2.6 0Z"/><path d="M12 17h.01"/></svg>';
const ICON_IDLE  = '<svg viewBox="0 0 24 24" width="22" height="22" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="9"/><path d="M12 8v5"/><path d="M12 16h.01"/></svg>';

// ── Helpers ──────────────────────────────────────────────────────────────────

function setEl(id, text) {
  const el = document.getElementById(id);
  if (el) el.textContent = text;
}

// ── Módulo Hero (banner global) ───────────────────────────────────────────────

function renderHero(state) {
  const hero  = document.getElementById("hero-status");
  const icon  = document.getElementById("hero-icon");
  const title = document.getElementById("hero-title");
  const sub   = document.getElementById("hero-sub");
  const badge = document.getElementById("system-state-badge");
  const ipEl  = document.getElementById("meta-ip");

  if (ipEl && state.esp32_ip) ipEl.textContent = state.esp32_ip;
  if (badge) {
    badge.textContent = state.system_on ? "ENCENDIDO" : "APAGADO";
    badge.dataset.on  = state.system_on ? "true" : "false";
  }

  if (!state.system_on) {
    hero.dataset.status = "idle";
    icon.innerHTML      = ICON_IDLE;
    title.textContent   = "Sistema apagado";
    sub.textContent     = "Enciende el sistema en el dispositivo para empezar a monitorear";
    return;
  }

  const riesgos = [];
  if (state.drowsy_alert)   riesgos.push("somnolencia");
  if (state.alcohol_alert)  riesgos.push("nivel de alcohol");
  if (state.air_alert)      riesgos.push("calidad de aire");

  if (riesgos.length > 0) {
    hero.dataset.status = "critical";
    icon.innerHTML      = ICON_ALERT;
    title.textContent   = "¡Atención! Riesgo detectado";
    sub.textContent     = `Revisar: ${riesgos.join(", ")}`;
  } else {
    hero.dataset.status = "good";
    icon.innerHTML      = ICON_OK;
    title.textContent   = "Todo en orden";
    sub.textContent     = "Sistema encendido, sin alertas activas";
  }
}

// ── Fila de estado resumido (4 stat tiles) ───────────────────────────────────

function setStatTile(id, valueId, status, text) {
  const tile = document.getElementById(id);
  if (tile) tile.dataset.status = status;
  setEl(valueId, text);
}

function renderStatRow(state) {
  setStatTile("stat-system", "stat-system-value",
    state.system_on ? "good" : "idle",
    state.system_on ? "Encendido" : "Apagado");

  setStatTile("stat-drowsy", "stat-drowsy-value",
    !state.system_on ? "idle" : state.drowsy_alert ? "critical" : "good",
    !state.system_on ? "Sin datos" : state.drowsy_alert ? "¡Alerta!" : "Normal");

  setStatTile("stat-alcohol", "stat-alcohol-value",
    !state.system_on ? "idle" : state.alcohol_alert ? "critical" : "good",
    !state.system_on ? "Sin datos" : state.alcohol_alert ? "¡Positivo!" : "Normal");

  setStatTile("stat-air", "stat-air-value",
    !state.system_on ? "idle" : state.air_alert ? "critical" : "good",
    !state.system_on ? "Sin datos" : state.air_alert ? "¡Alerta!" : "Normal");
}

// ── Sensores (medidores MQ-3 / MQ-135) ───────────────────────────────────────

function renderMeter({ rawId, fillId, statusId, raw, alert, systemOn, alertText }) {
  const raw2 = raw || 0;
  const pct  = Math.min(100, (raw2 / METER_MAX_RAW) * 100);

  setEl(rawId, `${raw2} / ${METER_MAX_RAW}`);

  const fill = document.getElementById(fillId);
  if (fill) {
    fill.style.width = `${pct}%`;
    if (alert) fill.dataset.severity = "critical";
    else fill.removeAttribute("data-severity");
  }

  const status = document.getElementById(statusId);
  if (status) {
    status.textContent  = !systemOn ? "Sistema apagado" : alert ? alertText : "Normal";
    status.dataset.status = alert ? "critical" : "";
  }
}

function renderMeters(state) {
  renderMeter({
    rawId: "val-alcohol-raw", fillId: "fill-alcohol", statusId: "status-alcohol",
    raw: state.alcohol_raw, alert: state.alcohol_alert, systemOn: state.system_on,
    alertText: "¡Alcohol detectado!",
  });
  renderMeter({
    rawId: "val-air-raw", fillId: "fill-air", statusId: "status-air",
    raw: state.air_raw, alert: state.air_alert, systemOn: state.system_on,
    alertText: "Aire contaminado, ventilar",
  });

  const btn = document.getElementById("btn-test-mq3");
  if (btn) {
    btn.disabled    = !state.system_on || !!state.mq3_test_active;
    btn.textContent = state.mq3_test_active ? "Probando…" : "Probar";
  }
}

// ── Actuadores ────────────────────────────────────────────────────────────────

function renderActuators(state) {
  renderIndicator("ind-led-red",   "state-led-red",   state.led_red);
  renderIndicator("ind-led-green", "state-led-green", state.led_green);
  renderIndicator("ind-fan",       "state-fan",       state.fan_on);
  renderIndicator("ind-buzzer",    "state-buzzer",    state.buzzer_on);
}

function renderIndicator(id, stateId, on) {
  const ind   = document.getElementById(id);
  const stEl  = document.getElementById(stateId);
  if (ind)  ind.dataset.on   = on ? "true" : "false";
  if (stEl) {
    stEl.dataset.on   = on ? "true" : "false";
    stEl.textContent  = on ? "Encendido" : "Apagado";
  }
}

// ── Chip de conexión ─────────────────────────────────────────────────────────

function renderConnection(state) {
  const chip  = document.getElementById("conn-chip");
  const label = document.getElementById("conn-label");
  if (!chip || !label) return;
  chip.dataset.status  = state.esp32_online ? "good" : "warning";
  label.textContent    = state.esp32_online ? "ESP32 conectado" : "ESP32 sin conexión";
}

// ── Video disponibilidad ──────────────────────────────────────────────────────

function renderVideoAvailability(online) {
  const ph = document.getElementById("video-placeholder");
  const lb = document.getElementById("live-badge");
  if (ph) ph.hidden = online;
  if (lb) lb.hidden = !online;
}

// ── Función principal expuesta al socket-client.js ───────────────────────────

function renderState(state) {
  renderConnection(state);
  renderVideoAvailability(!!state.esp32_online);
  renderHero(state);
  renderStatRow(state);
  renderMeters(state);
  renderActuators(state);
}
