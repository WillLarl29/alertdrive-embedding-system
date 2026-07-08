// Modo demo: pisa la vista con datos simulados para presentaciones sin
// hardware conectado. No toca el backend real ni persiste entre recargas.
let demoActive = false;
let demoScenario = "normal";

const DEMO_BASE = {
  esp32_online: true,
  system_on: true,
  fan_on: false,
  alcohol_raw: 0, // sin consumo de alcohol: el MQ-3 no se lee fuera de la ventana de prueba
  alcohol_alert: false,
  air_raw: 3200,
  air_alert: false,
  mq3_test_active: false,
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
  last_update: new Date().toISOString(),
};

const DEMO_STATE_ALERT = {
  ...DEMO_BASE,
  buzzer_on: true,
  led_red: true,
  led_green: true,
  drowsy_alert: true,
  drowsy_confidence: 0.93,
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

function applyDemoScenario(scenario) {
  const wasAlert = demoScenario === "alert";
  demoScenario = scenario;
  const isAlert = scenario === "alert";

  renderState(isAlert ? DEMO_STATE_ALERT : DEMO_STATE_NORMAL);
  document.getElementById("video-stream").src = isAlert ? "/assets/cerrado.jpeg" : "/assets/abierto.jpeg";
  document.getElementById("video-placeholder").hidden = true;
  document.getElementById("live-badge").hidden = false;
  document.getElementById("btn-test-mq3").disabled = true;

  document.getElementById("scenario-normal").dataset.active = String(!isAlert);
  document.getElementById("scenario-alert").dataset.active = String(isAlert);

  // Al pasar de Normal a Riesgo, sumar la alerta al historial en el momento,
  // igual que haria el sistema real ante una transicion OK -> ALERTA.
  if (isAlert && !wasAlert) {
    prependAlert({
      type: "drowsy_alert",
      detail: { label: "Somnolencia detectada", confidence: DEMO_STATE_ALERT.drowsy_confidence },
      timestamp: new Date().toISOString(),
    });
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
document.getElementById("scenario-alert").addEventListener("click", () => applyDemoScenario("alert"));
