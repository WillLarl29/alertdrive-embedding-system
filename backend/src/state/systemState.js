// Estado agregado en memoria: fusiona lo ultimo leido del ESP32 (/status)
// con el flag de somnolencia que empuja el notebook Python.
const state = {
  // Conexion
  esp32_online: false,
  esp32_ip: "192.168.139.207",   // IP local del ESP32 (actualizada al conectar)

  // Sistema
  system_on: false,
  buzzer_on: false,
  led_red: false,
  led_green: false,
  fan_on: false,
  uptime_ms: 0,
  wifi_rssi: 0,
  last_update: null,

  // Modulo somnolencia (empujado por Python)
  drowsy_alert: false,
  drowsy_confidence: null,
  eye_close_seconds: 0,          // segundos acumulados con ojos cerrados

  // Modulo calidad de aire MQ-135 (lectura continua)
  air_raw: 0,
  air_mv: 0,                     // milivoltios (raw * 0.125)
  air_alert: false,              // true si raw > 3000

  // Modulo alcoholemia MQ-3 (bajo demanda)
  alcohol_raw: 0,
  alcohol_mv: 0,                 // milivoltios del pico maximo del test
  alcohol_alert: false,          // true si raw > 1500
  mq3_test_active: false,
  mq3_countdown_s: 0,            // segundos restantes de la ventana de 7s
  mq3_verdict: "waiting",        // "waiting" | "testing" | "pass" | "fail"
  mq3_max_mv: 0,                 // pico maximo de mv durante el ultimo test
};

function getState() {
  return { ...state };
}

function updateFromEsp32(status, ip) {
  // Calcular mv a partir del raw usando el factor del firmware (0.125 mV/unidad)
  const airMv   = (status.air_raw   || 0) * 0.125;
  const alcMv   = (status.alcohol_raw || 0) * 0.125;

  Object.assign(state, {
    esp32_online:    true,
    esp32_ip:        ip || state.esp32_ip,
    system_on:       status.system_on       ?? state.system_on,
    buzzer_on:       status.buzzer_on       ?? state.buzzer_on,
    led_red:         status.led_red         ?? state.led_red,
    led_green:       status.led_green       ?? state.led_green,
    fan_on:          status.fan_on          ?? state.fan_on,
    uptime_ms:       status.uptime_ms       ?? state.uptime_ms,
    wifi_rssi:       status.wifi_rssi       ?? state.wifi_rssi,
    air_raw:         status.air_raw         ?? state.air_raw,
    air_mv:          airMv,
    air_alert:       status.air_alert       ?? state.air_alert,
    alcohol_raw:     status.alcohol_raw     ?? state.alcohol_raw,
    alcohol_mv:      alcMv,
    alcohol_alert:   status.alcohol_alert   ?? state.alcohol_alert,
    mq3_test_active: status.mq3_test_active ?? state.mq3_test_active,
    last_update:     new Date().toISOString(),
  });

  // Actualizar veredicto MQ-3
  if (status.mq3_test_active) {
    state.mq3_verdict = "testing";
    if (alcMv > state.mq3_max_mv) state.mq3_max_mv = alcMv;
  } else if (state.mq3_verdict === "testing") {
    // La ventana acaba de terminar
    state.mq3_verdict = state.alcohol_alert ? "fail" : "pass";
  }
}

function markEsp32Offline() {
  state.esp32_online = false;
}

function updateDrowsiness(drowsy, confidence, eyeCloseSeconds) {
  state.drowsy_alert        = drowsy;
  state.drowsy_confidence   = confidence ?? null;
  state.eye_close_seconds   = eyeCloseSeconds ?? 0;
  state.last_update         = new Date().toISOString();
}

function resetMq3() {
  state.mq3_verdict  = "waiting";
  state.mq3_max_mv   = 0;
}

module.exports = { getState, updateFromEsp32, markEsp32Offline, updateDrowsiness, resetMq3 };
