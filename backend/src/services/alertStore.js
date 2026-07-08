const fs = require("fs");
const path = require("path");

// data/ es el directorio que se monta como volumen en Docker (ver docker-compose.yml)
// para que el historial de alertas sobreviva a un `docker compose down`.
const DATA_DIR = path.join(__dirname, "..", "..", "data");
const ALERTS_FILE = path.join(DATA_DIR, "alerts.json");
const MAX_IN_MEMORY = 200;

fs.mkdirSync(DATA_DIR, { recursive: true });

let alerts = [];

function load() {
  try {
    const raw = fs.readFileSync(ALERTS_FILE, "utf8");
    alerts = JSON.parse(raw);
  } catch (err) {
    alerts = [];
  }
}

function persist() {
  fs.writeFile(ALERTS_FILE, JSON.stringify(alerts.slice(-MAX_IN_MEMORY), null, 2), () => {});
}

function addAlert(type, detail) {
  const alert = { type, detail, timestamp: new Date().toISOString() };
  alerts.push(alert);
  if (alerts.length > MAX_IN_MEMORY) alerts = alerts.slice(-MAX_IN_MEMORY);
  persist();
  return alert;
}

function getRecent(n = 50) {
  return alerts.slice(-n).reverse();
}

load();

module.exports = { addAlert, getRecent };
