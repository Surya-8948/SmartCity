/*
  ESP32 Smart City 
*/
#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESP32Servo.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ---------------- User pin map ----------------
constexpr uint8_t PIN_BIN_TRIG    = 13;
constexpr uint8_t PIN_BIN_ECHO    = 14; // 12Boot strap pin: GPIO14 is safer if available
constexpr uint8_t PIN_BIN_SERVO   = 27;
constexpr uint8_t PIN_LDR_DIGITAL = 34; //15 Boot strap pin: GPIO34 is safer if available
constexpr uint8_t PIN_NIGHT_LED   = 25;
constexpr uint8_t PIN_PIR         = 18;
constexpr uint8_t PIN_PIR_LED     = 32;
constexpr uint8_t PIN_MQ2_ANALOG  = 36; // ADC1
constexpr uint8_t PIN_IR_PARKING  = 39;
constexpr uint8_t PIN_PARKING_LED = 33;
constexpr uint8_t PIN_RAIN_ANALOG = 35; // ADC1
constexpr uint8_t PIN_CLOTH_SERVO = 19;
constexpr uint8_t PIN_I2C_SDA     = 21;
constexpr uint8_t PIN_I2C_SCL     = 22;

const char *AP_SSID = "SmartCity-Mecatredz";
const char *AP_PASSWORD = "smartcity123";

constexpr bool LDR_ACTIVE_LOW = false;
constexpr bool IR_ACTIVE_LOW = true;
constexpr bool RAIN_VALUE_DECREASES_WHEN_WET = true;
constexpr float BIN_OPEN_DISTANCE_CM = 10.0f;
constexpr float BIN_REARM_DISTANCE_CM = 15.0f;
constexpr uint8_t BIN_CLOSED_ANGLE = 0;
constexpr uint8_t BIN_OPEN_ANGLE = 90;
constexpr uint32_t BIN_OPEN_TIME_MS = 5000;
constexpr uint16_t RAIN_WET_THRESHOLD = 1800;
constexpr uint16_t RAIN_HYSTERESIS = 180;
constexpr uint8_t CLOTH_DRY_ANGLE = 0;
constexpr uint8_t CLOTH_WET_ANGLE = 90;
constexpr uint16_t MQ2_ALARM_THRESHOLD = 1800;
constexpr uint32_t PARKING_LED_TIME_MS = 3000;
constexpr uint32_t SONAR_TIMEOUT_US = 30000; // Maximum block is only inside sonar task

LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo binServo;
Servo clothServo;
AsyncWebServer server(80);
AsyncEventSource events("/events");
SemaphoreHandle_t dataMutex;

struct SystemState {
  float distance = -1.0f;
  bool binOpen = false;
  bool dark = false;
  bool motion = false;
  uint16_t mq2 = 0;
  bool gasAlarm = false;
  bool parking = false;
  bool parkingLed = false;
  uint16_t rainRaw = 0;
  bool rain = false;
  uint8_t clothAngle = CLOTH_DRY_ANGLE;
};
SystemState state;

SystemState getSnapshot() {
  SystemState copy;
  if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(30)) == pdTRUE) {
    copy = state;
    xSemaphoreGive(dataMutex);
  }
  return copy;
}

String makeStatusJson() {
  SystemState s = getSnapshot();
  char json[430];
  snprintf(json, sizeof(json),
    "{\"distance\":%.1f,\"binOpen\":%s,\"dark\":%s,\"motion\":%s,"
    "\"mq2\":%u,\"gasAlarm\":%s,\"parking\":%s,\"parkingLed\":%s,"
    "\"rainRaw\":%u,\"rain\":%s,\"clothAngle\":%u,\"uptime\":%lu}",
    s.distance, s.binOpen ? "true" : "false", s.dark ? "true" : "false",
    s.motion ? "true" : "false", s.mq2, s.gasAlarm ? "true" : "false",
    s.parking ? "true" : "false", s.parkingLed ? "true" : "false",
    s.rainRaw, s.rain ? "true" : "false", s.clothAngle,
    (unsigned long)millis());
  return String(json);
}

