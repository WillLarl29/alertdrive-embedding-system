#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>

// ===== WiFi =====
const char* ssid     = "Will";
const char* password = "willman123";

// ===== Pines Unificados =====
const int PIN_BTN_SISTEMA = 18; 
const int PIN_BTN_MQ3     = 19; 
const int PIN_LED_VERDE   = 32; 
const int PIN_LED_ROJO    = 25; 
const int PIN_BUZZER      = 33; 
const int PIN_FAN         = 23; 

const int PIN_SDA         = 21; 
const int PIN_SCL         = 22; 

// ===== Umbrales de Prueba en escala ADC =====
const int16_t UMBRAL_MQ135_AIRE  = 3500; 
const int16_t UMBRAL_MQ3_ALCOHOL = 2000; 

// Factor de conversión física (Ganancia 1x estándar = 0.125 mV por unidad)
const float ADS_FACTOR_MV = 0.1250; 

// ===== Estados =====
int estadoSistema    = 0;
int estadoBtnSistema = 1;
int estadoBtnMQ3     = 1;

unsigned long ultimoTiempoLectura = 0;
const unsigned long intervaloLectura = 500; 

unsigned long ultimoTiempoLedRojo = 0;
bool estadoLedRojoAlarma = false;

// Ultimas lecturas y flags, cacheados para que /status responda al instante
// sin tener que leer el ADS1115 dentro del handler HTTP.
int16_t ultimaLecturaMQ135 = 0;
int16_t ultimaLecturaMQ3   = 0;
bool mq3TestActivo         = false;
bool estadoBuzzerActivo    = false;

// ===== Instancias =====
WebServer server(80);
Adafruit_ADS1115 ads; 

// ===== FUNCIONES DE SONIDOS DEDICADOS =====

void buzzerMicrosueño() {
  Serial.println("    [Buzzer] Alerta: Microsueño Detectado (Señal de Python)");
  estadoBuzzerActivo = true;
  for (int c = 0; c < 6; c++) {
    tone(PIN_BUZZER, 1600); delay(180);
    noTone(PIN_BUZZER);     delay(100);
    tone(PIN_BUZZER, 1600); delay(180);
    noTone(PIN_BUZZER);     delay(300);
  }
  estadoBuzzerActivo = false;
}

void buzzerAlcohol() {
  Serial.println("    [Buzzer] Alerta: Alto Nivel de Alcohol");
  estadoBuzzerActivo = true;
  for (int c = 0; c < 2; c++) {
    tone(PIN_BUZZER, 900); delay(200);
    noTone(PIN_BUZZER);    delay(200);
    tone(PIN_BUZZER, 900); delay(200);
    noTone(PIN_BUZZER);    delay(800);
  }
  estadoBuzzerActivo = false;
}

void buzzerCalidadAire() {
  Serial.println("    [Buzzer] Alerta: Calidad de Aire Degradada");
  estadoBuzzerActivo = true;
  for (int c = 0; c < 2; c++) {
    tone(PIN_BUZZER, 800); delay(200);
    noTone(PIN_BUZZER);    delay(350);
    tone(PIN_BUZZER, 800); delay(200);
    noTone(PIN_BUZZER);    delay(1000);
  }
  estadoBuzzerActivo = false;
}

// ===== Handlers HTTP =====

void handleAlarma() {
  if (estadoSistema == 1) {
    server.send(200, "text/plain", "ALERTA MICROSUEÑO ACTIVA");
    buzzerMicrosueño(); 
  } else {
    server.send(200, "text/plain", "SISTEMA APAGADO");
  }
}

void handleOk() {
  noTone(PIN_BUZZER);
  digitalWrite(PIN_LED_ROJO, LOW);
  Serial.println(">>> Alertas reseteadas por Python");
  server.send(200, "text/plain", "OK");
}

void handleRoot() {
  server.send(200, "text/plain", "ESP32 AlertDrive OK\n");
}

