// gauges.js — Renderiza el estado del sistema en los 4 módulos del dashboard.
// Umbrales deben coincidir con el firmware AlertDrive_ESP32.ino
const UMBRAL_ALCOHOL_RAW = 1500;   // unidades ADC
const UMBRAL_AIRE_RAW    = 3000;   // unidades ADC
const ADS_FACTOR_MV      = 0.125;  // mV por unidad ADC

// Histórico del MQ-135 para el mini-gráfico (últimas 40 lecturas)
const airHistory = [];
const AIR_HIST_MAX = 40;

const ICON_OK    = '<svg viewBox="0 0 24 24" width="22" height="22" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="m20 6-11 11-5-5"/></svg>';
const ICON_ALERT = '<svg viewBox="0 0 24 24" width="22" height="22" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M12 9v4"/><path d="m10.3 3.6-8 14A1.5 1.5 0 0 0 3.6 20h16.8a1.5 1.5 0 0 0 1.3-2.4l-8-14a1.5 1.5 0 0 0-2.6 0Z"/><path d="M12 17h.01"/></svg>';
const ICON_IDLE  = '<svg viewBox="0 0 24 24" width="22" height="22" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="9"/><path d="M12 8v5"/><path d="M12 16h.01"/></svg>';

// ── Helpers ──────────────────────────────────────────────────────────────────

function setEl(id, text) {
  const el = document.getElementById(id);
  if (el) el.textContent = text;
}

function setAttr(id, attr, val) {
  const el = document.getElementById(id);
  if (el) el.setAttribute(attr, val);
}