const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html><html lang="en"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 Smart City</title>
<style>
:root{--bg:#07111f;--panel:#102137;--line:#203b59;--text:#e8f2ff;--muted:#91a9c3;--blue:#43b6ff;--green:#38df91;--red:#ff6275;--amber:#ffc857}
*{box-sizing:border-box}body{margin:0;background:radial-gradient(circle at top,#123355 0,#07111f 45%);color:var(--text);font-family:system-ui,-apple-system,Segoe UI,sans-serif;min-height:100vh}
main{width:min(1100px,92%);margin:auto;padding:32px 0 50px}header{display:flex;justify-content:space-between;align-items:center;gap:20px;margin-bottom:24px}.eyebrow{color:var(--blue);letter-spacing:.16em;font-size:.75rem;font-weight:800}h1{font-size:clamp(1.8rem,5vw,3.2rem);margin:.2rem 0}.sub{color:var(--muted)}#connection{padding:8px 12px;border:1px solid var(--line);border-radius:999px;color:var(--amber);white-space:nowrap}.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:14px}.card{background:linear-gradient(145deg,rgba(18,43,70,.96),rgba(11,27,45,.96));border:1px solid var(--line);border-radius:18px;padding:18px;box-shadow:0 14px 35px #0005;min-height:145px}.top{display:flex;justify-content:space-between;align-items:center}.icon{font-size:1.55rem}.label{color:var(--muted);font-size:.8rem;text-transform:uppercase;letter-spacing:.08em}.value{font-size:1.65rem;font-weight:800;margin:16px 0 5px}.detail{color:var(--muted);font-size:.88rem}.dot{display:inline-block;width:9px;height:9px;border-radius:50%;margin-right:7px;background:var(--muted)}.on{color:var(--green)}.on .dot{background:var(--green);box-shadow:0 0 12px var(--green)}.alert{color:var(--red)}.alert .dot{background:var(--red);box-shadow:0 0 12px var(--red)}footer{color:var(--muted);text-align:center;margin-top:28px;font-size:.82rem}@media(max-width:600px){header{align-items:flex-start;flex-direction:column}}
</style></head><body><main>
<header><div><div class="eyebrow">LIVE MUNICIPAL IOT</div><h1>Smart City Control</h1><div class="sub">Real-time ESP32 sensor and actuator status</div></div><div id="connection">● Connecting</div></header>
<section class="grid">
<div class="card"><div class="top"><span class="label">Smart Bin</span><span class="icon">🗑️</span></div><div class="value" id="bin">--</div><div class="detail" id="binState">Waiting for data</div></div>
<div class="card"><div class="top"><span class="label">Night Lighting</span><span class="icon">💡</span></div><div class="value" id="night">--</div><div class="detail">Digital LDR controlled streetlight</div></div>
<div class="card"><div class="top"><span class="label">Motion Zone</span><span class="icon">🚶</span></div><div class="value" id="motion">--</div><div class="detail">PIR controlled security light</div></div>
<div class="card"><div class="top"><span class="label">Air Safety</span><span class="icon">🌫️</span></div><div class="value" id="gas">--</div><div class="detail" id="gasRaw">MQ-2 raw: --</div></div>
<div class="card"><div class="top"><span class="label">Parking Gate</span><span class="icon">🚗</span></div><div class="value" id="parking">--</div><div class="detail" id="parkLed">Indicator: --</div></div>
<div class="card"><div class="top"><span class="label">Rain & Clothes</span><span class="icon">🌧️</span></div><div class="value" id="rain">--</div><div class="detail" id="rainRaw">Rain raw: --</div></div>
</section><footer>ESP32 SoftAP · Live updates using Server-Sent Events · <span id="uptime">--</span></footer>
</main><script>
const $=id=>document.getElementById(id);function state(el,text,active,alarm=false){el.innerHTML='<span class="dot"></span>'+text;el.className='value '+(alarm?'alert':active?'on':'')}
function render(d){$('connection').textContent='● Live';$('connection').style.color='#38df91';$('bin').textContent=d.distance<0?'No echo':d.distance.toFixed(1)+' cm';$('binState').textContent=d.binOpen?'Lid open — closing automatically':'Lid closed';state($('night'),d.dark?'LIGHT ON':'DAY / OFF',d.dark);state($('motion'),d.motion?'MOTION / ON':'CLEAR / OFF',d.motion);state($('gas'),d.gasAlarm?'GAS ALERT':'AIR NORMAL',!d.gasAlarm,d.gasAlarm);$('gasRaw').textContent='MQ-2 raw: '+d.mq2;state($('parking'),d.parking?'VEHICLE':'CLEAR',d.parking);$('parkLed').textContent='3-second indicator: '+(d.parkingLed?'ON':'OFF');state($('rain'),d.rain?'RAIN / COVERED':'DRY / NORMAL',d.rain,d.rain);$('rainRaw').textContent='Rain raw: '+d.rainRaw+' · Servo: '+d.clothAngle+'°';let s=Math.floor(d.uptime/1000);$('uptime').textContent='Uptime '+Math.floor(s/3600)+'h '+Math.floor(s%3600/60)+'m '+s%60+'s'}
const es=new EventSource('/events');es.addEventListener('update',e=>render(JSON.parse(e.data)));es.onopen=()=>{$('connection').textContent='● Live';$('connection').style.color='#38df91'};es.onerror=()=>{$('connection').textContent='● Reconnecting';$('connection').style.color='#ffc857'};
fetch('/api/status').then(r=>r.json()).then(render).catch(()=>{});
</script></body></html>
)HTML";

void setupWebServer() {
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false); // More reliable continuous SSE in SoftAP mode
  if (!WiFi.softAP(AP_SSID, AP_PASSWORD, 1, false, 4)) {
    Serial.println("ERROR: SoftAP could not start");
  }

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, "text/html", INDEX_HTML);
  });
  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "application/json", makeStatusJson());
  });
  server.on("/health", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "OK");
  });
  events.onConnect([](AsyncEventSourceClient *client) {
    String payload = makeStatusJson();
    client->send(payload.c_str(), "update", millis(), 2000);
  });
  server.addHandler(&events);
  server.onNotFound([](AsyncWebServerRequest *request) {
    request->send(404, "application/json", "{\"error\":\"Not found\"}");
  });
  server.begin();
}

