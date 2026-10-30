/*
  Crop-Aware Predictive Solar Cold Storage - Prototype Controller
  Board: ESP32
  Sensors: DS18B20 + solar voltage divider
  Display: SSD1306 128x64 I2C OLED
  Relays: Relay 1 = source selection, Relay 2 = fan ON/OFF

  IMPORTANT SAFETY:
  - The solar panel must NOT be connected directly to a 12 V fan.
    Your panel's Voc is up to 22.5 V.
  - Relay 1 must switch between two SAFE, compatible 12 V sources:
      NC = regulated 12 V adapter positive
      NO = regulated solar-path positive (must be regulated/limited to
           a safe voltage for the fan and able to supply its starting current)
      COM = selected positive output to Relay 2 COM
  - Relay 2: COM = selected positive; NO = fan positive; NC unused.
  - All supply negatives must be wired according to the approved circuit.
    Do not connect the raw panel positive to the fan.
  - SOLAR_PATH_SAFE is FALSE by default. Do not change it to true until
    the solar power path has been verified under fan load.
  - Voltage threshold is only a voltage-based approximation. Voltage alone
    does NOT prove available solar power/current.
*/

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <OneWire.h>
#include <DallasTemperature.h>

// ---------------- OLED ----------------
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1
#define OLED_ADDRESS  0x3C

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ---------------- ESP32 PINS ----------------
#define TEMP_PIN        4
#define SOLAR_ADC_PIN   34
#define SOURCE_RELAY    23   // Relay 1: LOW=solar (NO), HIGH=adapter (NC)
#define FAN_RELAY       19   // Relay 2: LOW=fan ON, HIGH=fan OFF

// ---------------- USER SETTINGS ----------------
// Your divider: R1=100k from panel+ to GPIO34 junction.
// R2=15.5k from GPIO34 junction to GND (13.5k + 2k in series).
// Ratio = (R1 + R2) / R2 = 115.5 / 15.5.
const float DIVIDER_RATIO = 115.5f / 15.5f;

// Solar switching threshold with hysteresis.
// This is an illustrative voltage threshold, NOT a power measurement.
// Calibrate against measured, regulated solar-path voltage under load.
const float SOLAR_SWITCH_TO_SOLAR_V = 12.5f;
const float SOLAR_SWITCH_TO_ADAPTER_V = 11.5f;

// Fan temperature hysteresis (demonstration settings only).
const float FAN_ON_TEMP_C  = 30.0f;
const float FAN_OFF_TEMP_C = 28.0f;

// Keep FALSE until solar output is safely regulated for the 12 V fan
// and verified to maintain voltage/current with the fan starting/running.
const bool SOLAR_PATH_SAFE = false;

// Most common 4-channel relay modules are active LOW.
// If your relay board behaved oppositely during testing, change these.
const int RELAY_ON  = LOW;
const int RELAY_OFF = HIGH;

// ---------------- SENSOR OBJECTS ----------------
OneWire oneWire(TEMP_PIN);
DallasTemperature tempSensor(&oneWire);

// ---------------- STATE ----------------
bool fanOn = false;
bool usingSolar = false;
bool sourceInitialized = false;

float readSolarVoltage() {
  // Average multiple readings to reduce noise.
  const int samples = 20;
  uint32_t totalMilliVolts = 0;

  for (int i = 0; i < samples; i++) {
    totalMilliVolts += analogReadMilliVolts(SOLAR_ADC_PIN);
    delay(5);
  }

  float adcVoltage = (totalMilliVolts / (float)samples) / 1000.0f;
  return adcVoltage * DIVIDER_RATIO;
}

void setSource(bool solarSelected) {
  usingSolar = solarSelected;
  digitalWrite(SOURCE_RELAY, usingSolar ? RELAY_ON : RELAY_OFF);
}

void setFan(bool on) {
  fanOn = on;
  digitalWrite(FAN_RELAY, fanOn ? RELAY_ON : RELAY_OFF);
}

