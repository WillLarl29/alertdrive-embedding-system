const ALERT_LABELS = {
  alcohol_alert: "Alcohol",
  air_alert: "Calidad de aire",
  drowsy_alert: "Somnolencia",
  esp32_offline: "Conexión",
};

function fmtTime(iso) {
  const d = new Date(iso);
  return d.toLocaleTimeString("es-PE", { hour12: false });
}

function alertRowHtml(a) {
  const label = ALERT_LABELS[a.type] || a.type;
  const detail = a.detail && a.detail.label ? a.detail.label : "";
  return `
    <td class="time">${fmtTime(a.timestamp)}</td>
    <td class="type" data-type="${a.type}"><span class="type-dot"></span>${label}</td>
    <td>${detail}</td>
  `;
}

function renderAlerts(alerts) {
  const body = document.getElementById("alerts-body");

  if (!alerts || alerts.length === 0) {
    body.innerHTML = '<tr class="empty-row"><td colspan="3">Sin alertas todavía</td></tr>';
    return;
  }

  body.innerHTML = alerts.map((a) => `<tr>${alertRowHtml(a)}</tr>`).join("");
}

function prependAlert(alert) {
  const body = document.getElementById("alerts-body");
  const emptyRow = body.querySelector(".empty-row");
  if (emptyRow) emptyRow.remove();

  const row = document.createElement("tr");
  row.className = "alert-row-new";
  row.innerHTML = alertRowHtml(alert);
  body.prepend(row);
}
