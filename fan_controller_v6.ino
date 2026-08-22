//
// Automated Fan Controller with Manual Control
// Copyright (c) 2026 King Art Dedullo Pacumberto
//
// Author: King Art Dedullo Pacumberto
// Description: Arduino-based temperature-controlled fan
//              with automatic and manual control modes.
// ============================================================
//   AUTOMATIC + MANUAL FAN CONTROLLER v6
//   LM35 + MOSFET + Push Button
//
//   AUTO BEHAVIOR:
//   - Fan ALWAYS runs at LOW minimum (never stops in AUTO)
//   - Every 5 seconds, votes on temperature zone
//   - If MED or HIGH wins vote → step up with kick
//   - When temp drops back → return to LOW baseline (no kick)
//   - LOW is the permanent floor, forever
//
//   1st Press=AUTO | 2nd=LOW | 3rd=MED | 4th=HIGH | 5th=OFF 
// ============================================================

// ===== PIN DEFINITIONS =====
const int TEMP_PIN   = A0;
const int FAN_PIN    = 3;
const int BUTTON_PIN = 2;
const int LED_PIN    = 13;

// ===== MODES =====
int mode     = 0;
int lastMode = -1;

// ===== FAN SPEED CONSTANTS =====
const int FAN_OFF  = 0;
const int FAN_LOW  = 150;
const int FAN_MED  = 200;
const int FAN_HIGH = 255;
const int FAN_KICK = 255;

// ===== STARTUP KICK =====
const unsigned long KICK_DURATION = 600;
bool          kickActive          = false;
unsigned long kickStart           = 0;
int           kickTarget          = 0;

// ===== AUTO MODE THRESHOLDS (C) =====
const float TEMP_MED  = 35.0;
const float TEMP_HIGH = 40.0;

// ===== 5-SECOND VOTE WINDOW =====
const int           SAMPLE_COUNT    = 10;
const unsigned long SAMPLE_INTERVAL = 500;

int           tempVotes[4]   = {0, 0, 0, 0};
int           samplesTaken   = 0;
unsigned long lastSampleTime = 0;
int           committedSpeed = FAN_LOW;

// ===== BUTTON STATE MACHINE =====
typedef enum { BTN_IDLE, BTN_DEBOUNCING, BTN_PRESSED, BTN_RELEASED } ButtonState;
ButtonState   btnState   = BTN_IDLE;
bool          lastRawBtn = HIGH;
unsigned long btnTimer   = 0;
const unsigned long DEBOUNCE_MS = 50;

// ===== BUTTON HISTORY =====
const int HISTORY_SIZE = 5;
struct PressEvent {
  unsigned long timestamp;
  int           modeAfter;
};
PressEvent history[HISTORY_SIZE];
int historyIndex = 0;
int historyCount = 0;

// ===== SERIAL PRINT =====
unsigned long lastPrintTime = 0;
const unsigned long PRINT_INTERVAL = 500;

// ============================================================
void setup() {
  pinMode(FAN_PIN,    OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_PIN,    OUTPUT);

  // Boot kick — ensure fan starts spinning from the very beginning
  analogWrite(FAN_PIN, FAN_KICK);
  delay(KICK_DURATION);
  analogWrite(FAN_PIN, FAN_LOW);
  committedSpeed = FAN_LOW;

  digitalWrite(LED_PIN, HIGH);

  Serial.begin(9600);
  Serial.println(F("============================================"));
  Serial.println(F("   AUTO + MANUAL FAN CONTROLLER v6"));
  Serial.println(F("   AUTO: LOW is permanent floor forever"));
  Serial.println(F("   Votes every 5s to step up if hotter"));
  Serial.println(F("============================================"));
  Serial.println(F("  Press 1=AUTO | 2=LOW | 3=MED | 4=HIGH | 5=OFF"));
  Serial.println(F("============================================"));
  Serial.println(F("  Fan running at LOW baseline. Ready."));
  Serial.println();
}

