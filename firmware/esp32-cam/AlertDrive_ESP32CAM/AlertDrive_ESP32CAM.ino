/*
 * AlertDrive_ESP32CAM.ino
 *
 * Firmware completo del prototipo AlertDrive (ESP32-CAM AI-Thinker).
 * Mapa de pines: cableado real del prototipo (los canales del ADS1115
 * difieren del PDF original - ver defines abajo).
 *
 * Puerto 81: stream MJPEG de la camara
 * Puerto 82: control y telemetria
 *   GET  /alarma    -> marca el flag de somnolencia (lo llama
 * deteccion_esp32cam.py) GET  /ok        -> limpia el flag de somnolencia GET
 * /estado    -> estado del buzzer en texto plano (compatibilidad) GET  /status
 * -> estado completo del sistema en JSON (consumido por el backend) POST
 * /test-mq3  -> dispara remotamente la ventana de prueba de alcoholemia
 *
 * El buzzer y el LED rojo se activan ante CUALQUIERA de estas 3 condiciones:
 * somnolencia (flag desde Python), alcohol sobre umbral, o aire sobre umbral.
 */

#include "esp_camera.h"
#include "esp_http_server.h"
#include <Adafruit_ADS1X15.h>
#include <WiFi.h>
#include <Wire.h>

// ===== CONFIGURACION WIFI =====
const char *ssid = "Will";
const char *password = "willman123";

// ===== Pines del prototipo =====
#define PIN_BUZZER 2
#define PIN_FAN_BASE 3 // base del TIP142, via R 220ohm
#define PIN_BTN_MQ3                                                            \
  4 // boton prueba MQ-3 (pull-down externo 10k) - comparte GPIO con el flash
    // LED onboard del AI-Thinker
#define PIN_LED_ROJO 12
#define PIN_LED_VERDE 13
#define PIN_I2C_SCL 14
#define PIN_I2C_SDA 15
#define PIN_BTN_POWER 16 // boton encendido general (pull-down externo 10k)

#define ADS1115_ADDR 0x48
#define CH_MQ3 1   // A1 (invertido respecto al PDF original)
#define CH_MQ135 0 // A0 (invertido respecto al PDF original)

// ===== Umbrales y tiempos (ajustar segun calibracion real de los sensores)
// =====
#define UMBRAL_ALCOHOL 9000 // lectura cruda ADS1115 (16 bit, ganancia default)
#define UMBRAL_AIRE 9000
#define VENTANA_MQ3_MS                                                         \
  8000 // duracion de la ventana de prueba (fan succionando + lectura)
#define PARPADEO_MS 400 // periodo de parpadeo del LED rojo en riesgo
#define DEBOUNCE_MS 250
#define INTERVALO_SENSOR_MS 500

// ===== Pines camara AI Thinker =====
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27
#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

Adafruit_ADS1115 ads;
bool ads_disponible = false;

// ===== Estado del sistema (leido por /status y por los handlers HTTP) =====
volatile bool drowsy_flag = false; // set por /alarma, limpiado por /ok
bool system_on = false;
bool led_verde_on = false;
bool led_rojo_on = false;
bool fan_on = false;
bool buzzer_activo = false;
bool alcohol_alert = false;
bool air_alert = false;
bool mq3_test_active = false;

int16_t alcohol_raw = 0;
int16_t air_raw = 0;

unsigned long mq3_test_start = 0;
unsigned long last_blink_toggle = 0;
unsigned long last_sensor_read = 0;
unsigned long last_btn_power_change = 0;
unsigned long last_btn_mq3_change = 0;
int last_btn_power_state = LOW;
int last_btn_mq3_state = LOW;

// ===== Melodias del buzzer por tipo de alerta (no bloqueante, sin delay())
// ===== Un paso con freq=0 es silencio. El patron se repite en loop mientras la
// alerta siga activa. Un cambio de tipo de alerta reinicia el patron.
struct BuzzerStep {
  int freq;
  unsigned long duracion_ms;
};

const BuzzerStep PATRON_SOMNOLENCIA[] = {
    {1600, 180}, {0, 100}, {1600, 180}, {0, 300}};
const BuzzerStep PATRON_ALCOHOL[] = {
    {900, 200}, {0, 200}, {900, 200}, {0, 800}};
const BuzzerStep PATRON_AIRE[] = {{800, 200}, {0, 350}, {800, 200}, {0, 1000}};