void taskSonarBin(void *parameter) {
  bool binOpen = false;
  bool armed = true;
  uint32_t closeAt = 0;
  TickType_t lastWake = xTaskGetTickCount();

  for (;;) {
    // pulseIn has a strict 30 ms timeout and blocks ONLY this task.
    digitalWrite(PIN_BIN_TRIG, LOW);
    delayMicroseconds(3);
    digitalWrite(PIN_BIN_TRIG, HIGH);
    delayMicroseconds(10);
    digitalWrite(PIN_BIN_TRIG, LOW);
    uint32_t pulse = pulseIn(PIN_BIN_ECHO, HIGH, SONAR_TIMEOUT_US);
    float distance = pulse ? pulse * 0.0343f / 2.0f : -1.0f;
    if (distance < 2.0f || distance > 400.0f) distance = -1.0f;

    uint32_t now = millis();
    if (!binOpen && armed && distance > 0 && distance < BIN_OPEN_DISTANCE_CM) {
      binServo.write(BIN_OPEN_ANGLE);
      binOpen = true;
      armed = false;
      closeAt = now + BIN_OPEN_TIME_MS;
    }
    if (binOpen && (int32_t)(now - closeAt) >= 0) {
      binServo.write(BIN_CLOSED_ANGLE);
      binOpen = false;
    }
    if (!armed && distance > BIN_REARM_DISTANCE_CM) armed = true;

    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
      state.distance = distance;
      state.binOpen = binOpen;
      xSemaphoreGive(dataMutex);
    }
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(200));
  }
}