// ============================================================
void loop() {

  // ----------------------------------------------------------
  // 1. BUTTON STATE MACHINE (non-blocking)
  // ----------------------------------------------------------
  bool rawBtn = digitalRead(BUTTON_PIN);

  switch (btnState) {
    case BTN_IDLE:
      if (rawBtn == LOW && lastRawBtn == HIGH) {
        btnState = BTN_DEBOUNCING;
        btnTimer = millis();
      }
      break;
    case BTN_DEBOUNCING:
      if (millis() - btnTimer >= DEBOUNCE_MS) {
        if (digitalRead(BUTTON_PIN) == LOW) {
          btnState = BTN_PRESSED;
          registerPress();
        } else {
          btnState = BTN_IDLE;
        }
      }
      break;
    case BTN_PRESSED:
      if (rawBtn == HIGH) {
        btnState = BTN_RELEASED;
        btnTimer = millis();
      }
      break;
    case BTN_RELEASED:
      if (millis() - btnTimer >= DEBOUNCE_MS) {
        btnState = BTN_IDLE;
      }
      break;
  }

  lastRawBtn = rawBtn;

  // ----------------------------------------------------------
  // 2. READ TEMPERATURE
  // ----------------------------------------------------------
  int   raw         = analogRead(TEMP_PIN);
  float voltage     = raw * (5.0 / 1023.0);
  float temperature = voltage * 100.0;

  // ----------------------------------------------------------
  // 3. AUTO MODE — vote every 500ms, decide every 5 seconds
  //    Zone 1 (LOW) is the minimum — zone 0 (OFF) never used
  // ----------------------------------------------------------
  if (mode == 0) {
    if (millis() - lastSampleTime >= SAMPLE_INTERVAL) {
      lastSampleTime = millis();

      int zone = classifyTempAuto(temperature);
      tempVotes[zone]++;
      samplesTaken++;

      Serial.print(F("  [VOTE] "));
      Serial.print(samplesTaken);
      Serial.print(F("/"));
      Serial.print(SAMPLE_COUNT);
      Serial.print(F("  Zone: "));
      printZoneName(zone);
      Serial.print(F("  | LOW="));
      Serial.print(tempVotes[1]);
      Serial.print(F(" MED="));
      Serial.print(tempVotes[2]);
      Serial.print(F(" HIGH="));
      Serial.println(tempVotes[3]);

      if (samplesTaken >= SAMPLE_COUNT) {
        int winner   = getMajorityZone();
        int newSpeed = zoneToSpeed(winner);

        Serial.println(F("  [RESULT] --------------------"));
        Serial.print(F("  Winner: "));
        printZoneName(winner);
        Serial.print(F("  Committed PWM: "));
        Serial.println(newSpeed);
        Serial.println(F("  ------------------------------"));

        if (newSpeed != committedSpeed) {
          if (newSpeed > committedSpeed) {
            // Stepping UP — kick to overcome inertia
            triggerKick(newSpeed);
          } else {
            // Stepping DOWN — return to LOW baseline smoothly, no kick
            committedSpeed = newSpeed;
            Serial.print(F("  [DOWN] Back to LOW baseline PWM: "));
            Serial.println(newSpeed);
          }
        } else {
          Serial.println(F("  [HOLD] No change needed."));
        }

        resetVotes();
      }
    }
  }

  // ----------------------------------------------------------
  // 4. DETERMINE TARGET SPEED
  // ----------------------------------------------------------
  int targetSpeed = 0;
  switch (mode) {
    case 0: targetSpeed = committedSpeed; break;
    case 1: targetSpeed = FAN_LOW;        break;
    case 2: targetSpeed = FAN_MED;        break;
    case 3: targetSpeed = FAN_HIGH;       break;
    case 4: targetSpeed = FAN_OFF;        break;
  }

  // Kick on manual mode switch
  bool modeChanged = (mode != lastMode);
  if (modeChanged && targetSpeed > FAN_OFF) {
    triggerKick(targetSpeed);
  }
  lastMode = mode;

  // ----------------------------------------------------------
  // 5. APPLY SPEED (kick overlay)
  // ----------------------------------------------------------
  int fanSpeed = 0;

  if (kickActive) {
    if (millis() - kickStart < KICK_DURATION) {
      fanSpeed = FAN_KICK;
    } else {
      kickActive     = false;
      committedSpeed = kickTarget;
      fanSpeed       = kickTarget;
      Serial.print(F("  [KICK DONE] Settled at PWM: "));
      Serial.println(kickTarget);
    }
  } else {
    fanSpeed = targetSpeed;
  }

  // Hard floor: AUTO never drops below LOW under any circumstance
  if (mode == 0 && !kickActive && fanSpeed < FAN_LOW) {
    fanSpeed = FAN_LOW;
  }

  analogWrite(FAN_PIN, fanSpeed);

  // ----------------------------------------------------------
  // 6. LED — ON=AUTO, OFF=Manual
  // ----------------------------------------------------------
  digitalWrite(LED_PIN, (mode == 0) ? HIGH : LOW);

  // ----------------------------------------------------------
  // 7. SERIAL STATUS (every 500ms)
  // ----------------------------------------------------------
  if (millis() - lastPrintTime >= PRINT_INTERVAL) {
    lastPrintTime = millis();
    Serial.print(F("Temp: "));
    Serial.print(temperature, 1);
    Serial.print(F("C | Mode: "));
    Serial.print(getModeName(mode));
    Serial.print(F("| PWM: "));
    Serial.print(fanSpeed);
    Serial.print(F(" | Committed: "));
    Serial.print(committedSpeed);
    if (mode == 0) {
      Serial.print(F(" | Samples: "));
      Serial.print(samplesTaken);
      Serial.print(F("/"));
      Serial.print(SAMPLE_COUNT);
    }
    if (kickActive) Serial.print(F(" [KICKING]"));
    Serial.println();
  }

  delay(50);
}

