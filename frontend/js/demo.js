// Modo demo: pisa la vista con datos simulados para presentaciones sin
// hardware conectado. No toca el backend real ni persiste entre recargas.
let demoActive = false;
let demoScenario = "normal";
let demoTimer = null;

const ADS_FACTOR_MV = 0.125;

const DEMO_BASE = {
  esp32_online: true,
  esp32_ip: "192.168.139.207",
  system_on: true,
  fan_on: false,
  air_raw: 3200,
  air_mv: 3200 * ADS_FACTOR_MV,
  air_alert: false,
  alcohol_raw: 0, // sin consumo de alcohol: el MQ-3 no se lee fuera de la ventana de prueba
  alcohol_mv: 0,
  alcohol_alert: false,
  mq3_test_active: false,
  mq3_verdict: "waiting",
  mq3_countdown_s: 0,
  mq3_max_mv: 0,
  uptime_ms: 41 * 60 * 1000,
  wifi_rssi: -54,
};

const DEMO_STATE_NORMAL = {
  ...DEMO_BASE,
  buzzer_on: false,
  led_red: false,
  led_green: true,
  drowsy_alert: false,
  drowsy_confidence: 0.98,
  eye_close_seconds: 0,
  last_update: new Date().toISOString(),
};

const DEMO_STATE_SOMNOLENCIA = {
  ...DEMO_BASE,
  buzzer_on: true,
  led_red: true,
  led_green: true,
  drowsy_alert: true,
  drowsy_confidence: 0.93,
  eye_close_seconds: 3.4,
  last_update: new Date().toISOString(),
};

const DEMO_STATE_AIRE = {
  ...DEMO_BASE,
  air_raw: 15200,
  air_mv: 15200 * ADS_FACTOR_MV,
  air_alert: true,
  buzzer_on: true,
  led_red: true,
  led_green: true,
  drowsy_alert: false,
  drowsy_confidence: 0.98,
  eye_close_seconds: 0,
  last_update: new Date().toISOString(),
};

function demoAlerts() {
  const now = Date.now();
  return [
    {
      type: "drowsy_alert",
      detail: { label: "Somnolencia detectada", confidence: 0.91 },
      timestamp: new Date(now - 18 * 60 * 1000).toISOString(),
    },
    {
      type: "air_alert",
      detail: { label: "Calidad de aire degradada", air_raw: 15200 },
      timestamp: new Date(now - 47 * 60 * 1000).toISOString(),
    },
  ];
}

function setDemoToggleLabel() {
  const btn = document.getElementById("btn-demo-toggle");
  btn.dataset.active = demoActive ? "true" : "false";
  btn.textContent = demoActive ? "Salir de demo" : "Modo demo";
}

function clearDemoTimer() {
  if (demoTimer) {
    clearInterval(demoTimer);
    demoTimer = null;
  }
}

function setActiveScenarioButton(scenario) {
  ["normal", "somnolencia", "alcohol", "aire"].forEach((s) => {
    const btn = document.getElementById(`scenario-${s}`);
    if (btn) btn.dataset.active = String(s === scenario);
  });
}

function setVideoForScenario(scenario) {
  document.getElementById("video-stream").src = scenario === "somnolencia" ? "/assets/cerrado.jpeg" : "/assets/abierto.jpeg";
  document.getElementById("video-placeholder").hidden = true;
  document.getElementById("live-badge").hidden = false;
}

// Simula la rafaga de 7s del MQ-3 (boton "Probar"): sube el nivel de alcohol
// paso a paso y termina en veredicto "fail", igual que haria el ESP32 real.
function runAlcoholTestDemo() {
  const TARGET_RAW = 1850; // > UMBRAL_MQ3_ALCOHOL (1500)
  const STEPS = 4;
  let step = 0;

  const tick = () => {
    step += 1;
    const raw = Math.round((TARGET_RAW / STEPS) * step);
    const testing = {
      ...DEMO_BASE,
      led_red: true,
      led_green: true,
      fan_on: true,
      drowsy_confidence: 0.97,
      mq3_test_active: true,
      mq3_verdict: "testing",
      mq3_countdown_s: STEPS - step,
      alcohol_raw: raw,
      alcohol_mv: raw * ADS_FACTOR_MV,
      last_update: new Date().toISOString(),
    };
    renderState(testing);

    if (step >= STEPS) {
      clearDemoTimer();
      const final = {
        ...testing,
        mq3_test_active: false,
        mq3_verdict: "fail",
        mq3_max_mv: raw * ADS_FACTOR_MV,
        alcohol_alert: true,
        buzzer_on: true,
        fan_on: false,
      };
      renderState(final);
      prependAlert({
        type: "alcohol_alert",
        detail: { label: "Alcohol detectado", alcohol_raw: raw, alcohol_mv: Math.round(raw * ADS_FACTOR_MV) },
        timestamp: new Date().toISOString(),
      });
    }
  };

  tick();
  demoTimer = setInterval(tick, 700);
}

function applyDemoScenario(scenario) {
  clearDemoTimer();
  const wasScenario = demoScenario;
  demoScenario = scenario;

  setActiveScenarioButton(scenario);
  setVideoForScenario(scenario);
  document.getElementById("btn-test-mq3").disabled = true;

  if (scenario === "normal") {
    renderState(DEMO_STATE_NORMAL);
  } else if (scenario === "somnolencia") {
    renderState(DEMO_STATE_SOMNOLENCIA);
    if (wasScenario !== "somnolencia") {
      prependAlert({
        type: "drowsy_alert",
        detail: { label: "Somnolencia detectada", confidence: DEMO_STATE_SOMNOLENCIA.drowsy_confidence },
        timestamp: new Date().toISOString(),
      });
    }
  } else if (scenario === "aire") {
    renderState(DEMO_STATE_AIRE);
    if (wasScenario !== "aire") {
      prependAlert({
        type: "air_alert",
        detail: { label: "Calidad de aire degradada", air_raw: DEMO_STATE_AIRE.air_raw },
        timestamp: new Date().toISOString(),
      });
    }
  } else if (scenario === "alcohol") {
    runAlcoholTestDemo();
  }
}

function enterDemo() {
  demoActive = true;
  renderAlerts(demoAlerts());
  document.getElementById("demo-scenarios").hidden = false;
  applyDemoScenario("normal");
  setDemoToggleLabel();
}

function exitDemo() {
  demoActive = false;
  clearDemoTimer();
  document.getElementById("demo-scenarios").hidden = true;
  document.getElementById("video-stream").src = "/video";

  // Volver a pedir el estado real para no dejar la vista pisada por la demo.
  fetch("/api/status").then((r) => r.json()).then(renderState).catch(() => {});
  fetch("/api/alerts").then((r) => r.json()).then(renderAlerts).catch(() => {});

  setDemoToggleLabel();
}

document.getElementById("btn-demo-toggle").addEventListener("click", () => {
  if (demoActive) exitDemo();
  else enterDemo();
});

document.getElementById("scenario-normal").addEventListener("click", () => applyDemoScenario("normal"));
document.getElementById("scenario-somnolencia").addEventListener("click", () => applyDemoScenario("somnolencia"));
document.getElementById("scenario-alcohol").addEventListener("click", () => applyDemoScenario("alcohol"));
document.getElementById("scenario-aire").addEventListener("click", () => applyDemoScenario("aire"));
