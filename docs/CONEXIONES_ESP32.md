# AlertDrive — Conexiones del ESP32 con los componentes

> Basado en el firmware vigente
> [funcional_prototipo_ultimo.ino](../firmware/esp32/funcional_prototipo_ultimo/funcional_prototipo_ultimo.ino)
> (mismo pinout que [AlertDrive_ESP32.ino](../firmware/esp32/AlertDrive_ESP32/AlertDrive_ESP32.ino)).
>
> **Cómo leer este documento:** los pines GPIO, los canales del ADS1115, la
> dirección I²C y la lógica de cada señal salen **directamente del código** (✅).
> El código no dice nada sobre la parte eléctrica que no pasa por un pin del
> ESP32: valores de resistencias, de qué riel (3,3 V o 5 V) se alimenta cada
> módulo, si hay diodos, etc. Eso se marca como **⚠️ a verificar en la placa**,
> junto con la conexión recomendada.

---

## 1. Vista general

```
                                   ┌──────────────────────────────┐
                                   │          ESP32 DevKit         │
                                   │                               │
  Botón general ──────────────────►│ GPIO 18  (entrada)            │
  Botón prueba MQ-3 ──────────────►│ GPIO 19  (entrada)            │
                                   │                               │
                                   │ GPIO 32  (salida) ────────────┼──► LED verde ──► GND
                                   │ GPIO 25  (salida) ────────────┼──► LED rojo  ──► GND
                                   │ GPIO 33  (salida, PWM tone) ──┼──► Buzzer pasivo ──► GND
                                   │ GPIO 23  (salida) ──[R base]──┼──► Base TIP142 ──► Ventilador
                                   │                               │
                                   │ GPIO 21  SDA ◄───────────────►│◄─► ADS1115 SDA
                                   │ GPIO 22  SCL ────────────────►│──► ADS1115 SCL
                                   │                               │
                                   │ 3V3 / 5V(VIN) / GND           │──► alimentación de módulos
                                   │ WiFi (STA)                    │◄─► red local (HTTP puerto 80)
                                   └──────────────────────────────┘

  ADS1115 (0x48)  A0 ◄── AO del MQ-135 (calidad de aire)
                  A1 ◄── AO del MQ-3   (alcohol)
```

La **webcam no se conecta al ESP32**: está en la laptop, y la laptop habla con
el ESP32 por WiFi (sección 9).

---

## 2. Tabla resumen de pines

| Pin ESP32 | Componente | Dirección | Configuración en código | Lógica | Estado |
|---|---|---|---|---|---|
| GPIO 18 | Botón general (ON/OFF del sistema) | Entrada | `pinMode(18, INPUT)` | Dispara en flanco 1 → 0 | ✅ |
| GPIO 19 | Botón prueba de alcoholemia | Entrada | `pinMode(19, INPUT)` | Dispara en flanco 1 → 0 | ✅ |
| GPIO 32 | LED verde | Salida | `pinMode(32, OUTPUT)` | HIGH = encendido | ✅ |
| GPIO 25 | LED rojo | Salida | `pinMode(25, OUTPUT)` | HIGH = encendido | ✅ |
| GPIO 33 | Buzzer pasivo | Salida (PWM) | `tone()` / `noTone()` | Onda cuadrada 800/900/1600 Hz | ✅ |
| GPIO 23 | Base del TIP142 (ventilador) | Salida | `pinMode(23, OUTPUT)` | HIGH = ventilador ON | ✅ |
| GPIO 21 | SDA del ADS1115 | Bidireccional | `Wire.begin(21, 22)` | I²C | ✅ |
| GPIO 22 | SCL del ADS1115 | Salida (reloj) | `Wire.begin(21, 22)` | I²C | ✅ |
| — | ADS1115 | I²C | `ads.begin(0x48)` | Dirección 0x48 | ✅ |
| ADS1115 A0 | MQ-135 | Entrada analógica | `readADC_SingleEnded(0)` | Lectura cruda 16 bits | ✅ |
| ADS1115 A1 | MQ-3 | Entrada analógica | `readADC_SingleEnded(1)` | Lectura cruda 16 bits | ✅ |