// ============================================================
//   CLASSIFY TEMP FOR AUTO
//   Returns zone 1, 2, or 3 — never 0 (LOW is the floor)
// ============================================================
int classifyTempAuto(float temp) {
  if (temp >= TEMP_HIGH) return 3;       // HIGH
  else if (temp >= TEMP_MED) return 2;   // MED
  else return 1;                         // LOW — default floor
}

// ============================================================
//   MAJORITY ZONE (1-3 only, never 0 in AUTO)
// ============================================================
int getMajorityZone() {
  int winner = 1;
  for (int i = 2; i < 4; i++) {
    if (tempVotes[i] > tempVotes[winner]) winner = i;
  }
  return winner;
}

// ============================================================
//   ZONE TO PWM
// ============================================================
int zoneToSpeed(int zone) {
  switch (zone) {
    case 1: return FAN_LOW;
    case 2: return FAN_MED;
    case 3: return FAN_HIGH;
  }
  return FAN_LOW;
}

// ============================================================
//   RESET VOTES
// ============================================================
void resetVotes() {
  for (int i = 0; i < 4; i++) tempVotes[i] = 0;
  samplesTaken = 0;
}

// ============================================================
//   STARTUP KICK
// ============================================================
void triggerKick(int settleSpeed) {
  kickActive = true;
  kickStart  = millis();
  kickTarget = settleSpeed;
  Serial.print(F("  [KICK] Full power 600ms then PWM "));
  Serial.println(settleSpeed);
}

// ============================================================
//   REGISTER BUTTON PRESS
// ============================================================
void registerPress() {
  mode++;
  if (mode > 4) mode = 0;

  if (mode == 0) {
    resetVotes();
    committedSpeed = FAN_LOW;
    triggerKick(FAN_LOW);
    Serial.println(F("  [AUTO] LOW baseline restored."));
  }

  if (mode == 4) {
  Serial.println(F("  [OFF] Fan stopped."));
  }

  history[historyIndex].timestamp = millis();
  history[historyIndex].modeAfter = mode;
  historyIndex = (historyIndex + 1) % HISTORY_SIZE;
  if (historyCount < HISTORY_SIZE) historyCount++;

  Serial.println(F("--------------------------------------------"));
  Serial.print(F("  BUTTON >> Mode: "));
  Serial.println(getModeName(mode));
  for (int i = 0; i < historyCount; i++) {
    int idx = (historyIndex - 1 - i + HISTORY_SIZE) % HISTORY_SIZE;
    Serial.print(F("  ["));
    Serial.print(historyCount - i);
    Serial.print(F("] t="));
    Serial.print(history[idx].timestamp);
    Serial.print(F("ms => "));
    Serial.println(getModeName(history[idx].modeAfter));
  }
  Serial.println(F("--------------------------------------------"));
}

// ============================================================
//   HELPERS
// ============================================================
void printZoneName(int z) {
  switch (z) {
    case 1: Serial.print(F("LOW  ")); break;
    case 2: Serial.print(F("MED  ")); break;
    case 3: Serial.print(F("HIGH ")); break;
    default: Serial.print(F("LOW  ")); break;
  }
}

String getModeName(int m) {
  switch (m) {
    case 0: return "AUTO   ";
    case 1: return "LOW    ";
    case 2: return "MEDIUM ";
    case 3: return "HIGH   ";
    case 4: return "OFF    ";
  }
  return "UNKNOWN";
}