void updateDisplay(float solarVoltage, float temperatureC, bool tempValid) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(F("CROP COLD STORAGE"));
  display.drawLine(0, 11, 127, 11, SSD1306_WHITE);

  display.setCursor(0, 17);
  display.print(F("Solar: "));
  display.print(solarVoltage, 1);
  display.println(F(" V"));

  display.setCursor(0, 29);
  display.print(F("Temp: "));
  if (tempValid) {
    display.print(temperatureC, 1);
    display.println(F(" C"));
  } else {
    display.println(F("Sensor error"));
  }

  display.setCursor(0, 42);
  display.print(F("Source: "));
  display.println(usingSolar ? F("SOLAR") : F("ADAPTER"));

  display.setCursor(0, 54);
  display.print(F("Fan: "));
  display.println(fanOn ? F("ON") : F("OFF"));

  display.display();
}

void setup() {
  Serial.begin(115200);

  // Set relay pins to inactive state.
  pinMode(SOURCE_RELAY, OUTPUT);
  pinMode(FAN_RELAY, OUTPUT);
  digitalWrite(SOURCE_RELAY, RELAY_OFF);
  digitalWrite(FAN_RELAY, RELAY_OFF);

  pinMode(SOLAR_ADC_PIN, INPUT);
  analogReadResolution(12);
  analogSetPinAttenuation(SOLAR_ADC_PIN, ADC_11db);

  tempSensor.begin();

  // OLED I2C: SDA=GPIO21, SCL=GPIO22.
  Wire.begin(21, 22);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println(F("OLED not detected. Check wiring/address."));
    while (true) {
      setFan(false);
      delay(1000);
    }
  }

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(F("System starting..."));
  display.display();

  // Start in adapter mode (Relay 1 de-energized, NC selected).
  setSource(false);
  setFan(false);
  sourceInitialized = true;

  delay(1000);
}

void loop() {
  float solarVoltage = readSolarVoltage();

  // Read DS18B20.
  tempSensor.requestTemperatures();
  float temperatureC = tempSensor.getTempCByIndex(0);
  bool tempValid =
      (temperatureC != DEVICE_DISCONNECTED_C) &&
      (temperatureC > -55.0f) &&
      (temperatureC < 125.0f);

  // Source selection:
  // Disabled by default for safety. When enabled, this uses voltage only
  // as a simple demonstration threshold, not a true available-power test.
  if (!SOLAR_PATH_SAFE) {
    setSource(false);  // Adapter position only.
  } else {
    if (!sourceInitialized) {
      setSource(false);
      sourceInitialized = true;
    }

    if (!usingSolar && solarVoltage >= SOLAR_SWITCH_TO_SOLAR_V) {
      setSource(true);
    } else if (usingSolar &&
               solarVoltage <= SOLAR_SWITCH_TO_ADAPTER_V) {
      setSource(false);
    }
  }

  // Temperature-based fan control with hysteresis.
  // On sensor failure, turn fan off for this demonstration.
  if (!tempValid) {
    setFan(false);
  } else {
    if (!fanOn && temperatureC >= FAN_ON_TEMP_C) {
      setFan(true);
    } else if (fanOn && temperatureC <= FAN_OFF_TEMP_C) {
      setFan(false);
    }
  }

  updateDisplay(solarVoltage, temperatureC, tempValid);

  Serial.print(F("Solar: "));
  Serial.print(solarVoltage, 2);
  Serial.print(F(" V | Temperature: "));

  if (tempValid) {
    Serial.print(temperatureC, 1);
    Serial.print(F(" C"));
  } else {
    Serial.print(F("Sensor error"));
  }

  Serial.print(F(" | Source command: "));
  Serial.print(usingSolar ? F("SOLAR") : F("ADAPTER"));
  Serial.print(F(" | Fan command: "));
  Serial.println(fanOn ? F("ON") : F("OFF"));

  delay(1000);
}