Al arrancar, el firmware pone en LOW los LEDs y el ventilador, y silencia el
buzzer, antes de cualquier otra cosa. Así ningún actuador queda encendido por
accidente durante el arranque.

---

## 3. Botones (GPIO 18 y GPIO 19)

### 3.1 Qué hace el código

```cpp
pinMode(PIN_BTN_SISTEMA, INPUT);   // GPIO 18
pinMode(PIN_BTN_MQ3,     INPUT);   // GPIO 19
int estadoBtnSistema = 1;          // estado "anterior" inicial = HIGH
...
if (estadoBtnSistema == 1 && btnSistema == 0) { ... }  // flanco 1 -> 0
delay(250);                        // antirrebote simple
```

- Se configuran como `INPUT` **sin resistencia interna** (no `INPUT_PULLUP` ni
  `INPUT_PULLDOWN`). Por eso **cada botón necesita obligatoriamente una
  resistencia externa**; sin ella el pin queda "flotando" y genera pulsaciones
  falsas.
- La acción se dispara cuando la lectura **pasa de 1 a 0**.
- Después de cada acción hay un `delay(250)` que sirve de antirrebote.

### 3.2 Conexión recomendada (coherente con el código): pull-up a 3,3 V

```
 3V3 ──[ 10 kΩ ]──┬──── GPIO 18 (o GPIO 19)
                  │
               [ BOTÓN ]
                  │
 GND ─────────────┘
```

- En reposo el pin lee **1** (lo sube la resistencia de 10 kΩ).
- Al presionar, el botón lleva el pin a GND y se lee **0**. Ese flanco 1 → 0
  ocurre **en el momento de presionar**, que es lo que espera el código (el
  estado inicial `= 1` supone que el reposo es HIGH).

⚠️ **A verificar en la placa:** los comentarios del firmware antiguo del
ESP32-CAM decían "pull-down externo 10 kΩ". Si la placa sigue cableada con
**pull-down** (resistencia a GND y botón a 3,3 V), el sistema también funciona,
pero la acción ocurre **al soltar** el botón, no al presionarlo (en ese caso el
flanco 1 → 0 es la liberación). Revisa cuál de los dos montajes tienes y
descríbelo así en el informe.

> Nunca conectar el botón a 5 V: los GPIO del ESP32 trabajan a 3,3 V y no
> toleran 5 V.

### 3.3 Función de cada botón

| Botón | Si el sistema está APAGADO | Si el sistema está ENCENDIDO |
|---|---|---|
| General (GPIO 18) | Lo enciende: LED verde ON, LED rojo/ventilador/buzzer OFF | Lo apaga: todo OFF |
| MQ-3 (GPIO 19) | Se ignora (mensaje "Boton MQ3 ignorado") | Inicia la prueba de alcohol de 7 s |

---

## 4. LEDs (GPIO 32 verde, GPIO 25 rojo)

### 4.1 Conexión

```
 GPIO 32 ──[ 220–330 Ω ]──►|── GND        (LED verde: ánodo al resistor, cátodo a GND)
 GPIO 25 ──[ 220–330 Ω ]──►|── GND        (LED rojo)
```

- Lógica **activa en alto**: `digitalWrite(pin, HIGH)` enciende el LED. Esto
  implica que el LED va del GPIO a **GND** (no a 3,3 V).
- ⚠️ **A verificar en la placa:** el valor de la resistencia en serie (el código
  no lo indica). Con 3,3 V, un LED rojo (~2 V) y 220 Ω circulan ≈ 6 mA, dentro
  del límite seguro del pin. **Sin resistencia en serie se puede dañar el GPIO y
  el LED.**

### 4.2 Comportamiento

| LED | Cuándo se enciende |
|---|---|
| Verde (GPIO 32) | Fijo mientras el sistema está ENCENDIDO |
| Rojo (GPIO 25) | Fijo durante los 7 s de la prueba de alcohol; **parpadea cada 250 ms** cuando el MQ-135 supera el umbral de aire; se apaga con `GET /ok` |

---