enum TipoAlerta {
  ALERTA_NINGUNA,
  ALERTA_SOMNOLENCIA,
  ALERTA_ALCOHOL,
  ALERTA_AIRE
};

TipoAlerta alerta_buzzer_actual = ALERTA_NINGUNA;
int buzzer_step = 0;
unsigned long buzzer_step_t0 = 0;

const BuzzerStep *patron_para_tipo(TipoAlerta tipo, int *len) {
  switch (tipo) {
  case ALERTA_SOMNOLENCIA:
    *len = 4;
    return PATRON_SOMNOLENCIA;
  case ALERTA_ALCOHOL:
    *len = 4;
    return PATRON_ALCOHOL;
  case ALERTA_AIRE:
    *len = 4;
    return PATRON_AIRE;
  default:
    *len = 0;
    return nullptr;
  }
}

// Prioridad si hay mas de una condicion de riesgo a la vez: somnolencia >
// alcohol > aire.
void actualizar_buzzer(unsigned long now) {
  TipoAlerta tipo = ALERTA_NINGUNA;
  if (drowsy_flag)
    tipo = ALERTA_SOMNOLENCIA;
  else if (alcohol_alert)
    tipo = ALERTA_ALCOHOL;
  else if (air_alert)
    tipo = ALERTA_AIRE;

  if (tipo != alerta_buzzer_actual) {
    alerta_buzzer_actual = tipo;
    buzzer_step = 0;
    buzzer_step_t0 = now;

    if (tipo == ALERTA_NINGUNA) {
      noTone(PIN_BUZZER);
      buzzer_activo = false;
      return;
    }

    int len;
    const BuzzerStep *patron = patron_para_tipo(tipo, &len);
    if (patron[0].freq > 0)
      tone(PIN_BUZZER, patron[0].freq);
    else
      noTone(PIN_BUZZER);
    buzzer_activo = patron[0].freq > 0;
    return;
  }

  if (tipo == ALERTA_NINGUNA)
    return;

  int len;
  const BuzzerStep *patron = patron_para_tipo(tipo, &len);
  if (now - buzzer_step_t0 >= patron[buzzer_step].duracion_ms) {
    buzzer_step = (buzzer_step + 1) % len;
    buzzer_step_t0 = now;
    if (patron[buzzer_step].freq > 0)
      tone(PIN_BUZZER, patron[buzzer_step].freq);
    else
      noTone(PIN_BUZZER);
    buzzer_activo = patron[buzzer_step].freq > 0;
  }
}

// ===== Handler del stream MJPEG =====
#define PART_BOUNDARY "123456789000000000000987654321"
static const char *STREAM_CONTENT_TYPE =
    "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *STREAM_PART =
    "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

httpd_handle_t stream_httpd = NULL;
httpd_handle_t control_httpd = NULL;

// Handler del stream de video
static esp_err_t stream_handler(httpd_req_t *req) {
  camera_fb_t *fb = NULL;
  esp_err_t res = ESP_OK;
  char *part_buf[64];

  res = httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
  if (res != ESP_OK)
    return res;

  while (true) {
    fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Error capturando frame");
      res = ESP_FAIL;
      break;
    }

    // Boundary
    res = httpd_resp_send_chunk(req, STREAM_BOUNDARY, strlen(STREAM_BOUNDARY));
    if (res != ESP_OK)
      break;

    // Header del frame
    size_t hlen = snprintf((char *)part_buf, 64, STREAM_PART, fb->len);
    res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
    if (res != ESP_OK)
      break;

    // Imagen JPEG
    res = httpd_resp_send_chunk(req, (const char *)fb->buf, fb->len);
    esp_camera_fb_return(fb);
    fb = NULL;
    if (res != ESP_OK)
      break;
  }

  if (fb)
    esp_camera_fb_return(fb);
  return res;
}

// Handler de /alarma (llamado por deteccion_esp32cam.py cuando detecta ojos
// cerrados)
static esp_err_t alarma_handler(httpd_req_t *req) {
  drowsy_flag = true;
  Serial.println(">>> Somnolencia detectada (Python)");
  httpd_resp_set_type(req, "text/plain");
  return httpd_resp_send(req, "ALARMA ON", 9);
}

// Handler de /ok
static esp_err_t ok_handler(httpd_req_t *req) {
  drowsy_flag = false;
  Serial.println(">>> Somnolencia OK (Python)");
  httpd_resp_set_type(req, "text/plain");
  return httpd_resp_send(req, "OK", 2);
}

