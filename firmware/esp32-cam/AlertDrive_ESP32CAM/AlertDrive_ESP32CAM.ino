/*
 * AlertDrive_ESP32CAM.ino
 *
 * Firmware exclusivo para la ESP32-CAM AI-Thinker.
 * Responsabilidad UNICA: capturar y transmitir video MJPEG.
 *
 * Puerto 81  /        -> stream MJPEG (consumido por deteccion_esp32cam.py
 *                        y por el proxy de video del backend Node.js)
 * Puerto 82  /status  -> JSON con estado basico de la camara (health-check)
 *
 * El buzzer, los sensores (MQ-3, MQ-135), los LEDs, el ventilador y los
 * botones fisicos estan en el ESP32 separado (AlertDrive_ESP32.ino).
 * La deteccion de somnolencia llama a /alarma en el ESP32, NO aqui.
 */

#include "esp_camera.h"
#include <WiFi.h>
#include "esp_http_server.h"

// ===== CONFIGURACION WIFI =====
const char* ssid     = "Will";
const char* password = "willman123";

// ===== Pines camara AI Thinker =====
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// ===== Handler del stream MJPEG =====
#define PART_BOUNDARY "123456789000000000000987654321"
static const char* STREAM_CONTENT_TYPE =
    "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* STREAM_PART =
    "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

httpd_handle_t stream_httpd = NULL;
httpd_handle_t status_httpd = NULL;

unsigned long boot_time = 0;

// ===== Stream handler =====
static esp_err_t stream_handler(httpd_req_t* req) {
  camera_fb_t* fb  = NULL;
  esp_err_t    res = ESP_OK;
  char*        part_buf[64];

  res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
  if (res != ESP_OK) return res;

  while (true) {
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Error capturando frame");
      res = ESP_FAIL;
      break;
    }

    res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
    if (res != ESP_OK) break;

    size_t hlen = snprintf((char*)part_buf, 64, STREAM_PART, fb->len);
    res = httpd_resp_send_chunk(req, (const char*)part_buf, hlen);
    if (res != ESP_OK) break;

    res = httpd_resp_send_chunk(req, (const char*)fb->buf, fb->len);
    esp_camera_fb_return(fb);
    fb = NULL;
    if (res != ESP_OK) break;
  }

  if (fb) esp_camera_fb_return(fb);
  return res;
}

// ===== Status handler (health-check JSON) =====
static esp_err_t status_handler(httpd_req_t* req) {
  char buf[200];
  int  len = snprintf(buf, sizeof(buf),
      "{"
      "\"cam_ok\":true,"
      "\"uptime_ms\":%lu,"
      "\"wifi_rssi\":%d,"
      "\"stream_url\":\"http://%s:81/\""
      "}",
      (unsigned long)(millis()),
      WiFi.RSSI(),
      WiFi.localIP().toString().c_str());
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, buf, len);
}

// ===== Servidor de stream (puerto 81) =====
void start_stream_server() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 81;

  httpd_uri_t stream_uri = {
    .uri      = "/",
    .method   = HTTP_GET,
    .handler  = stream_handler,
    .user_ctx = NULL
  };

  if (httpd_start(&stream_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(stream_httpd, &stream_uri);
    Serial.printf("Stream MJPEG en http://%s:81/\n", WiFi.localIP().toString().c_str());
  }
}

// ===== Servidor de status (puerto 82) =====
void start_status_server() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 82;

  httpd_uri_t status_uri = {
    .uri      = "/status",
    .method   = HTTP_GET,
    .handler  = status_handler,
    .user_ctx = NULL
  };

  if (httpd_start(&status_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(status_httpd, &status_uri);
    Serial.printf("Status cam en http://%s:82/status\n", WiFi.localIP().toString().c_str());
  }
}

// ============================================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== AlertDrive ESP32-CAM ===");

  // Inicializar camara
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0  = Y2_GPIO_NUM;
  config.pin_d1  = Y3_GPIO_NUM;
  config.pin_d2  = Y4_GPIO_NUM;
  config.pin_d3  = Y5_GPIO_NUM;
  config.pin_d4  = Y6_GPIO_NUM;
  config.pin_d5  = Y7_GPIO_NUM;
  config.pin_d6  = Y8_GPIO_NUM;
  config.pin_d7  = Y9_GPIO_NUM;
  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size   = FRAMESIZE_QVGA;  // 320x240
  config.jpeg_quality = 12;
  config.fb_count     = 2;
  config.fb_location  = CAMERA_FB_IN_PSRAM;
  config.grab_mode    = CAMERA_GRAB_LATEST;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Error camara: 0x%x\n", err);
    delay(3000);
    ESP.restart();
  }
  Serial.println("Camara OK");

  // WiFi
  WiFi.begin(ssid, password);
  Serial.print("Conectando WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\nWiFi OK - IP: %s\n", WiFi.localIP().toString().c_str());

  // Servidores
  start_stream_server();
  start_status_server();

  boot_time = millis();
  Serial.println("\n=== ESP32-CAM lista ===");
}

void loop() {
  // El stream corre en tareas del RTOS en segundo plano.
  // Solo mantenemos el watchdog feliz.
  delay(10000);
}
