/*
 * AlertDrive_ESP32.ino
 *
 * Firmware para el ESP32 (NO ESP32-CAM).
 * Responsabilidad: sensores, actuadores y servidor de control HTTP.
 *
 * Componentes conectados:
 *   - Buzzer pasivo                   -> GPIO 2  (tone/noTone)
 *   - Ventilador via TIP142           -> GPIO 3
 *   - Boton prueba MQ-3               -> GPIO 4  (pull-down externo 10k)
 *   - LED rojo                        -> GPIO 12
 *   - LED verde                       -> GPIO 13
 *   - I2C SCL (ADS1115)               -> GPIO 14
 *   - I2C SDA (ADS1115)               -> GPIO 15
 *   - Boton encendido general         -> GPIO 16 (pull-down externo 10k)
 *   - ADS1115 @ 0x48
 *       A0 -> MQ-135 (calidad de aire)
 *       A1 -> MQ-3   (alcohol)
 *
 * Servidor HTTP (puerto 80):
 *   GET  /alarma    -> activa flag de somnolencia (llamado por deteccion_esp32cam.py)
 *   GET  /ok        -> limpia flag de somnolencia
 *   GET  /estado    -> "ON" / "OFF" (compatibilidad)
 *   GET  /status    -> JSON completo del sistema (consumido por el backend Node.js)
 *   POST /test-mq3  -> inicia ventana de prueba de alcohol (llamado desde el dashboard)
 *
 * El buzzer y el LED rojo se activan ante CUALQUIERA de estas 3 condiciones:
 *   somnolencia (flag desde Python), alcohol sobre umbral, o aire sobre umbral.
 * Prioridad: somnolencia > alcohol > aire.
 *
 * NOTA: La IP de ESTE ESP32 debe configurarse en:
 *   - deteccion_esp32cam.py  (variable ESP32_IP -> para /alarma y /ok)
 *   - backend/.env           (variable ESP32_CONTROL_URL -> para /status)
 */

#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include "esp_http_server.h"

// ===== CONFIGURACION WIFI =====
const char* ssid     = "Will";
const char* password = "willman123";

// ===== Pines =====
#define PIN_BUZZER     2
#define PIN_FAN_BASE   3   // base del TIP142, via R 220ohm
#define PIN_BTN_MQ3    4   // boton prueba MQ-3 (pull-down externo 10k)
#define PIN_LED_ROJO   12
#define PIN_LED_VERDE  13
#define PIN_I2C_SCL    14
#define PIN_I2C_SDA    15
#define PIN_BTN_POWER  16  // boton encendido general (pull-down externo 10k)

// ===== ADS1115 =====
#define ADS1115_ADDR 0x48
#define CH_MQ3       1    // A1
#define CH_MQ135     0    // A0

// ===== Umbrales y tiempos =====
#define UMBRAL_ALCOHOL    9000  // lectura cruda ADS1115 (16 bit, ganancia default)
#define UMBRAL_AIRE       9000
#define VENTANA_MQ3_MS    8000  // duracion de la ventana de prueba del MQ-3
#define PARPADEO_MS        400  // periodo de parpadeo del LED rojo en riesgo
#define DEBOUNCE_MS        250
#define INTERVALO_SENSOR_MS 500

Adafruit_ADS1115 ads;
bool ads_disponible = false;

// ===== Estado del sistema =====
volatile bool drowsy_flag   = false;
bool system_on       = false;
bool led_verde_on    = false;
bool led_rojo_on     = false;
bool fan_on          = false;
bool buzzer_activo   = false;
bool alcohol_alert   = false;
bool air_alert       = false;
bool mq3_test_active = false;

int16_t alcohol_raw = 0;
int16_t air_raw     = 0;

unsigned long mq3_test_start       = 0;
unsigned long last_blink_toggle    = 0;
unsigned long last_sensor_read     = 0;
unsigned long last_btn_power_change = 0;
unsigned long last_btn_mq3_change  = 0;
int last_btn_power_state = LOW;
int last_btn_mq3_state   = LOW;

// ===== Melodias del buzzer (no bloqueantes, sin delay()) =====
struct BuzzerStep {
  int           freq;
  unsigned long duracion_ms;
};

// Patron de somnolencia: doble pip rapido
const BuzzerStep PATRON_SOMNOLENCIA[] = {
  {1600, 180}, {0, 100}, {1600, 180}, {0, 300}
};
// Patron de alcohol: pip grave lento
const BuzzerStep PATRON_ALCOHOL[] = {
  {900, 200}, {0, 200}, {900, 200}, {0, 800}
};
// Patron de aire: pip muy grave, espaciado
const BuzzerStep PATRON_AIRE[] = {
  {800, 200}, {0, 350}, {800, 200}, {0, 1000}
};

enum TipoAlerta {
  ALERTA_NINGUNA,
  ALERTA_SOMNOLENCIA,
  ALERTA_ALCOHOL,
  ALERTA_AIRE
};

