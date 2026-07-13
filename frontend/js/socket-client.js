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

// La disponibilidad del video la decide el backend (sabe con certeza si hay
// frames frescos) y la empuja por socket - el evento "load" del <img> NO es
// confiable para un stream multipart que nunca termina (varios navegadores
// no lo disparan nunca). El <img> solo se usa para pintar los pixeles; el
// placeholder/badge se controlan por "video_status", no por sus eventos.
const videoStream = document.getElementById("video-stream");

function reloadVideoStream() {
  videoStream.src = "/video?_=" + Date.now();
}

videoStream.addEventListener("error", () => {
  // El <img> sigue reintentando abrir la conexion en el fondo; cuando el
  // backend detecte frames frescos, "video_status" fuerza un reload igual.
  if (!demoActive) setTimeout(() => { if (!demoActive) reloadVideoStream(); }, 3000);
});

socket.on("video_status", ({ available }) => {
  if (demoActive) return;
  renderVideoAvailability(available);
  if (available) reloadVideoStream();
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
