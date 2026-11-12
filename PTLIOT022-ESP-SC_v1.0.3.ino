/********************************************************
 * SMART MEDICINE BOX – FINAL ESP32 CODE (EEPROM SAFE)
 ********************************************************/

#include <Wire.h>
#include <RTClib.h>
#include <LiquidCrystal_I2C.h>
#include <BluetoothSerial.h>
#include <Preferences.h>

/* ---------------- PIN CONFIG ---------------- */
#define BUZZER_PIN   2
#define BUTTON_PIN   26
#define RESET_HOLD_TIME 3000
#define DEFAULT_PILL_COUNT 10

/* ---------------- OBJECTS ---------------- */
RTC_DS1307 rtc;
LiquidCrystal_I2C lcd(0x27, 16, 2);
BluetoothSerial SerialBT;
Preferences prefs;

/* ---------------- CONFIG ---------------- */
#define MAX_REMINDERS 5

int reminderHours[MAX_REMINDERS];
int reminderMinutes[MAX_REMINDERS];
bool reminderDone[MAX_REMINDERS];
int reminderCount = 0;

int lastTabletCount = DEFAULT_PILL_COUNT;

/* ---------------- LOG HELPERS ---------------- */
void logSerial(String msg) {
  Serial.print("[ESP32] ");
  Serial.println(msg);
}

void showLCD(String l1, String l2) {
  lcd.clear();
  lcd.setCursor(0,0); lcd.print(l1);
  lcd.setCursor(0,1); lcd.print(l2);
}

/* ---------------- BUTTON LONG PRESS ---------------- */
bool isButtonHeld(unsigned long holdTimeMs) {
  unsigned long start = millis();
  while (millis() - start < holdTimeMs) {
    if (digitalRead(BUTTON_PIN) == HIGH) return false;
    delay(10);
  }
  return true;
}

/* =================================================
   LOAD REMINDER SCHEDULE FROM NVS
   ================================================= */
void loadScheduleFromNVS() {
  reminderCount = prefs.getInt("rem_cnt", 0);

  if (reminderCount < 0 || reminderCount > MAX_REMINDERS) {
    reminderCount = 0;
  }

  for (int i = 0; i < reminderCount; i++) {
    String key = "rem_" + String(i);
    String t = prefs.getString(key.c_str(), "");

    if (t.length() == 4) {
      reminderHours[i]   = t.substring(0,2).toInt();
      reminderMinutes[i] = t.substring(2,4).toInt();
      reminderDone[i] = false;
    }
  }

  logSerial("Loaded reminders: " + String(reminderCount));
}

/* =================================================
   SAVE REMINDER SCHEDULE TO NVS
   ================================================= */
void saveScheduleToNVS() {
  prefs.putInt("rem_cnt", reminderCount);

  for (int i = 0; i < reminderCount; i++) {
    String key = "rem_" + String(i);
    char buf[5];
    snprintf(buf, sizeof(buf), "%02d%02d",
             reminderHours[i], reminderMinutes[i]);
    prefs.putString(key.c_str(), buf);
  }

  logSerial("Saved reminders to NVS");
}

/* =================================================
   PARSE CONFIG FROM PC
   ================================================= */
void parseReminderConfig(String data) {
  reminderCount = 0;
  data.replace("REM:", "");

  int start = 0;
  while (reminderCount < MAX_REMINDERS) {
    int comma = data.indexOf(',', start);
    String t = (comma == -1) ? data.substring(start)
                             : data.substring(start, comma);

    if (t.length() == 4) {
      reminderHours[reminderCount]   = t.substring(0,2).toInt();
      reminderMinutes[reminderCount] = t.substring(2,4).toInt();
      reminderDone[reminderCount] = false;
      reminderCount++;
    }

    if (comma == -1) break;
    start = comma + 1;
  }

  saveScheduleToNVS();
  SerialBT.println("REM_ACK");

  showLCD("Config Updated", "Rem:" + String(reminderCount));
  delay(1500);
}

