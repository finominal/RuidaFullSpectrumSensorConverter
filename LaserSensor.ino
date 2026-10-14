/*
 * HQ-A68-1 water flow sensor + dual OPB830W55Z optical limit switches, Arduino Mini
 *
 * Pin map:
 *   D2   IN   HQ-A68-1 Hall-effect flow sensor pulse (hardware interrupt INT0)
 *   D12  OUT  LOW when flow exceeds threshold
 *   D13  OUT  onboard LED, ON when flow exceeds threshold
 *   D11  OUT  LOW when X limit switch (A0) is triggered (slot blocked)
 *   D10  OUT  LOW when Y limit switch (A1) is triggered (slot blocked)
 *   A0   IN   OPB830W55Z X limit switch (white collector wire)
 *   A1   IN   OPB830W55Z Y limit switch (white collector wire)
 *
 * OPB830W55Z wiring per sensor:
 *   Red   + 220Ω resistor -> 5V
 *   Black -> GND
 *   White -> A0 or A1 (with 10kΩ pull-up to 5V)
 *   Green -> GND
 *
 * Signal logic: HIGH = slot blocked (limit triggered), LOW = slot clear
 *
 * Timing model:
 *   - Limit switch reads and their dependent outputs (D10, D11) update EVERY
 *     loop iteration, so they react promptly (microseconds latency).
 *   - Flow RATE is calculated once per SAMPLE_INTERVAL (1 second), because
 *     measuring pulse frequency inherently needs a time window.
 *   - Serial reporting is also once per SAMPLE_INTERVAL.
 */

// ---- Pin assignments ----
const uint8_t  FLOW_PIN          = 2;     // hardware interrupt pin (INT0)
const uint8_t  FLOW_OUT_PIN      = 12;    // LOW above flow threshold
const uint8_t  LIMIT_X_OUT_PIN   = 11;    // LOW when X limit triggered
const uint8_t  LIMIT_Y_OUT_PIN   = 10;    // LOW when Y limit triggered
const uint8_t  LIMIT_X_PIN       = A0;    // OPB830W55Z X limit switch
const uint8_t  LIMIT_Y_PIN       = A1;    // OPB830W55Z Y limit switch

// ---- Timing ----
const uint16_t SAMPLE_INTERVAL = 1000;  // ms between flow-rate calcs + reports

// ---- Flow threshold ----
const float FLOW_THRESHOLD_LPM = 2.0;   // L/min above which D12 goes LOW

// ---- Flow calibration ----
// frequency (Hz) = K * flowRate (L/min); K ~5.5-6.0 for many G3/8" sensors.
// VERIFY by running a measured volume through and adjusting.
const float PULSES_PER_LITRE = 330.0;   // = K(5.5) * 60; tweak after testing

// ---- State ----
volatile uint32_t pulseCount = 0;       // updated inside the ISR
uint32_t lastSampleTime      = 0;
float    totalLitres         = 0.0;
float    flowRateLPM         = 0.0;     // last calculated rate

bool limitX = false;   // true = X limit switch triggered (slot blocked)
bool limitY = false;   // true = Y limit switch triggered (slot blocked)

void countPulse() {
  pulseCount++;
}

void setup() {
  Serial.begin(9600);

  pinMode(FLOW_PIN, INPUT_PULLUP);      // internal pull-up; external 10k still recommended
  attachInterrupt(digitalPinToInterrupt(FLOW_PIN), countPulse, RISING);

  pinMode(FLOW_OUT_PIN, OUTPUT);
  digitalWrite(FLOW_OUT_PIN, HIGH);     // start HIGH = flow below threshold

  pinMode(LED_BUILTIN, OUTPUT);         // D13 onboard LED
  digitalWrite(LED_BUILTIN, LOW);       // start OFF = flow below threshold

  pinMode(LIMIT_X_OUT_PIN, OUTPUT);
  digitalWrite(LIMIT_X_OUT_PIN, HIGH);  // start HIGH = not triggered

  pinMode(LIMIT_Y_OUT_PIN, OUTPUT);
  digitalWrite(LIMIT_Y_OUT_PIN, HIGH);  // start HIGH = not triggered

  // Limit switch sanity check: confirm both sensors read sensibly.
  // With an OPB830W55Z, you should read either HIGH (clear) or LOW (blocked),
  // not stuck at a rail. If a pin reads stuck, check wiring and pull-up.
  uint8_t x0 = digitalRead(LIMIT_X_PIN);
  uint8_t y0 = digitalRead(LIMIT_Y_PIN);
  Serial.print(F("Limit switches at startup - X: "));
  Serial.print(x0 ? F("HIGH (clear)") : F("LOW (blocked)"));
  Serial.print(F("  Y: "));
  Serial.println(y0 ? F("HIGH (clear)") : F("LOW (blocked)"));

  lastSampleTime = millis();
  Serial.println(F("HQ-A68-1 flow sensor + OPB830W55Z limits ready"));
}

void loop() {
  // ===== Runs EVERY iteration: fast limit switch reads + dependent outputs =====

  // OPB830W55Z gives clean digital signal: HIGH = blocked, LOW = clear
  limitX = digitalRead(LIMIT_X_PIN);
  limitY = digitalRead(LIMIT_Y_PIN);

  // Limit switch outputs LOW when their sensor is triggered (slot blocked)
  digitalWrite(LIMIT_X_OUT_PIN, limitX ? LOW : HIGH);
  digitalWrite(LIMIT_Y_OUT_PIN, limitY ? LOW : HIGH);

  // ===== Runs once per SAMPLE_INTERVAL: flow-rate calc + reporting =====
  uint32_t now = millis();
  if (now - lastSampleTime >= SAMPLE_INTERVAL) {
    // Snapshot and reset the pulse counter with interrupts briefly off
    noInterrupts();
    uint32_t pulses = pulseCount;
    pulseCount = 0;
    interrupts();

    float elapsedMinutes = (now - lastSampleTime) / 60000.0;
    lastSampleTime = now;

    float litresThisSample = pulses / PULSES_PER_LITRE;
    flowRateLPM = litresThisSample / elapsedMinutes;
    totalLitres += litresThisSample;

    // Flow-driven outputs: D12 LOW and D13 LED ON when above threshold.
    bool flowHigh = flowRateLPM > FLOW_THRESHOLD_LPM;
    digitalWrite(FLOW_OUT_PIN, flowHigh ? LOW : HIGH);
    digitalWrite(LED_BUILTIN,  flowHigh ? HIGH : LOW);

    // ---- Report ----
    Serial.print(F("Flow: "));
    Serial.print(flowRateLPM, 2);
    Serial.print(F(" L/min  Total: "));
    Serial.print(totalLitres, 3);
    Serial.print(F(" L  D12:"));
    Serial.print(flowHigh ? F("LOW ") : F("HIGH"));
    Serial.print(F(" LED:"));
    Serial.print(flowHigh ? F("ON ") : F("OFF"));
    Serial.print(F("  X-limit: "));
    Serial.print(limitX ? F("BLOCKED") : F("clear   "));
    Serial.print(F(" D11:"));
    Serial.print(limitX ? F("LOW ") : F("HIGH"));
    Serial.print(F("  Y-limit: "));
    Serial.print(limitY ? F("BLOCKED") : F("clear   "));
    Serial.print(F(" D10:"));
    Serial.println(limitY ? F("LOW") : F("HIGH"));
  }
}