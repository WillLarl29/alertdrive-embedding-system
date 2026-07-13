const path = require("path");
const express = require("express");
const { createServer } = require("http");
const { Server } = require("socket.io");

const config = require("./config");
const systemState = require("./state/systemState");
const alertStore = require("./services/alertStore");
const frameBuffer = require("./state/frameBuffer");
const { startEsp32Poller } = require("./services/esp32Poller");

const statusRoute = require("./routes/status");
const alertsRoute = require("./routes/alerts");
const streamRoute = require("./routes/stream");
const controlRoute = require("./routes/control");
const drowsinessRoute = require("./routes/drowsiness");
const frameRoute = require("./routes/frame");

const app = express();
const httpServer = createServer(app);
const io = new Server(httpServer);

app.use(express.static(path.join(__dirname, "..", "..", "frontend")));
app.use("/api", statusRoute);
app.use("/api", alertsRoute);
app.use("/api", controlRoute);
app.use("/api", drowsinessRoute(io));
app.use("/api", frameRoute);
app.use(streamRoute);

io.on("connection", (socket) => {
  socket.emit("state", systemState.getState());
  socket.emit("alerts", alertStore.getRecent(50));
  socket.emit("video_status", { available: frameBuffer.isFresh() });
});

// El <img> del frontend no puede confiar en sus propios eventos load/error
// para un stream multipart (muchos navegadores nunca disparan "load" en un
// stream que no termina nunca); el backend es quien sabe con certeza si hay
// frames frescos, asi que empuja el estado por socket.
let videoAvailable = false;
setInterval(() => {
  const fresh = frameBuffer.isFresh();
  if (fresh !== videoAvailable) {
    videoAvailable = fresh;
    io.emit("video_status", { available: videoAvailable });
  }
}, 1000);

startEsp32Poller(io);

httpServer.listen(config.port, () => {
  console.log(`AlertDrive backend escuchando en http://localhost:${config.port}`);
  console.log(`Consultando ESP32 en ${config.esp32ControlUrl} (video: POST /api/frame desde detection/)`);
});