// Consultado por el backend (Node) cada POLL_INTERVAL_MS para reflejar el
// estado del dispositivo en el dashboard. Usa solo valores ya cacheados en
// variables globales, nunca lee sensores aqui, para responder al instante.
void handleStatus() {
  bool airAlert = ultimaLecturaMQ135 > UMBRAL_MQ135_AIRE;
  bool alcoholAlert = ultimaLecturaMQ3 > UMBRAL_MQ3_ALCOHOL;

  char buf[400];
  snprintf(buf, sizeof(buf),
    "{"
    "\"system_on\":%s,"
    "\"buzzer_on\":%s,"
    "\"led_red\":%s,"
    "\"led_green\":%s,"
    "\"fan_on\":%s,"
    "\"uptime_ms\":%lu,"
    "\"wifi_rssi\":%d,"
    "\"air_raw\":%d,"
    "\"air_alert\":%s,"
    "\"alcohol_raw\":%d,"
    "\"alcohol_alert\":%s,"
    "\"mq3_test_active\":%s"
    "}",
    estadoSistema == 1 ? "true" : "false",
    estadoBuzzerActivo ? "true" : "false",
    digitalRead(PIN_LED_ROJO) ? "true" : "false",
    digitalRead(PIN_LED_VERDE) ? "true" : "false",
    digitalRead(PIN_FAN) ? "true" : "false",
    millis(),
    WiFi.RSSI(),
    ultimaLecturaMQ135,
    airAlert ? "true" : "false",
    ultimaLecturaMQ3,
    alcoholAlert ? "true" : "false",
    mq3TestActivo ? "true" : "false"
  );
  server.send(200, "application/json", buf);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== ESP32 AlertDrive (Iniciando) ===");

  // Configurar Pines
  pinMode(PIN_BTN_SISTEMA, INPUT);
  pinMode(PIN_BTN_MQ3,     INPUT);
  pinMode(PIN_LED_VERDE,   OUTPUT);
  pinMode(PIN_LED_ROJO,    OUTPUT);
  pinMode(PIN_BUZZER,      OUTPUT);
  pinMode(PIN_FAN,         OUTPUT);

  // OJO: Todo forzado a apagarse físicamente al conectar la energía
  digitalWrite(PIN_LED_VERDE, LOW);
  digitalWrite(PIN_LED_ROJO,  LOW);
  digitalWrite(PIN_FAN,       LOW); 
  noTone(PIN_BUZZER);

  // --- LÓGICA DE PRECALENTAMIENTO DE 10 SEGUNDOS ---
  Serial.print("Estabilizando y precalentando sensores químicos");
  for (int i = 0; i < 10; i++) {
    delay(1000); // Bloquea lecturas durante 10 segundos
    Serial.print(".");
  }
  Serial.println("\n[Sensores listos y estabilizados]");

  // Inicializar Comunicación I2C y ADS1115 después del calentamiento
  Wire.begin(PIN_SDA, PIN_SCL); 

  if (!ads.begin(0x48)) { 
    Serial.println("¡ERROR: ADS1115 no encontrado!");
  } else {
    Serial.println("Módulo ADS1115 OK.");
  }

  // WiFi
  Serial.printf("Conectando a %s", ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi OK");
  Serial.print("IP del ESP32: "); Serial.println(WiFi.localIP());

  server.on("/",       handleRoot);
  server.on("/alarma", handleAlarma);
  server.on("/ok",     handleOk);
  server.on("/status", handleStatus); // Consultado por el backend (dashboard)
  server.begin();
  
  Serial.println("Servidor HTTP activo.");
  Serial.println("SISTEMA GENERAL: APAGADO. Presiona el boton general para activar.");
}

void loop() {
  server.handleClient();

  int btnSistema = digitalRead(PIN_BTN_SISTEMA);
  int btnMQ3     = digitalRead(PIN_BTN_MQ3);

  // ── BOTON GENERAL ──
  if (estadoBtnSistema == 1 && btnSistema == 0) {
    if (estadoSistema == 0) {
      estadoSistema = 1;
      digitalWrite(PIN_LED_VERDE, HIGH);
      digitalWrite(PIN_LED_ROJO,  LOW);
      digitalWrite(PIN_FAN,       LOW);
      noTone(PIN_BUZZER);
      Serial.println(">>> Sistema ENCENDIDO — LED verde ON");
    } else {
      estadoSistema = 0;
      digitalWrite(PIN_LED_VERDE, LOW);
      digitalWrite(PIN_LED_ROJO,  LOW);
      digitalWrite(PIN_FAN,       LOW);
      noTone(PIN_BUZZER);
      Serial.println(">>> Sistema APAGADO — todo OFF");
    }
    delay(250); 
  }
  estadoBtnSistema = btnSistema;

  // ── BOTON MQ3 — Ventana de ráfaga de 7 segundos ──
  if (estadoBtnMQ3 == 1 && btnMQ3 == 0) {
    if (estadoSistema == 1) {
      Serial.println(">>> Iniciando ventana de RÁFAGA MQ-3 (7 segundos)...");
      
      digitalWrite(PIN_LED_ROJO, HIGH);
      digitalWrite(PIN_FAN,      HIGH);
      mq3TestActivo = true;

      unsigned long inicioVentana = millis();
      unsigned long ultimoSegundo = 0;
      bool alcoholDetectado = false;

      while (millis() - inicioVentana < 7000) {
        int16_t adc_mq135 = ads.readADC_SingleEnded(0);
        int16_t adc_mq3   = ads.readADC_SingleEnded(1);
        ultimaLecturaMQ135 = adc_mq135;
        ultimaLecturaMQ3   = adc_mq3;

        float  mv_mq135 = adc_mq135 * ADS_FACTOR_MV;
        float  mv_mq3   = adc_mq3 * ADS_FACTOR_MV;

        unsigned long transcurrido = (millis() - inicioVentana) / 1000;
        if (transcurrido != ultimoSegundo) {
          Serial.printf("    [Tiempo: %d s / 7 s]\n", transcurrido + 1);
          ultimoSegundo = transcurrido;
        }

        Serial.printf("[RÁFAGA] -> MQ-135: %d unidades | Tensión: %0.1f mV  ||  MQ-3: %d unidades | Tensión: %0.1f mV\n", 
                      adc_mq135, mv_mq135, adc_mq3, mv_mq3);
        
        if (adc_mq3 > UMBRAL_MQ3_ALCOHOL) {
          alcoholDetectado = true;
        }
        delay(250); 
      }

      mq3TestActivo = false;
      digitalWrite(PIN_FAN,      LOW);
      digitalWrite(PIN_LED_ROJO, LOW);

      Serial.println(">>> FINALIZA LOS 7 SEGUNDOS.");
      Serial.println("    Fan + LED rojo de ráfaga OFF");

      if (alcoholDetectado) {
        buzzerAlcohol();
      }
    } else {
      Serial.println(">>> Boton MQ3 ignorado — sistema apagado");
    }
    delay(250);
  }
  estadoBtnMQ3 = btnMQ3;

  // ── MONITOR CONTINUO DE AIRE (Solo funciona si el sistema general está ENCENDIDO) ──
  if (estadoSistema == 1) {
    
    if (millis() - ultimoTiempoLectura >= intervaloLectura) {
      ultimoTiempoLectura = millis();

      int16_t lectura_MQ135 = ads.readADC_SingleEnded(0);
      ultimaLecturaMQ135 = lectura_MQ135;
      float mv_continuo_aire = lectura_MQ135 * ADS_FACTOR_MV;

      Serial.printf("MONITOR SENSORES -> MQ-135: %d unidades | Tensión: %0.1f mV\n", lectura_MQ135, mv_continuo_aire);

      if (lectura_MQ135 > UMBRAL_MQ135_AIRE) {
        buzzerCalidadAire();
      }
    }

    // Control de parpadeo del LED Rojo
    if (ads.readADC_SingleEnded(0) > UMBRAL_MQ135_AIRE) {
      if (millis() - ultimoTiempoLedRojo >= 250) { 
        ultimoTiempoLedRojo = millis();
        estadoLedRojoAlarma = !estadoLedRojoAlarma;
        digitalWrite(PIN_LED_ROJO, estadoLedRojoAlarma ? HIGH : LOW);
      }
    } else {
      if (digitalRead(PIN_FAN) == LOW) {
        digitalWrite(PIN_LED_ROJO, LOW);
      }
    }
  }

  delay(10);
}