function fmtMs(ms) {
  if (!ms) return "0 s";
  if (ms < 60000) return `${Math.floor(ms / 1000)} s`;
  const m = Math.floor(ms / 60000);
  const s = Math.floor((ms % 60000) / 1000);
  return `${m}m ${s}s`;
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

// ── Módulo 1: Somnolencia ────────────────────────────────────────────────────

function renderDrowsy(state) {
  const module = document.getElementById("module-drowsy");
  const pill   = document.getElementById("pill-drowsy");
  const light  = document.getElementById("traffic-light");

  const drowsy  = !!state.drowsy_alert;
  const seconds = typeof state.eye_close_seconds === "number"
    ? state.eye_close_seconds.toFixed(1)
    : "0.0";

  // Semáforo
  if (light) light.dataset.state = drowsy ? "alert" : "ok";

  // Píldora de estado
  if (pill) {
    pill.dataset.status = drowsy ? "alert" : "ok";
    pill.textContent    = drowsy ? "¡ALERTA DE SOMNOLENCIA!" : "Conductor Alerta";
  }

  // Módulo border
  if (module) module.dataset.alert = drowsy ? "true" : "false";

  // Timer
  setEl("val-eye-timer", drowsy ? `${seconds} s` : "0.0 s");
}

// ── Módulo 2: Calidad de Aire (MQ-135) ──────────────────────────────────────

function renderAir(state) {
  const module = document.getElementById("module-air");
  const pill   = document.getElementById("pill-air");
  const alert  = !!state.air_alert;

  // mV: usar el campo ya calculado por el backend, o calcularlo aquí
  const mv = typeof state.air_mv === "number"
    ? state.air_mv
    : (state.air_raw || 0) * ADS_FACTOR_MV;

  setEl("val-air-mv", `${mv.toFixed(2)} mV`);

  const statusText = !state.system_on ? "Sin datos"
    : alert ? "⚠ Gases Detectados / Ventilar Cabina"
    : "✓ Aire Óptimo";

  setEl("val-air-status", statusText);

  if (pill) {
    pill.dataset.status = !state.system_on ? "idle" : alert ? "alert" : "ok";
    pill.textContent    = !state.system_on ? "Sin datos" : alert ? "Gases Detectados" : "Aire Óptimo";
  }
  if (module) module.dataset.alert = alert ? "true" : "false";

  // Actualizar histórico y redibujar mini-gráfico
  if (state.system_on && typeof mv === "number") {
    airHistory.push(mv);
    if (airHistory.length > AIR_HIST_MAX) airHistory.shift();
    drawSparkline("chart-air", airHistory, alert);
  }
}

// Mini-gráfico de líneas (canvas)
function drawSparkline(canvasId, data, isAlert) {
  const canvas = document.getElementById(canvasId);
  if (!canvas || data.length < 2) return;

  const ctx    = canvas.getContext("2d");
  const W      = canvas.offsetWidth || 260;
  const H      = canvas.height;
  canvas.width = W;

  const max  = Math.max(...data, 1);
  const min  = Math.min(...data, 0);
  const rng  = max - min || 1;
  const step = W / (data.length - 1);

  ctx.clearRect(0, 0, W, H);

  // Línea de umbral
  const thresholdMv = UMBRAL_AIRE_RAW * ADS_FACTOR_MV;
  const ty = H - ((thresholdMv - min) / rng) * H;
  ctx.strokeStyle = "rgba(208,59,59,0.4)";
  ctx.lineWidth   = 1;
  ctx.setLineDash([4, 4]);
  ctx.beginPath();
  ctx.moveTo(0, ty);
  ctx.lineTo(W, ty);
  ctx.stroke();
  ctx.setLineDash([]);

  // Área rellena
  const grad = ctx.createLinearGradient(0, 0, 0, H);
  const color = isAlert ? "#d03b3b" : "#2a78d6";
  grad.addColorStop(0, color + "55");
  grad.addColorStop(1, color + "00");

  ctx.beginPath();
  ctx.moveTo(0, H);
  data.forEach((v, i) => {
    const x = i * step;
    const y = H - ((v - min) / rng) * H;
    i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
  });
  ctx.lineTo((data.length - 1) * step, H);
  ctx.closePath();
  ctx.fillStyle = grad;
  ctx.fill();

  // Línea
  ctx.strokeStyle = color;
  ctx.lineWidth   = 2;
  ctx.lineJoin    = "round";
  ctx.beginPath();
  data.forEach((v, i) => {
    const x = i * step;
    const y = H - ((v - min) / rng) * H;
    i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
  });
  ctx.stroke();
}

// ── Módulo 3: Alcoholemia (MQ-3) ─────────────────────────────────────────────

function renderAlcohol(state) {
  const module  = document.getElementById("module-alcohol");
  const pill    = document.getElementById("pill-alcohol");
  const btn     = document.getElementById("btn-test-mq3");
  const verdict = state.mq3_verdict || "waiting";
  const testing = !!state.mq3_test_active;

  // Estado del test
  let stateText, verdictText, pillStatus;

  if (!state.system_on) {
    stateText   = "Sistema apagado";
    verdictText = "—";
    pillStatus  = "idle";
  } else if (testing) {
    stateText   = "Sople Ahora… ventana de 7s activa";
    verdictText = "En curso…";
    pillStatus  = "testing";
  } else if (verdict === "pass") {
    stateText   = "Test completado";
    verdictText = "✓ Apto para Conducir (Negativo)";
    pillStatus  = "ok";
  } else if (verdict === "fail") {
    stateText   = "Test completado";
    verdictText = "✗ ¡Conductor No Apto (Positivo)!";
    pillStatus  = "alert";
  } else {
    stateText   = "Esperando muestreo";
    verdictText = "—";
    pillStatus  = "idle";
  }

  setEl("val-mq3-state", stateText);
  setEl("val-mq3-verdict", verdictText);

  // Voltaje máximo registrado
  const maxMv = typeof state.mq3_max_mv === "number" && state.mq3_max_mv > 0
    ? `${state.mq3_max_mv.toFixed(2)} mV`
    : "— mV";
  setEl("val-mq3-max-mv", maxMv);

  if (pill) {
    pill.dataset.status = pillStatus;
    pill.textContent = testing ? "Muestreando…" :
      verdict === "pass" ? "Negativo" :
      verdict === "fail" ? "¡Positivo!" : "Esperando";
  }
  if (module) module.dataset.alert = verdict === "fail" ? "true" : "false";

  if (btn) {
    btn.disabled    = !state.system_on || testing;
    btn.textContent = testing ? "Prueba en curso…" : "Iniciar prueba de alcohol";
  }
}

// ── Módulo 4: Estado general del dispositivo ─────────────────────────────────

function renderDevice(state) {
  // Sistema ON/OFF
  setEl("val-system", state.system_on ? "ENCENDIDO" : "APAGADO");

  // IP del ESP32
  if (state.esp32_ip) {
    setEl("val-ip", state.esp32_ip);
    setEl("meta-ip", state.esp32_ip);
  }

  // RSSI
  setEl("val-rssi", state.wifi_rssi ? `${state.wifi_rssi} dBm` : "— dBm");

  // Uptime
  setEl("val-uptime", state.uptime_ms ? fmtMs(state.uptime_ms) : "—");

  // Actuadores
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
  renderDrowsy(state);
  renderAir(state);
  renderAlcohol(state);
  renderDevice(state);
}