## 5. Buzzer pasivo (GPIO 33)

### 5.1 Conexión

```
 Opción A (buzzer pequeño, consumo bajo):
   GPIO 33 ──[ 100 Ω ]── (+) BUZZER (−) ── GND

 Opción B (recomendada, más volumen y protege el pin):
   GPIO 33 ──[ 1 kΩ ]── Base NPN (2N2222 / S8050)
                        Colector ── (−) BUZZER (+) ── 5 V
                        Emisor  ── GND
```

- Se usa `tone(PIN_BUZZER, frecuencia)`, que genera una **onda cuadrada (PWM)**.
  Por eso el buzzer debe ser **pasivo** (sin oscilador interno). Un buzzer
  activo sonaría con un tono fijo e ignoraría la frecuencia.
- ⚠️ **A verificar en la placa:** si el buzzer va directo al pin (opción A) o con
  transistor (opción B). El código funciona igual con ambas.

### 5.2 Patrones de sonido (uno por tipo de alerta)

| Alerta | Frecuencia | Patrón | Origen |
|---|---|---|---|
| Microsueño | 1600 Hz | 6 ciclos de doble pitido (180 ms ON / 100 ms OFF / 180 ms ON / 300 ms OFF) | `GET /alarma` desde la PC |
| Alcohol | 900 Hz | 2 ciclos de doble pitido (200/200/200/800 ms) | Fin de la prueba MQ-3 con detección |
| Calidad de aire | 800 Hz | 2 ciclos de doble pitido (200/350/200/1000 ms) | Lectura MQ-135 sobre el umbral |

> Los patrones usan `delay()`, es decir, **bloquean** el `loop()` mientras suenan
> (≈ 4,6 s el de microsueño). Durante ese tiempo el ESP32 no lee botones ni
> responde peticiones HTTP.

---

## 6. Ventilador con transistor TIP142 (GPIO 23)

El ventilador consume más corriente de la que puede entregar un GPIO (máx. ~20–40 mA),
así que se maneja con un **TIP142**, un transistor **Darlington NPN** que trabaja
como interruptor del lado bajo (*low-side switch*).

### 6.1 Conexión

```
                       +5 V (o la tensión del ventilador)
                          │
                    ┌─────┴─────┐
                    │ VENTILADOR│        D1 (1N4007) en paralelo con el ventilador,
                    │   (+)(−)  │◄──|──┐ cátodo hacia +5 V  (diodo de rueda libre)
                    └─────┬─────┘      │
                          ├────────────┘
                          │
                     C (colector)
 GPIO 23 ──[ 220 Ω ]── B   TIP142
                     E (emisor)
                          │
 GND ─────────────────────┴──────────── GND del ESP32 (GND común)
```

Pines del TIP142 (encapsulado TO-247, mirando de frente con las patas abajo):
**B – C – E**, de izquierda a derecha.

### 6.2 Explicación

- **GPIO 23 en HIGH (3,3 V)** → circula corriente por la base → el Darlington
  satura → el colector queda prácticamente a GND → el ventilador se enciende.
- **GPIO 23 en LOW** → no hay corriente de base → el transistor se corta → el
  ventilador se apaga.
- **Resistencia de base de 220 Ω:** es el valor que figuraba en el firmware
  original ("base del TIP142, via R 220ohm"). Corriente aproximada:
  (3,3 V − ~1,4 V de V<sub>BE</sub> del Darlington) / 220 Ω ≈ **8–9 mA**, segura
  para el GPIO. Gracias a la ganancia del Darlington (h<sub>FE</sub> ≥ 1000), eso
  alcanza para varios cientos de mA en el colector.
- **Diodo de rueda libre (1N4007):** el motor del ventilador es una carga
  inductiva; al apagarlo genera un pico de tensión inversa. El diodo lo absorbe
  y protege al transistor. ⚠️ **A verificar en la placa** si está montado
  (el código no puede saberlo); es muy recomendable.
- **GND común:** el emisor del TIP142, el GND de la fuente del ventilador y el
  GND del ESP32 deben estar unidos; si no, la base no tiene referencia y el
  transistor no conmuta.

