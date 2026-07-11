// Ultimo frame JPEG recibido de detection/*.py (webcam local), retransmitido
// como MJPEG en GET /video. Pub-sub simple: cada cliente conectado a /video
// se suscribe y recibe cada frame nuevo apenas llega, sin polling.
const { EventEmitter } = require("events");

const emitter = new EventEmitter();
emitter.setMaxListeners(0);

let latestFrame = null;
let latestAt = 0;

function setFrame(buf) {
  latestFrame = buf;
  latestAt = Date.now();
  emitter.emit("frame", buf);
}

function getFrame() {
  return latestFrame;
}

function isFresh(maxAgeMs = 5000) {
  return !!latestFrame && Date.now() - latestAt < maxAgeMs;
}

module.exports = { setFrame, getFrame, isFresh, emitter };
