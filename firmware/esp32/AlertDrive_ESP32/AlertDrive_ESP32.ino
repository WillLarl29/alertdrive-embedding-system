#include <Adafruit_ADS1X15.h>
#include <WebServer.h>
#include <WiFi.h>
#include <Wire.h>

// ===== WiFi =====
const char *ssid = "Will";
const char *password = "willman123";

// ===== Pines Unificados =====
const int PIN_BTN_SISTEMA = 18;
const int PIN_BTN_MQ3 = 19;
const int PIN_LED_VERDE = 32;
const int PIN_LED_ROJO = 25;
const int PIN_BUZZER = 33;
const int PIN_FAN = 23;

const int PIN_SDA = 21;
const int PIN_SCL = 22;

// ===== Umbrales de Prueba en escala ADC (Configuración Recomendada) =====
const int16_t UMBRAL_MQ135_AIRE = 3000;  // Límite en escala ADC pura
const int16_t UMBRAL_MQ3_ALCOHOL = 1500; // Límite en escala ADC pura

// Factor de conversión física (Ganancia 1x estándar = 0.125 mV por unidad)
const float ADS_FACTOR_MV = 0.1250;

// ===== Estados =====
int estadoSistema = 0;
int estadoBtnSistema = 1;
int estadoBtnMQ3 = 1;

unsigned long ultimoTiempoLectura = 0;
const unsigned long intervaloLectura = 500;

unsigned long ultimoTiempoLedRojo = 0;
bool estadoLedRojoAlarma = false;

// ===== Instancias =====
WebServer server(80);
Adafruit_ADS1115 ads;

// ===== FUNCIONES DE SONIDOS DEDICADOS =====

void buzzerMicrosueño() {
  Serial.println("    [Buzzer] Alerta: Microsueño Detectado (Señal de Python)");
  for (int c = 0; c < 6; c++) {
    tone(PIN_BUZZER, 1600);
    delay(180);
    noTone(PIN_BUZZER);
    delay(100);
    tone(PIN_BUZZER, 1600);
    delay(180);
    noTone(PIN_BUZZER);
    delay(300);
  }
}

void buzzerAlcohol() {
  Serial.println("    [Buzzer] Alerta: Alto Nivel de Alcohol");
  for (int c = 0; c < 2; c++) {
    tone(PIN_BUZZER, 900);
    delay(200);
    noTone(PIN_BUZZER);
    delay(200);
    tone(PIN_BUZZER, 900);
    delay(200);
    noTone(PIN_BUZZER);
    delay(800);
  }
}

void buzzerCalidadAire() {
  Serial.println("    [Buzzer] Alerta: Calidad de Aire Degradada");
  for (int c = 0; c < 2; c++) {
    tone(PIN_BUZZER, 800);
    delay(200);
    noTone(PIN_BUZZER);
    delay(350);
    tone(PIN_BUZZER, 800);
    delay(200);
    noTone(PIN_BUZZER);
    delay(1000);
  }
}

// ===== Handlers HTTP (Reciben peticiones del Notebook) =====