// Handler de /estado (compatibilidad con versiones previas)
static esp_err_t estado_handler(httpd_req_t *req) {
  const char *estado = buzzer_activo ? "ON" : "OFF";
  httpd_resp_set_type(req, "text/plain");
  return httpd_resp_send(req, estado, strlen(estado));
}

// Handler de /status -> JSON completo para el dashboard
static esp_err_t status_handler(httpd_req_t *req) {
  char buf[420];
  int len = snprintf(
      buf, sizeof(buf),
      "{"
      "\"system_on\":%s,"
      "\"buzzer_on\":%s,"
      "\"led_red\":%s,"
      "\"led_green\":%s,"
      "\"fan_on\":%s,"
      "\"drowsy_alert\":%s,"
      "\"alcohol_raw\":%d,"
      "\"alcohol_alert\":%s,"
      "\"air_raw\":%d,"
      "\"air_alert\":%s,"
      "\"mq3_test_active\":%s,"
      "\"uptime_ms\":%lu,"
      "\"wifi_rssi\":%d"
      "}",
      system_on ? "true" : "false", buzzer_activo ? "true" : "false",
      led_rojo_on ? "true" : "false", led_verde_on ? "true" : "false",
      fan_on ? "true" : "false", drowsy_flag ? "true" : "false", alcohol_raw,
      alcohol_alert ? "true" : "false", air_raw, air_alert ? "true" : "false",
      mq3_test_active ? "true" : "false", (unsigned long)millis(), WiFi.RSSI());
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, buf, len);
}

// Handler de /test-mq3 -> dispara desde el dashboard la misma ventana que el
// boton fisico
static esp_err_t test_mq3_handler(httpd_req_t *req) {
  if (system_on && !mq3_test_active) {
    mq3_test_active = true;
    mq3_test_start = millis();
    Serial.println(">>> Ventana de prueba MQ-3 iniciada (remota)");
  }
  httpd_resp_set_type(req, "text/plain");
  return httpd_resp_send(req, "OK", 2);
}

// ===== Iniciar servidor de stream (puerto 81) =====
void start_stream_server() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 81;

  httpd_uri_t stream_uri = {.uri = "/",
                            .method = HTTP_GET,
                            .handler = stream_handler,
                            .user_ctx = NULL};

  if (httpd_start(&stream_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(stream_httpd, &stream_uri);
    Serial.println("Stream en puerto 81");
  }
}

// ===== Iniciar servidor de control (puerto 82) =====
void start_control_server() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 82;
  config.max_uri_handlers = 8;

  httpd_uri_t alarma_uri = {.uri = "/alarma",
                            .method = HTTP_GET,
                            .handler = alarma_handler,
                            .user_ctx = NULL};
  httpd_uri_t ok_uri = {.uri = "/ok",
                        .method = HTTP_GET,
                        .handler = ok_handler,
                        .user_ctx = NULL};
  httpd_uri_t estado_uri = {.uri = "/estado",
                            .method = HTTP_GET,
                            .handler = estado_handler,
                            .user_ctx = NULL};
  httpd_uri_t status_uri = {.uri = "/status",
                            .method = HTTP_GET,
                            .handler = status_handler,
                            .user_ctx = NULL};
  httpd_uri_t test_mq3_uri = {.uri = "/test-mq3",
                              .method = HTTP_POST,
                              .handler = test_mq3_handler,
                              .user_ctx = NULL};

  if (httpd_start(&control_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(control_httpd, &alarma_uri);
    httpd_register_uri_handler(control_httpd, &ok_uri);
    httpd_register_uri_handler(control_httpd, &estado_uri);
    httpd_register_uri_handler(control_httpd, &status_uri);
    httpd_register_uri_handler(control_httpd, &test_mq3_uri);
    Serial.println("Control en puerto 82");
  }
}