void taskSensors(void *parameter) {
  bool rain = false;
  bool oldRain = false;
  bool parkingArmed = true;
  bool parkingLed = false;
  uint32_t parkingOffAt = 0;
  TickType_t lastWake = xTaskGetTickCount();

  for (;;) {
    bool ldrLevel = digitalRead(PIN_LDR_DIGITAL);
    bool dark = LDR_ACTIVE_LOW ? !ldrLevel : ldrLevel;
    bool motion = digitalRead(PIN_PIR);
    uint16_t mq2 = analogRead(PIN_MQ2_ANALOG);
    bool gas = mq2 >= MQ2_ALARM_THRESHOLD;
    bool irLevel = digitalRead(PIN_IR_PARKING);
    bool parking = IR_ACTIVE_LOW ? !irLevel : irLevel;
    uint16_t rainRaw = analogRead(PIN_RAIN_ANALOG);

    if (RAIN_VALUE_DECREASES_WHEN_WET) {
      if (!rain && rainRaw <= RAIN_WET_THRESHOLD) rain = true;
      else if (rain && rainRaw >= RAIN_WET_THRESHOLD + RAIN_HYSTERESIS) rain = false;
    } else {
      if (!rain && rainRaw >= RAIN_WET_THRESHOLD) rain = true;
      else if (rain && rainRaw + RAIN_HYSTERESIS <= RAIN_WET_THRESHOLD) rain = false;
    }

    digitalWrite(PIN_NIGHT_LED, dark);
    digitalWrite(PIN_PIR_LED, motion);
    if (rain != oldRain) {
      clothServo.write(rain ? CLOTH_WET_ANGLE : CLOTH_DRY_ANGLE);
      oldRain = rain;
    }

    uint32_t now = millis();
    if (parking && parkingArmed) {
      parkingArmed = false;
      parkingLed = true;
      parkingOffAt = now + PARKING_LED_TIME_MS;
      digitalWrite(PIN_PARKING_LED, HIGH);
    }
    if (parkingLed && (int32_t)(now - parkingOffAt) >= 0) {
      parkingLed = false;
      digitalWrite(PIN_PARKING_LED, LOW);
    }
    if (!parking) parkingArmed = true;

    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
      state.dark = dark;
      state.motion = motion;
      state.mq2 = mq2;
      state.gasAlarm = gas;
      state.parking = parking;
      state.parkingLed = parkingLed;
      state.rainRaw = rainRaw;
      state.rain = rain;
      state.clothAngle = rain ? CLOTH_WET_ANGLE : CLOTH_DRY_ANGLE;
      xSemaphoreGive(dataMutex);
    }
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(50));
  }
}

void lcdLine(uint8_t row, String text) {
  if (text.length() > 16) text.remove(16);
  while (text.length() < 16) text += ' ';
  lcd.setCursor(0, row);
  lcd.print(text);
}

