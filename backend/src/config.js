require("dotenv").config();

const config = {
  esp32Ip: process.env.ESP32_IP || "192.168.1.50",
  esp32StreamPort: Number(process.env.ESP32_STREAM_PORT || 81),
  esp32ControlPort: Number(process.env.ESP32_CONTROL_PORT || 82),
  pollIntervalMs: Number(process.env.POLL_INTERVAL_MS || 1500),
  port: Number(process.env.PORT || 3000),
};

config.esp32StreamUrl = `http://${config.esp32Ip}:${config.esp32StreamPort}/`;
config.esp32ControlUrl = `http://${config.esp32Ip}:${config.esp32ControlPort}`;

module.exports = config;