// ============================================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== AlertDrive ESP32-CAM ===");

  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_FAN_BASE, OUTPUT);
  pinMode(PIN_LED_ROJO, OUTPUT);
  pinMode(PIN_LED_VERDE, OUTPUT);
  pinMode(PIN_BTN_POWER, INPUT);
  pinMode(PIN_BTN_MQ3, INPUT);
  digitalWrite(PIN_BUZZER, LOW);
  digitalWrite(PIN_FAN_BASE, LOW);
  digitalWrite(PIN_LED_ROJO, LOW);
  digitalWrite(PIN_LED_VERDE, LOW);

  // ADS1115 (MQ-3 en A0, MQ-135 en A1)
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  if (ads.begin(ADS1115_ADDR)) {
    ads_disponible = true;
    Serial.println("ADS1115 OK");
  } else {
    Serial.println(
        "ADS1115 no encontrado - sensores de alcohol/aire deshabilitados");
  }

  // Inicializar camara
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_QVGA; // 320x240, buen balance velocidad/calidad
  config.jpeg_quality = 12;
  config.fb_count = 2;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.grab_mode = CAMERA_GRAB_LATEST;

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
  Serial.println("\nWiFi OK");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  // Servidores
  start_stream_server();
  start_control_server();

  Serial.println("\n=== Sistema listo ===");
  Serial.printf("Stream:  http://%s:81/\n", WiFi.localIP().toString().c_str());
  Serial.printf("Status:  http://%s:82/status\n",
                WiFi.localIP().toString().c_str());

  // 2 beeps de confirmacion (tone(), no digitalWrite: el buzzer es pasivo y
  // necesita PWM para sonar; bloquear aca es inofensivo, todavia no arrancaron
  // los servidores HTTP)
  for (int i = 0; i < 2; i++) {
    tone(PIN_BUZZER, 1500);
    delay(150);
    noTone(PIN_BUZZER);
    delay(150);
  }
}

void loop() {
  unsigned long now = millis();

  // --- Boton de encendido general (toggle por software, con debounce) ---
  int btn_power = digitalRead(PIN_BTN_POWER);
  if (btn_power == HIGH && last_btn_power_state == LOW &&
      (now - last_btn_power_change) > DEBOUNCE_MS) {
    system_on = !system_on;
    last_btn_power_change = now;
    Serial.printf(">>> Sistema %s\n", system_on ? "ENCENDIDO" : "APAGADO");
    if (!system_on) {
      drowsy_flag = false;
      mq3_test_active = false;
      alcohol_alert = false;
      air_alert = false;
    }
  }
  last_btn_power_state = btn_power;

  // --- Boton de prueba de alcoholemia ---
  int btn_mq3 = digitalRead(PIN_BTN_MQ3);
  if (system_on && btn_mq3 == HIGH && last_btn_mq3_state == LOW &&
      (now - last_btn_mq3_change) > DEBOUNCE_MS && !mq3_test_active) {
    mq3_test_active = true;
    mq3_test_start = now;
    last_btn_mq3_change = now;
    Serial.println(">>> Ventana de prueba MQ-3 iniciada (boton fisico)");
  }
  last_btn_mq3_state = btn_mq3;

  // --- Ventana de prueba MQ-3 / fan ---
  if (mq3_test_active && (now - mq3_test_start >= VENTANA_MQ3_MS)) {
    mq3_test_active = false;
  }
  fan_on = system_on && mq3_test_active;
  digitalWrite(PIN_FAN_BASE, fan_on ? HIGH : LOW);

  // --- Lectura de sensores (cada INTERVALO_SENSOR_MS) ---
  if (system_on && ads_disponible &&
      (now - last_sensor_read >= INTERVALO_SENSOR_MS)) {
    last_sensor_read = now;

    if (mq3_test_active) {
      alcohol_raw = ads.readADC_SingleEnded(CH_MQ3);
      alcohol_alert = alcohol_raw >= UMBRAL_ALCOHOL;
    } else {
      alcohol_alert = false; // el MQ-3 solo se lee durante la ventana de prueba
    }

    air_raw = ads.readADC_SingleEnded(CH_MQ135);
    air_alert = air_raw >= UMBRAL_AIRE;
  }

  // --- LED verde: fijo mientras el sistema esta encendido ---
  led_verde_on = system_on;
  digitalWrite(PIN_LED_VERDE, led_verde_on ? HIGH : LOW);

  // --- Riesgo combinado (somnolencia | alcohol | aire) -> LED rojo parpadeante
  // + buzzer ---
  bool riesgo = system_on && (drowsy_flag || alcohol_alert || air_alert);
  if (riesgo) {
    if (now - last_blink_toggle >= PARPADEO_MS) {
      last_blink_toggle = now;
      led_rojo_on = !led_rojo_on;
    }
  } else {
    led_rojo_on = false;
  }
  digitalWrite(PIN_LED_ROJO, led_rojo_on ? HIGH : LOW);

  // buzzer_activo se actualiza adentro de actualizar_buzzer() (refleja si el
  // paso actual del patron esta sonando o en un silencio intermedio)
  actualizar_buzzer(now);
}
