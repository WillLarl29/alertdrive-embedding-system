const express = require("express");
const frameBuffer = require("../state/frameBuffer");

const router = express.Router();

const BOUNDARY = "frame";

// GET /video - reenvia como MJPEG los frames que empuja detection/*.py
// (webcam local + POST /api/frame), asi el frontend solo habla con el
// backend (mismo origen, sin problemas de CORS/mixed content).
router.get("/video", (req, res) => {
  if (!frameBuffer.isFresh()) {
    res.status(502).send("Camara no disponible");
    return;
  }

  res.writeHead(200, {
    "Content-Type": `multipart/x-mixed-replace; boundary=${BOUNDARY}`,
    "Cache-Control": "no-cache, private",
    Connection: "close",
    Pragma: "no-cache",
  });

  const writeFrame = (buf) => {
    res.write(`--${BOUNDARY}\r\nContent-Type: image/jpeg\r\nContent-Length: ${buf.length}\r\n\r\n`);
    res.write(buf);
    res.write("\r\n");
  };

  writeFrame(frameBuffer.getFrame());
  frameBuffer.emitter.on("frame", writeFrame);

  req.on("close", () => {
    frameBuffer.emitter.off("frame", writeFrame);
  });
});

module.exports = router;