TipoAlerta    alerta_buzzer_actual = ALERTA_NINGUNA;
int           buzzer_step          = 0;
unsigned long buzzer_step_t0       = 0;

const BuzzerStep* patron_para_tipo(TipoAlerta tipo, int* len) {
  switch (tipo) {
    case ALERTA_SOMNOLENCIA: *len = 4; return PATRON_SOMNOLENCIA;
    case ALERTA_ALCOHOL:     *len = 4; return PATRON_ALCOHOL;
    case ALERTA_AIRE:        *len = 4; return PATRON_AIRE;
    default:                 *len = 0; return nullptr;
  }
}

void actualizar_buzzer(unsigned long now) {
  TipoAlerta tipo = ALERTA_NINGUNA;
  if      (drowsy_flag)   tipo = ALERTA_SOMNOLENCIA;
  else if (alcohol_alert) tipo = ALERTA_ALCOHOL;
  else if (air_alert)     tipo = ALERTA_AIRE;

  if (tipo != alerta_buzzer_actual) {
    alerta_buzzer_actual = tipo;
    buzzer_step   = 0;
    buzzer_step_t0 = now;

    if (tipo == ALERTA_NINGUNA) {
      noTone(PIN_BUZZER);
      buzzer_activo = false;
      return;
    }

    int len;
    const BuzzerStep* patron = patron_para_tipo(tipo, &len);
    if (patron[0].freq > 0) tone(PIN_BUZZER, patron[0].freq);
    else                    noTone(PIN_BUZZER);
    buzzer_activo = patron[0].freq > 0;
    return;
  }

  if (tipo == ALERTA_NINGUNA) return;

  int len;
  const BuzzerStep* patron = patron_para_tipo(tipo, &len);
  if (now - buzzer_step_t0 >= patron[buzzer_step].duracion_ms) {
    buzzer_step    = (buzzer_step + 1) % len;
    buzzer_step_t0 = now;
    if (patron[buzzer_step].freq > 0) tone(PIN_BUZZER, patron[buzzer_step].freq);
    else                              noTone(PIN_BUZZER);
    buzzer_activo = patron[buzzer_step].freq > 0;
  }
}

// ===== Handlers HTTP =====
httpd_handle_t control_httpd = NULL;

// GET /alarma -> activa somnolencia (llamado por deteccion_esp32cam.py)
static esp_err_t alarma_handler(httpd_req_t* req) {
  drowsy_flag = true;
  Serial.println(">>> Somnolencia detectada (Python)");
  httpd_resp_set_type(req, "text/plain");
  return httpd_resp_send(req, "ALARMA ON", 9);
}

// GET /ok -> limpia somnolencia
static esp_err_t ok_handler(httpd_req_t* req) {
  drowsy_flag = false;
  Serial.println(">>> Somnolencia OK (Python)");
  httpd_resp_set_type(req, "text/plain");
  return httpd_resp_send(req, "OK", 2);
}

// GET /estado -> "ON" / "OFF" (compatibilidad con versiones previas)
static esp_err_t estado_handler(httpd_req_t* req) {
  const char* estado = buzzer_activo ? "ON" : "OFF";
  httpd_resp_set_type(req, "text/plain");
  return httpd_resp_send(req, estado, strlen(estado));
}

// GET /status -> JSON completo (consumido por el backend Node.js)
static esp_err_t status_handler(httpd_req_t* req) {
  char buf[420];
  int  len = snprintf(buf, sizeof(buf),
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
      system_on       ? "true" : "false",
      buzzer_activo   ? "true" : "false",
      led_rojo_on     ? "true" : "false",
      led_verde_on    ? "true" : "false",
      fan_on          ? "true" : "false",
      drowsy_flag     ? "true" : "false",
      alcohol_raw,
      alcohol_alert   ? "true" : "false",
      air_raw,
      air_alert       ? "true" : "false",
      mq3_test_active ? "true" : "false",
      (unsigned long)millis(),
      WiFi.RSSI());
  httpd_resp_set_type(req, "application/json");
  return httpd_resp_send(req, buf, len);
}

// POST /test-mq3 -> inicia ventana de prueba de alcohol (desde el dashboard)
static esp_err_t test_mq3_handler(httpd_req_t* req) {
  if (system_on && !mq3_test_active) {
    mq3_test_active = true;
    mq3_test_start  = millis();
    Serial.println(">>> Ventana de prueba MQ-3 iniciada (remota)");
  }
  httpd_resp_set_type(req, "text/plain");
  return httpd_resp_send(req, "OK", 2);
}