void taskLCD(void *parameter) {
  const String welcome = "   Welcome to ESP32 Smart City Made by - Saloni Singh -Khushi Gupta - Chanda -Priyanshi - Preeti GGP Ayodhya Thanks";
  String doubled = welcome + welcome;
  lcdLine(0, "** SMART CITY **");
  for (uint16_t i = 0; i < welcome.length(); i++) {
    lcdLine(1, doubled.substring(i, i + 16));
    vTaskDelay(pdMS_TO_TICKS(250));
  }
  lcd.clear();

  uint8_t page = 0;
  for (;;) {
    SystemState s = getSnapshot();
    switch (page) {
      case 0:
        lcdLine(0, String("BIN ") + (s.distance < 0 ? String("NO ECHO") : String(s.distance, 1) + "cm") + (s.binOpen ? " OP" : " CL"));
        lcdLine(1, String("NIGHT:") + (s.dark ? "ON" : "OFF") + " PIR:" + (s.motion ? "ON" : "OFF"));
        break;
      case 1:
        lcdLine(0, String("GAS:") + s.mq2 + (s.gasAlarm ? " ALERT" : " SAFE"));
        lcdLine(1, String("PARK:") + (s.parking ? "CAR" : "CLEAR") + " L:" + (s.parkingLed ? "ON" : "OFF"));
        break;
      case 2:
        lcdLine(0, String("RAIN:") + s.rainRaw);
        lcdLine(1, String(s.rain ? "WET" : "DRY") + " CLOTH:" + (s.rain ? "COVER" : "NORMAL"));
        break;
      case 3:
        lcdLine(0, "WiFi:SmartCity");
        lcdLine(1, WiFi.softAPIP().toString());
        break;
    }
    page = (page + 1) % 4;
    vTaskDelay(pdMS_TO_TICKS(2200));
  }
}

void taskWebPush(void *parameter) {
  for (;;) {
    String payload = makeStatusJson();
    events.send(payload.c_str(), "update", millis());

    // ESPAsyncWebServer 3.9.4 manages disconnected SSE clients internally.
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}
void setup() {
  Serial.begin(115200);
  pinMode(PIN_BIN_TRIG, OUTPUT);
  pinMode(PIN_BIN_ECHO, INPUT);
  digitalWrite(PIN_BIN_TRIG, LOW);
  pinMode(PIN_LDR_DIGITAL, INPUT);
  pinMode(PIN_NIGHT_LED, OUTPUT);
  pinMode(PIN_PIR, INPUT);
  pinMode(PIN_PIR_LED, OUTPUT);
  pinMode(PIN_IR_PARKING, INPUT);
  pinMode(PIN_PARKING_LED, OUTPUT);
  digitalWrite(PIN_NIGHT_LED, LOW);
  digitalWrite(PIN_PIR_LED, LOW);
  digitalWrite(PIN_PARKING_LED, LOW);

  analogReadResolution(12);
  analogSetPinAttenuation(PIN_MQ2_ANALOG, ADC_11db);
  analogSetPinAttenuation(PIN_RAIN_ANALOG, ADC_11db);

  binServo.setPeriodHertz(50);
  clothServo.setPeriodHertz(50);
  binServo.attach(PIN_BIN_SERVO, 500, 2400);
  clothServo.attach(PIN_CLOTH_SERVO, 500, 2400);
  binServo.write(BIN_CLOSED_ANGLE);
  clothServo.write(CLOTH_DRY_ANGLE);

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  lcd.init();
  lcd.backlight();
  lcd.clear();

  dataMutex = xSemaphoreCreateMutex();
  if (dataMutex == nullptr) {
    Serial.println("FATAL: mutex creation failed");
    while (true) delay(1000);
  }

  setupWebServer();

  // Core 0: networking. Core 1: hardware tasks. Priorities keep sonar timely.
  xTaskCreatePinnedToCore(taskSonarBin, "SonarBin", 4096, nullptr, 3, nullptr, 1);
  xTaskCreatePinnedToCore(taskSensors,  "Sensors",  4096, nullptr, 2, nullptr, 1);
  xTaskCreatePinnedToCore(taskLCD,      "LCD",      4096, nullptr, 1, nullptr, 1);
  xTaskCreatePinnedToCore(taskWebPush,  "WebPush",  4096, nullptr, 1, nullptr, 0);

  Serial.println("FreeRTOS Smart City started");
  Serial.print("Wi-Fi: "); Serial.println(AP_SSID);
  Serial.print("Open: http://"); Serial.println(WiFi.softAPIP());
}

void loop() {
  // Arduino loop task remains idle; all work is done by FreeRTOS tasks.
  vTaskDelay(portMAX_DELAY);
}