### 6.3 Cuándo se enciende

Solo durante la **prueba de alcoholemia**: el ventilador succiona el aliento hacia
el MQ-3 durante 7 s y luego se apaga. El firmware lo pone en LOW al arrancar, al
encender y al apagar el sistema.

---

## 7. ADS1115 — conversor analógico-digital por I²C (GPIO 21 / 22)

### 7.1 Por qué se usa

Los sensores MQ entregan una tensión analógica. El ESP32 tiene un ADC interno,
pero es de 12 bits, poco lineal, y el ADC2 deja de funcionar cuando el WiFi está
activo. El **ADS1115** da **16 bits**, buena linealidad, y se comunica por I²C,
así que no le roba pines analógicos al ESP32.

### 7.2 Conexión

| Pin ADS1115 | Conectar a | Nota |
|---|---|---|
| VDD | **3,3 V** del ESP32 (recomendado) | ⚠️ ver 7.4 |
| GND | GND común | |
| SCL | GPIO 22 | `Wire.begin(SDA=21, SCL=22)` ✅ |
| SDA | GPIO 21 | ✅ |
| ADDR | **GND** | Fija la dirección **0x48** que usa `ads.begin(0x48)` ✅ |
| ALRT | Sin conectar | El firmware no usa la alerta por hardware |
| A0 | AO del **MQ-135** | `readADC_SingleEnded(0)` ✅ |
| A1 | AO del **MQ-3** | `readADC_SingleEnded(1)` ✅ |
| A2, A3 | Sin conectar | |

- El bus I²C necesita resistencias **pull-up** en SDA y SCL; los módulos
  ADS1115 comerciales ya las traen (≈ 10 kΩ hacia VDD).
- Si `ads.begin(0x48)` falla, el monitor serie muestra
  **"¡ERROR: ADS1115 no encontrado!"**. Causas típicas: SDA/SCL invertidos,
  ADDR sin conectar, o falta de GND común.

### 7.3 Cómo se usan las lecturas

| Canal | Sensor | Cuándo se lee | Umbral (lectura cruda) |
|---|---|---|---|
| A0 | MQ-135 | Cada **500 ms** con el sistema encendido, y también en cada vuelta del `loop()` para el parpadeo del LED rojo | > **3500** |
| A1 | MQ-3 | Cada **250 ms** durante la ventana de **7 s** de la prueba (≈ 28 lecturas) | > **2000** (basta con una lectura sobre el umbral) |

### 7.4 ⚠️ Precauciones eléctricas (revisar en la placa)

1. **Alimentación del ADS1115 a 3,3 V.** Si se alimenta a 5 V, sus pull-ups
   llevan SDA/SCL a 5 V, y los GPIO del ESP32 no son tolerantes a 5 V. A 3,3 V
   el bus queda al nivel correcto.
2. **Tensión de entrada en A0/A1.** El ADS1115 no acepta en sus entradas más que
   VDD + 0,3 V. Los módulos MQ alimentados a 5 V pueden sacar por AO hasta
   ~4–5 V con mucho gas. Si el ADS1115 está a 3,3 V, conviene un **divisor
   resistivo** en cada AO (p. ej. 10 kΩ / 20 kΩ → factor 2/3) para no pasar de
   3,3 V.
3. **Factor de conversión a mV.** El firmware y el backend usan
   **0,125 mV por unidad** (ganancia 1x, rango ±4,096 V), pero el código **no
   llama a `ads.setGain()`**. La librería Adafruit usa por defecto
   `GAIN_TWOTHIRDS` (±6,144 V), que equivale a **0,1875 mV por unidad**. Por lo
   tanto, los mV que se muestran en el monitor serie y en el dashboard están
   **subestimados en un factor de 1,5**. Los umbrales no se ven afectados porque
   se comparan en unidades crudas. Para corregirlo: agregar
   `ads.setGain(GAIN_ONE);` después de `ads.begin()`, o cambiar el factor a
   0,1875.

---

## 8. Sensores MQ-3 y MQ-135

