const socket = io();

// Mientras el modo demo esta activo, ignorar el estado real entrante para
// que no pise la simulacion en pantalla.
socket.on("state", (s) => { if (!demoActive) renderState(s); });
socket.on("alerts", (a) => { if (!demoActive) renderAlerts(a); });
socket.on("alert", (a) => { if (!demoActive) prependAlert(a); });

// Distinguir "backend/contenedor caido" de "ESP32 sin conexion": si el socket
// mismo se desconecta, el problema es del servidor (p.ej. contenedor Docker
// detenido), no del dispositivo.
socket.on("disconnect", () => {
  if (demoActive) return;
  const chip  = document.getElementById("conn-chip");
  const label = document.getElementById("conn-label");
  if (chip)  chip.dataset.status = "critical";
  if (label) label.textContent   = "Backend desconectado";
});

// Respaldo inmediato: si el <img> del stream falla, no esperar al proximo
// tick del poller para ocultar el badge "EN VIVO" y mostrar el placeholder.
document.getElementById("video-stream").addEventListener("error", () => {
  document.getElementById("video-placeholder").hidden = false;
  document.getElementById("live-badge").hidden = true;
});

document.getElementById("btn-test-mq3").addEventListener("click", async (e) => {
  e.target.disabled = true;
  try {
    await fetch("/api/test-mq3", { method: "POST" });
  } catch (err) {
    console.error("No se pudo iniciar la prueba de nivel de alcohol", err);
  }
});

// Carga inicial por REST, por si el socket tarda en conectar
fetch("/api/status").then((r) => r.json()).then(renderState).catch(() => {});
fetch("/api/alerts").then((r) => r.json()).then(renderAlerts).catch(() => {});
