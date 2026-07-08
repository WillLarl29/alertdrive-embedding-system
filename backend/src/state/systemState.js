// Estado agregado en memoria: fusiona lo ultimo leido del ESP32 (/status)
// con el flag de somnolencia que empuja deteccion_esp32cam.py.
const state = {
  esp32_online: false,
  system_on: false,
  buzzer_on: false,
  led_red: false,
  led_green: false,
  fan_on: false,
  drowsy_alert: false,
  drowsy_confidence: null,
  alcohol_raw: 0,
  alcohol_alert: false,
  air_raw: 0,
  air_alert: false,
  mq3_test_active: false,
  uptime_ms: 0,
  wifi_rssi: 0,
  last_update: null,
};

function getState() {
  return state;
}

function updateFromEsp32(status) {
  Object.assign(state, status, {
    esp32_online: true,
    last_update: new Date().toISOString(),
  });
}

function markEsp32Offline() {
  state.esp32_online = false;
}

function updateDrowsiness(drowsy, confidence) {
  state.drowsy_alert = drowsy;
  state.drowsy_confidence = confidence;
  state.last_update = new Date().toISOString();
}

module.exports = { getState, updateFromEsp32, markEsp32Offline, updateDrowsiness };