Ambos son sensores de gas de óxido metálico (SnO₂), con un **calefactor
interno** que necesita **5 V**. Normalmente vienen en módulos de 4 pines:

| Pin del módulo | Conectar a |
|---|---|
| VCC | **5 V** (pin VIN/5V del ESP32 o fuente externa) |
| GND | GND común |
| AO (salida analógica) | MQ-135 → **ADS1115 A0** · MQ-3 → **ADS1115 A1** (con divisor si aplica, 7.4) |
| DO (salida digital del comparador) | **Sin conectar**: el firmware no la usa; el umbral se aplica por software |

Funcionamiento: cuanto más gas detecta, menor es la resistencia del sensor, y la
**tensión en AO sube**. Por eso la alerta se dispara cuando la lectura **supera**
el umbral.

- **Precalentamiento:** el firmware espera **10 s** al arrancar ("Estabilizando
  y precalentando sensores químicos") antes de iniciar el ADS1115. Es un tiempo
  mínimo para la demo; la hoja de datos recomienda 24–48 h de *burn-in* la
  primera vez para que las lecturas sean estables.
- **Consumo:** cada calefactor consume ≈ 150 mA a 5 V. Con los dos sensores más el
  ventilador se superan fácilmente los 400 mA. ⚠️ Si todo se alimenta desde el
  USB del ESP32, la tensión puede caer y el ESP32 reiniciarse al encender el
  ventilador. Se recomienda una **fuente externa de 5 V** para los sensores y el
  ventilador, con el GND unido al del ESP32.
- **Ubicación física:** el MQ-3 va en el ducto del ventilador (recibe el aliento
  succionado); el MQ-135 queda expuesto al aire de la cabina.

---

## 9. Conexión de red (WiFi / HTTP)

El ESP32 se conecta como estación (`WIFI_STA`) a la red configurada en el
sketch, y levanta un **servidor HTTP en el puerto 80**:

```
 Laptop (detección PyTorch + webcam)
     │  GET http://<IP_ESP32>/alarma   → buzzer de microsueño (si el sistema está ON)
     │  GET http://<IP_ESP32>/ok       → silencia el buzzer y apaga el LED rojo
     ▼
 ESP32 :80
     ▲
     │  GET http://<IP_ESP32>/status   cada 1,5 s → JSON con sensores y actuadores
 Backend Node (dashboard)
```

La IP asignada se imprime en el monitor serie (115200 baudios) al arrancar, y es
la que se configura como `ESP32_IP` en el backend y en el script de detección.

---

## 10. Alimentación y tierra (resumen)

| Riel | Alimenta | Nota |
|---|---|---|
| USB 5 V → regulador del ESP32 | ESP32 | |
| **3,3 V** (pin 3V3) | ADS1115, pull-ups de los botones | No conectar cargas grandes |
| **5 V** (VIN o fuente externa) | MQ-3, MQ-135, ventilador (por el TIP142), buzzer con transistor | Fuente externa recomendada |
| **GND común** | Todo | Imprescindible: ESP32, ADS1115, sensores, emisor del TIP142 y fuente externa |

---

## 11. Checklist de verificación en la placa

- [ ] El monitor serie muestra "Módulo ADS1115 OK." (I²C y dirección 0x48 correctos).
- [ ] Al presionar el botón general se enciende el LED verde (y se apaga al volver a presionar).
- [ ] Con el sistema encendido, el monitor muestra "MONITOR SENSORES -> MQ-135 …" cada 0,5 s.
- [ ] Al presionar el botón MQ-3: LED rojo y ventilador encendidos durante 7 s, con ~4 líneas `[RÁFAGA]` por segundo.
- [ ] Al acercar alcohol al MQ-3 durante la prueba, el valor de A1 sube por encima de 2000 y al final suena el buzzer de 900 Hz.
- [ ] `curl http://<IP_ESP32>/alarma` con el sistema encendido hace sonar el patrón de 1600 Hz.
- [ ] `curl http://<IP_ESP32>/status` devuelve el JSON.
- [ ] Al encender el ventilador, el ESP32 no se reinicia (si se reinicia, revisar la alimentación de 5 V).
