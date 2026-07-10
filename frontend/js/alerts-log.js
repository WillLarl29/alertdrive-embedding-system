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

function relTime(iso) {
  const min = Math.round((Date.now() - new Date(iso).getTime()) / 60000);
  if (min < 1) return "justo ahora";
  if (min < 60) return `hace ${min} min`;
  return `hace ${Math.round(min / 60)} h`;
}

function alertRowHtml(a) {
  const label = ALERT_LABELS[a.type] || a.type;
  const detail = a.detail && a.detail.label ? a.detail.label : "";
  return `
    <td class="time" title="${relTime(a.timestamp)}">${fmtTime(a.timestamp)}</td>
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