void handleAlarma() {
  if (estadoSistema == 1) {
    server.send(200, "text/plain", "ALERTA MICROSUEÑO ACTIVA");
    // Al recibir /alarma desde la celda de Python, ejecuta los tonos de
    // microsueño
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

void handleRoot() { server.send(200, "text/plain", "ESP32 AlertDrive OK\n"); }

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== ESP32 AlertDrive (Listo para Notebook Python) ===");

  pinMode(PIN_BTN_SISTEMA, INPUT);
  pinMode(PIN_BTN_MQ3, INPUT);
  pinMode(PIN_LED_VERDE, OUTPUT);
  pinMode(PIN_LED_ROJO, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_FAN, OUTPUT);

  digitalWrite(PIN_LED_VERDE, LOW);
  digitalWrite(PIN_LED_ROJO, LOW);
  digitalWrite(PIN_FAN, LOW);
  noTone(PIN_BUZZER);

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
  Serial.print("IP del ESP32: ");
  Serial.println(WiFi.localIP());

  // Endpoints HTTP mapeados para las celdas 4 y 5 del archivo .ipynb
  server.on("/", handleRoot);
  server.on("/alarma", handleAlarma); // Dispara secuencia de microsueño
  server.on("/ok", handleOk);         // Detiene alarmas
  server.begin();
  Serial.println("Servidor HTTP en puerto 80 activo.");
  Serial.println("Sistema APAGADO. Presiona boton general para iniciar.");
}

void loop() {
  server.handleClient();

  int btnSistema = digitalRead(PIN_BTN_SISTEMA);
  int btnMQ3 = digitalRead(PIN_BTN_MQ3);

  // ── BOTON GENERAL ──
  if (estadoBtnSistema == 1 && btnSistema == 0) {
    if (estadoSistema == 0) {
      estadoSistema = 1;
      digitalWrite(PIN_LED_VERDE, HIGH);
      digitalWrite(PIN_LED_ROJO, LOW);
      digitalWrite(PIN_FAN, LOW);
      noTone(PIN_BUZZER);
      Serial.println(">>> Sistema ENCENDIDO — LED verde ON");
    } else {
      estadoSistema = 0;
      digitalWrite(PIN_LED_VERDE, LOW);
      digitalWrite(PIN_LED_ROJO, LOW);
      digitalWrite(PIN_FAN, LOW);
      noTone(PIN_BUZZER);
      Serial.println(">>> Sistema APAGADO — todo OFF");
    }
    delay(250);
  }
  estadoBtnSistema = btnSistema;

  // ── BOTON MQ3 — Ráfaga de 7 segundos con datos brutos para comparar ──
  if (estadoBtnMQ3 == 1 && btnMQ3 == 0) {
    if (estadoSistema == 1) {
      Serial.println(">>> Iniciando ventana de RÁFAGA MQ-3 (7 segundos)...");

      digitalWrite(PIN_LED_ROJO, HIGH);
      digitalWrite(PIN_FAN, HIGH);

      unsigned long inicioVentana = millis();
      unsigned long ultimoSegundo = 0;
      bool alcoholDetectado = false;

      while (millis() - inicioVentana < 7000) {
        int16_t adc_mq135 = ads.readADC_SingleEnded(0); // A0 -> Aire
        int16_t adc_mq3 = ads.readADC_SingleEnded(1);   // A1 -> Alcohol

        float mv_mq135 = adc_mq135 * ADS_FACTOR_MV;
        float mv_mq3 = adc_mq3 * ADS_FACTOR_MV;

        unsigned long transcurrido = (millis() - inicioVentana) / 1000;
        if (transcurrido != ultimoSegundo) {
          Serial.printf("    [Tiempo: %d s / 7 s]\n", transcurrido + 1);
          ultimoSegundo = transcurrido;
        }

        // Imprime valor bruto (escala del límite) junto a los milivoltios
        // físicos
        Serial.printf("[RÁFAGA] -> MQ-135: %d (%0.1fmV) | MQ-3 (Límite %d): %d "
                      "(%0.1fmV)\n",
                      adc_mq135, mv_mq135, UMBRAL_MQ3_ALCOHOL, adc_mq3, mv_mq3);

        if (adc_mq3 > UMBRAL_MQ3_ALCOHOL) {
          alcoholDetectado = true;
        }
        delay(250);
      }

      digitalWrite(PIN_FAN, LOW);
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

  // ── MONITOR CONTINUO DE AIRE (Muestra el valor bruto para comparar con tu
  // límite) ──
  if (estadoSistema == 1) {

    if (millis() - ultimoTiempoLectura >= intervaloLectura) {
      ultimoTiempoLectura = millis();

      int16_t lectura_MQ135 =
          ads.readADC_SingleEnded(0); // A0 -> Calidad de Aire
      float mv_continuo_aire = lectura_MQ135 * ADS_FACTOR_MV;

      // Imprime tanto las unidades del límite como los milivoltios
      Serial.printf("MONITOR SENSORES -> MQ-135 (Límite %d): %d unidades | "
                    "Tensión: %0.1f mV\n",
                    UMBRAL_MQ135_AIRE, lectura_MQ135, mv_continuo_aire);

      if (lectura_MQ135 > UMBRAL_MQ135_AIRE) {
        buzzerCalidadAire();
      }
    }

    // Control de parpadeo del LED Rojo basado en el umbral analógico
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