// ===== Servidor de control (puerto 80) =====
void start_control_server() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port      = 80;
  config.max_uri_handlers = 8;

  httpd_uri_t alarma_uri  = { .uri = "/alarma",   .method = HTTP_GET,  .handler = alarma_handler,  .user_ctx = NULL };
  httpd_uri_t ok_uri      = { .uri = "/ok",       .method = HTTP_GET,  .handler = ok_handler,      .user_ctx = NULL };
  httpd_uri_t estado_uri  = { .uri = "/estado",   .method = HTTP_GET,  .handler = estado_handler,  .user_ctx = NULL };
  httpd_uri_t status_uri  = { .uri = "/status",   .method = HTTP_GET,  .handler = status_handler,  .user_ctx = NULL };
  httpd_uri_t test_mq3_uri = { .uri = "/test-mq3", .method = HTTP_POST, .handler = test_mq3_handler, .user_ctx = NULL };

  if (httpd_start(&control_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(control_httpd, &alarma_uri);
    httpd_register_uri_handler(control_httpd, &ok_uri);
    httpd_register_uri_handler(control_httpd, &estado_uri);
    httpd_register_uri_handler(control_httpd, &status_uri);
    httpd_register_uri_handler(control_httpd, &test_mq3_uri);
    Serial.printf("Control en http://%s/\n", WiFi.localIP().toString().c_str());
  }
}

// ============================================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n=== AlertDrive ESP32 ===");

  pinMode(PIN_BUZZER,    OUTPUT);
  pinMode(PIN_FAN_BASE,  OUTPUT);
  pinMode(PIN_LED_ROJO,  OUTPUT);
  pinMode(PIN_LED_VERDE, OUTPUT);
  pinMode(PIN_BTN_POWER, INPUT);
  pinMode(PIN_BTN_MQ3,   INPUT);
  digitalWrite(PIN_BUZZER,    LOW);
  digitalWrite(PIN_FAN_BASE,  LOW);
  digitalWrite(PIN_LED_ROJO,  LOW);
  digitalWrite(PIN_LED_VERDE, LOW);

  // ADS1115
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  if (ads.begin(ADS1115_ADDR)) {
    ads_disponible = true;
    Serial.println("ADS1115 OK");
  } else {
    Serial.println("ADS1115 no encontrado - sensores de alcohol/aire deshabilitados");
  }

  // WiFi
  WiFi.begin(ssid, password);
  Serial.print("Conectando WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\nWiFi OK - IP: %s\n", WiFi.localIP().toString().c_str());

  // Servidor HTTP
  start_control_server();

  Serial.println("\n=== ESP32 listo ===");
  Serial.printf("Alarma:   http://%s/alarma\n",   WiFi.localIP().toString().c_str());
  Serial.printf("Apagar:   http://%s/ok\n",        WiFi.localIP().toString().c_str());
  Serial.printf("Status:   http://%s/status\n",    WiFi.localIP().toString().c_str());
  Serial.printf("Test MQ3: http://%s/test-mq3\n",  WiFi.localIP().toString().c_str());

  // 2 beeps de confirmacion
  for (int i = 0; i < 2; i++) {
    tone(PIN_BUZZER, 1500);
    delay(150);
    noTone(PIN_BUZZER);
    delay(150);
  }
}

void loop() {
  unsigned long now = millis();

  // --- Boton de encendido general (toggle con debounce) ---
  int btn_power = digitalRead(PIN_BTN_POWER);
  if (btn_power == HIGH && last_btn_power_state == LOW &&
      (now - last_btn_power_change) > DEBOUNCE_MS) {
    system_on = !system_on;
    last_btn_power_change = now;
    Serial.printf(">>> Sistema %s\n", system_on ? "ENCENDIDO" : "APAGADO");
    if (!system_on) {
      drowsy_flag     = false;
      mq3_test_active = false;
      alcohol_alert   = false;
      air_alert       = false;
    }
  }
  last_btn_power_state = btn_power;

  // --- Boton de prueba de alcoholemia ---
  int btn_mq3 = digitalRead(PIN_BTN_MQ3);
  if (system_on && btn_mq3 == HIGH && last_btn_mq3_state == LOW &&
      (now - last_btn_mq3_change) > DEBOUNCE_MS && !mq3_test_active) {
    mq3_test_active = true;
    mq3_test_start  = now;
    last_btn_mq3_change = now;
    Serial.println(">>> Ventana de prueba MQ-3 iniciada (boton fisico)");
  }
  last_btn_mq3_state = btn_mq3;

  // --- Ventana de prueba MQ-3 / ventilador ---
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
      alcohol_raw   = ads.readADC_SingleEnded(CH_MQ3);
      alcohol_alert = alcohol_raw >= UMBRAL_ALCOHOL;
    } else {
      alcohol_alert = false;
    }

    air_raw   = ads.readADC_SingleEnded(CH_MQ135);
    air_alert = air_raw >= UMBRAL_AIRE;
  }

  // --- LED verde: encendido mientras el sistema esta activo ---
  led_verde_on = system_on;
  digitalWrite(PIN_LED_VERDE, led_verde_on ? HIGH : LOW);

  // --- Riesgo combinado -> LED rojo parpadeante + buzzer ---
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

  // buzzer_activo se actualiza dentro de actualizar_buzzer()
  actualizar_buzzer(now);
}