/* ================================================= */
void checkBluetooth() {
  if (SerialBT.available()) {
    String msg = SerialBT.readStringUntil('\n');
    msg.trim();
    logSerial("BT RX: " + msg);

    if (msg.startsWith("REM:")) {
      parseReminderConfig(msg);
    }
  }
}

/* ================================================= */
bool waitForButton() {
  unsigned long start = millis();
  showLCD("Please take", "1 tablet & press");

  while (millis() - start < 300000) {
    if (digitalRead(BUTTON_PIN) == LOW) {
      delay(200);
      return true;
    }
    delay(50);
  }
  return false;
}

/* ================================================= */
int requestTabletCount() {
  SerialBT.write('a');
  unsigned long start = millis();

  while (millis() - start < 5000) {
    if (SerialBT.available()) {
      int val = SerialBT.parseInt();
      if (val >= 0) return val;
    }
  }
  return -1;
}

/* ================================================= */
void setup() {
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  Serial.begin(115200);
  SerialBT.begin("ESP32_Medicine_Box");

  Wire.begin();
  if (!rtc.begin()) {
  logSerial("RTC NOT FOUND");
  showLCD("RTC ERROR", "Check Wiring");
  while (1);
}

// if (!rtc.isrunning()) {
//   logSerial("RTC NOT RUNNING, SETTING TIME");

//   // Set RTC to compile time (ONE TIME)
//   rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));

//   showLCD("RTC SET", "Compile Time");
//   delay(2000);
// }


  lcd.init();
  lcd.backlight();

  prefs.begin("medicine", false);

  /* -------- HARD RESET LOGIC -------- */
  showLCD("Hold button", "to reset");

  delay(500);

  if (digitalRead(BUTTON_PIN) == LOW && isButtonHeld(RESET_HOLD_TIME)) {
    logSerial("FACTORY RESET");

    prefs.clear();                    // FULL EEPROM CLEAR
    lastTabletCount = DEFAULT_PILL_COUNT;
    prefs.putInt("tablet", lastTabletCount);

    showLCD("Pills Reset", "Count: 10");
    delay(3000);
  }

  /* -------- LOAD CLEAN VALUES -------- */
  lastTabletCount = prefs.getInt("tablet", DEFAULT_PILL_COUNT);
  if (lastTabletCount < 0) lastTabletCount = DEFAULT_PILL_COUNT;

  loadScheduleFromNVS();

  showLCD("Medicine Box", "Ready");
  delay(1500);
}

/* ================================================= */
void loop() {
  checkBluetooth();

  DateTime now = rtc.now();

  char l1[17], l2[17];
  snprintf(l1, sizeof(l1), "Time %02d:%02d:%02d",
           now.hour(), now.minute(), now.second());
  snprintf(l2, sizeof(l2), "Pill:%d Rem:%d",
           lastTabletCount, reminderCount);

  lcd.setCursor(0,0); lcd.print(l1);
  lcd.setCursor(0,1); lcd.print(l2);

  for (int i = 0; i < reminderCount; i++) {

    if (!reminderDone[i] &&
        now.hour() == reminderHours[i] &&
        now.minute() == reminderMinutes[i]) {

      reminderDone[i] = true;
      digitalWrite(BUZZER_PIN, HIGH);

      bool pressed = waitForButton();
      digitalWrite(BUZZER_PIN, LOW);

      if (!pressed) {
        SerialBT.println("MISSED");
        showLCD("Dose MISSED", "");
        delay(3000);
        continue;
      }

      int currentCount = requestTabletCount();

      if (currentCount < 0) {
        showLCD("Count Error", "Retry");
        delay(3000);
        continue;
      }

      if (currentCount < lastTabletCount) {
        SerialBT.println("TAKEN");
        showLCD("Medicine", "TAKEN");
      } else {
        SerialBT.println("NOT_TAKEN");
        showLCD("Medicine", "NOT TAKEN");
      }

      lastTabletCount = currentCount;
      prefs.putInt("tablet", lastTabletCount);
      delay(3000);
    }

    if (now.minute() != reminderMinutes[i]) {
      reminderDone[i] = false;
    }
  }

  delay(1000);
}
