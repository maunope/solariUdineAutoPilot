#include <LiquidCrystal_I2C.h>  //https://github.com/johnrickman/LiquidCrystal_I2C

#include <RTClib.h>             //https://github.com/adafruit/RTClib

#include <SparkFun_External_EEPROM.h>  //https://github.com/sparkfun/SparkFun_External_EEPROM_Arduino_Library

// #define DEBUG_MODE
// #define ALWAYS_DST
// #define NEVER_DST
// #define FAST_DEBUG

const unsigned long EEPROM_SIGNATURE = 0xD24F789F;
const int EEPROM_PAGE_SIZE_BYTES = 64;
const int MAX_WRITE_PER_EEPROM_PAGE = 10080; // approx one week of regular operation
const int EEPROM_SIZE_BYTES = 32768;
const int EEPROM_TYPE = 256;
const int EEPROM_ADDRESS_BYTES = 2;

// struct for the EEProm index page
struct EepromIndexDescriptor {
  unsigned long signature;
  int pageOffset = 0;
};

// struct for clock status to be saved to the eeprom
struct EepromData {
  DateTime dateTime = DateTime(WINT_MAX);
  int nextPulsePolarity = 0;
  int currentWrites = 0;
  bool pausedTillNextDay = false;
};

const int motorPulseEnablePin = 6;
const int motorPulseUpPin = 7;
const int motorPulseDownPin = 8;
const int pushButtonPin = 9;
const int feedbackLedPin = 4;

const int secsPerMinute = 60;

LiquidCrystal_I2C lcd(0x27, 16, 2);
RTC_DS3231 rtc;
ExternalEEPROM extEeprom;

// to avoid straining the clock, if time is more than 120 seconds off will
// remain still until the next day
const int MAX_CATCHUP_MINUTES = 120;
// when true the clock will not self adjust until  RTC times is one full day
// ahead, then eeprom time will be bumped 24hrs, within MAX_CATCHUP_MINUTES, and
// self adjustment will take place
bool pausedTillNextDay = false;
// tracks last pulse sent to motor, to enforce max pace set by secsBetweenPulses
DateTime lastPulseTime = DateTime((uint32_t) 0);
// minimun delay between pulses, overridden by manual advances. real Solari
// Udine controllers use 4s
int secsBetweenPulses = 3;

// how long the push button has been pressed
unsigned long pushButtonPressedMillis = 0;
// duration of motor pulses, note shorter between circuit 555 timer and this
// applies
int pulseDurationMillis = 400;

void setup() {
  #ifdef FAST_DEBUG
  secsPerMinute = 60;
  secsBetweenPulses = 0;
  pulseDurationMillis = 10;
  #endif

  // Init pins
  pinMode(motorPulseEnablePin, INPUT);
  pinMode(motorPulseUpPin, OUTPUT);
  pinMode(motorPulseDownPin, OUTPUT);
  pinMode(feedbackLedPin, OUTPUT);
  pinMode(pushButtonPin, INPUT);
  digitalWrite(motorPulseUpPin, LOW);
  digitalWrite(motorPulseDownPin, LOW);
  digitalWrite(feedbackLedPin, LOW);

  // Init Serial and i2c modules
  Serial.begin(9600);

  lcd.init();
  lcd.backlight();
  lcd.clear();

  // check if the eeprom is available and initialize its parameters, check for
  // valid time in the eeprom is repeated in the loop
  if (!extEeprom.begin()) {
    Serial.println("External eeprom Error");
    blinkFeedbackLed(300, 300, 30);
    // reset
    asm volatile("  jmp 0");
  }
  extEeprom.setMemoryType(EEPROM_TYPE);
  extEeprom.setMemorySizeBytes(EEPROM_SIZE_BYTES);
  extEeprom.setAddressBytes(
    EEPROM_ADDRESS_BYTES); // Set address bytes and page size after
  // MemorySizeBytes()
  extEeprom.setPageSizeBytes(EEPROM_PAGE_SIZE_BYTES);

  // Under no circumnstance read/write the RTC module when not ready!
  // if RTC is unavailabe sends a blink pattern and then resets
  if (!rtc.begin()) {
    Serial.println("RTC Error");
    blinkFeedbackLed(50, 300, 30);
    // reset
    asm volatile("  jmp 0");
  }
  // if the rtc module is new, sets time. this only makes sense when connected
  // to a PC,  standard time is always used, DST compensation is applied at runtime
  // time zone is not relevant
  if (rtc.lostPower()) {
    Serial.println("Power lost, resetting time. (check battery maybe?");
    rtc.adjust(getStandardTime(DateTime(F(__DATE__), F(__TIME__))));
  }
}

// blinks the feedback led alternating given on/off time, for the given number
// of times.
void blinkFeedbackLed(int onMillis, int offMillis, int iterations) {
  for (int i = 0; i < iterations; i++) {
    digitalWrite(feedbackLedPin, HIGH);
    delay(onMillis);
    digitalWrite(feedbackLedPin, LOW);
    delay(offMillis);
  }
  digitalWrite(feedbackLedPin, LOW);
}

// writes EepromData struct to the eeprom, spreading writes over pages to
// maximize the eeprom life if the eeprom has no record of the current page, a
// new index page is created and writes restart from the first available page
int writeTimeDataToEeprom(EepromData & eepromData) {
  const int eepromDataPagesSizeBytes =
    ceil(sizeof(EepromData) / (float) EEPROM_PAGE_SIZE_BYTES) *
    EEPROM_PAGE_SIZE_BYTES;
  const int eepromIndexDescriptorSizeBytes =
    ceil(sizeof(EepromIndexDescriptor) / (float) EEPROM_PAGE_SIZE_BYTES) *
    EEPROM_PAGE_SIZE_BYTES;

  // Serial.println(String(sizeof(EepromData))+" a
  // "+String(eepromDataPagesSizeBytes)+" b
  // "+String((sizeof(EepromIndexDescriptor)))+" c
  // "+String(eepromIndexDescriptorSizeBytes));
  EepromIndexDescriptor eepromIndexDescriptor;
  eepromIndexDescriptor.signature = 0;

  Serial.println(String(EEPROM_SIGNATURE, HEX) +
    " eepromIndexDescriptor.signature: " +
    String(eepromIndexDescriptor.signature, HEX) +
    " eepromIndexDescriptor.pageOffset: " +
    String(eepromIndexDescriptor.pageOffset));

  // check if first 4 bytes contain the required signature string
  extEeprom.get(0, eepromIndexDescriptor);

  Serial.println(String(EEPROM_SIGNATURE, HEX) +
    " eepromIndexDescriptor.signature: " +
    String(eepromIndexDescriptor.signature, HEX) +
    " eepromIndexDescriptor.pageOffset: " +
    String(eepromIndexDescriptor.pageOffset));
  // check if first 4 bytes contain the required signature string

  if (eepromIndexDescriptor.signature != EEPROM_SIGNATURE) {
    // EEPROM is not initialized, write the signature and default address and
    // set 0 writes in counter
    Serial.println("EEprom not initialized, setting index page");

    eepromIndexDescriptor.signature = EEPROM_SIGNATURE;
    // set to the number of bytes containing the descriptor
    eepromIndexDescriptor.pageOffset = eepromIndexDescriptorSizeBytes;

    extEeprom.put(0, eepromIndexDescriptor);

    // write a blank time, setting to WINT_MAX ensures this dummy time will
    // prevent unwanted pulses
    EepromData blankTimeData;
    blankTimeData.dateTime = WINT_MAX;
    blankTimeData.nextPulsePolarity = 0;
    blankTimeData.currentWrites = 0;
    extEeprom.put(eepromIndexDescriptor.pageOffset, eepromIndexDescriptor);
  }

  EepromData currentEepromData;
  extEeprom.get(eepromIndexDescriptor.pageOffset, currentEepromData);

  // switch to next page if the number of writes on the current one exceeds the
  // desided value
  //  in case of power failure currentWrites will be reset and the current page
  //  will get more writes than
  // planned, this is a fair tradeoff as outages are infrequent and updating the
  // index page on each write would quickly wear it off
  if (currentEepromData.currentWrites > MAX_WRITE_PER_EEPROM_PAGE) {
    // Serial.println(String(currentEepromData.currentWrites) + " ->next page "
    // + String(eepromIndexDescriptor.pageOffset)); switch forward one page, if
    // we reached the top, start back from first page after index
    eepromIndexDescriptor.pageOffset =
      (eepromIndexDescriptor.pageOffset + eepromDataPagesSizeBytes <
        EEPROM_SIZE_BYTES) ?
      eepromIndexDescriptor.pageOffset + eepromDataPagesSizeBytes :
      eepromIndexDescriptorSizeBytes;
    eepromIndexDescriptor.signature = EEPROM_SIGNATURE;
    extEeprom.put(0, eepromIndexDescriptor);
    // reset writes counter in currentEepromData
    eepromData.currentWrites = 0;
    // Serial.println("New page offset: " +
    // String(eepromIndexDescriptor.pageOffset));
  } else {
    // write to eeprom and track we have +1 write on the current page
    eepromData.currentWrites = eepromData.currentWrites + 1;
  }

  extEeprom.put(eepromIndexDescriptor.pageOffset, eepromData);

  return 1;
}

// Function to read data from EEPROM, returns -1 and sets dateTime to WINT_MAX
// if the eeprom is uninitialized setting dateTime to WINT_MAX is a safety
// measure: it avoids any random/unwanted impulse as RTC time will always be
// lower
int readEepromData(EepromData & eepromData) {
  EepromIndexDescriptor eepromIndexDescriptor;
  extEeprom.get(0, eepromIndexDescriptor);

  // check if first 4 bytes contain the required signature string
  if (eepromIndexDescriptor.signature != EEPROM_SIGNATURE) {
    Serial.println("EEprom is not initialized");
    // EEPROM is not initialized, return 0
    eepromData.dateTime = WINT_MAX;
    eepromData.nextPulsePolarity = 0;
    return -1; // Indicate that EEPROM is not initialized
  }

  EepromData timeDataFromEEprom;
  extEeprom.get(eepromIndexDescriptor.pageOffset, eepromData);
  return 1; // Data read successfully
}

// returns true if give DateTime is DST, applies Central Europe rules
bool isDST(DateTime time) {
  #ifdef DEBUG_MODE
  #ifdef ALWAYS_DST
  time = DateTime((String(time.year()) + "-04-01%00:00:00").c_str());
  #endif
  #ifdef NEVER_DST
  time = DateTime((String(time.year()) + "-11-01%00:00:00").c_str());
  #endif
  #endif
  // Calculate the last Sunday of March
  DateTime dstStart =
    DateTime((String(time.year()) + "-03-31%02:00:00").c_str());
  while (dstStart.dayOfTheWeek() != 1) { // 1 represents Sunday
    dstStart = dstStart - TimeSpan(SECONDS_PER_DAY);
  }
  // Calculate the last Sunday of October
  DateTime dstEnd = DateTime((String(time.year()) + "-10-31%03:00:00").c_str());
  while (dstEnd.dayOfTheWeek() != 1) { // 1 represents Sunday
    dstEnd = dstEnd - TimeSpan(SECONDS_PER_DAY);
  }
  return (time >= dstStart && time <= dstEnd);
}

// reads  time form rtc module. RTC always container standard time, DST is applied during runtime
// to be called After RTC module initiation only!
// in debug mode, inits communication with rtc on each read and returns 0 in
// case of failure.
DateTime getRTCDateTime() {
  // return SECONDSOFFSET + (millis() / 1000);
  #ifdef DEBUG_MODE
  if (!rtc.begin()) {
    return DateTime((uint32_t) 0);
  }
  #endif
  return rtc.now();
}

// given a DateTime, returns its winter time
DateTime getStandardTime(DateTime time) {
  return (isDST(time) ? time - TimeSpan(secsPerMinute * 60) : time);
}
// returns the DST adjusted value of the given DateTime
DateTime getDSTAdjustedTime(DateTime time) {
  return (isDST(time) ? time + TimeSpan(secsPerMinute * 60) : time);
}

// sends a pulse for to the motor for the given duration and polarity
// and updates next polarity in eepromData
// actual motor pulse won't exceed what the 555 timer in the circuit allows
void sendPulse(EepromData & eepromData, int pulseDurationMillis) {
  int polarity =
    eepromData.nextPulsePolarity != 0 ? eepromData.nextPulsePolarity : 1;

  digitalWrite(motorPulseUpPin, LOW);
  digitalWrite(motorPulseDownPin, LOW);
  if (polarity > 0) {
    digitalWrite(motorPulseUpPin, HIGH);
    digitalWrite(motorPulseDownPin, LOW);
  } else {
    digitalWrite(motorPulseUpPin, LOW);
    digitalWrite(motorPulseDownPin, HIGH);
  }
  delay(pulseDurationMillis);

  digitalWrite(motorPulseUpPin, LOW);
  digitalWrite(motorPulseDownPin, LOW);
  eepromData.nextPulsePolarity = polarity > 0 ? -1 : 1;
}

// displays debug info on LCD, does not compensate DST, send compensated DateTime
void pulseDebugStringToDisplay(EepromData eepromData, DateTime RTCDateTime,
  bool motorPulseEnable, bool isDST) {
  lcd.setCursor(0, 0);
  lcd.print("R:" + String(RTCDateTime.hour() < 10 ? "0" : "") +
    String(RTCDateTime.hour()) + ":" +
    (RTCDateTime.minute() < 10 ? "0" : "") +
    String(RTCDateTime.minute()) + ":" +
    (RTCDateTime.second() < 10 ? "0" : "") +
    String(RTCDateTime.second()) + " ");
  lcd.setCursor(12, 0);
  lcd.print(String(eepromData.nextPulsePolarity == 0 ?
      "/" :
      (eepromData.nextPulsePolarity > 0 ? "+" : "-")) +
    " " + String(motorPulseEnable ? "E" : "D"));
  lcd.setCursor(0, 1);
  lcd.print("E:" + String(eepromData.dateTime.hour() < 10 ? "0" : "") +
    String(eepromData.dateTime.hour()) + ":" +
    (eepromData.dateTime.minute() < 10 ? "0" : "") +
    String(eepromData.dateTime.minute()) + ":" +
    (eepromData.dateTime.second() < 10 ? "0" : "") +
    String(eepromData.dateTime.second()) + " ");
  lcd.setCursor(12, 1);
  lcd.print(isDST ? "D" : "W");
  lcd.setCursor(14, 1);
  lcd.print(eepromData.pausedTillNextDay ? "P" : "R");
}

// - reset every week
// - if automatic advancement is enabled, check eeprom time Vs. RTC module time
// and advance if needed
// - handle push button input
void loop() {
  // self reset every week, just in case I f*cked up and some variable would
  // overflow left unchecked
  if (millis() >= 604800000UL) {
    asm volatile("  jmp 0");
  }

  bool motorPulseEnable = digitalRead(motorPulseEnablePin) == HIGH;

  // RTCDateTime is the time coming from the battery backed Real Time Clock module, always in standard time
  DateTime RTCDateTime = getRTCDateTime();
  // DST adjusted version of RTCDateTime
  DateTime dstAdjRTCDateTime = getDSTAdjustedTime(RTCDateTime);

  // EEPromTimeData is the last recorded time, polarity and no of writes on the
  // current eeprom page  that was pulsed to the moto
  EepromData eepromData;
  readEepromData(eepromData);
  // if EEprom has no valid time set, skip clock movement and notify user
  // Serial.println("serial "+String(eepromData.dateTime.unixtime())+"
  // "+String(eepromData.pausedTillNextDay)+"
  // "+String(eepromData.dateTime.isValid()));
  if (!eepromData.dateTime.isValid()) {
    lcd.setCursor(0, 0);
    lcd.print("Adj. time");
    Serial.println("No valid time set in eeprom, please adjust clock");
    blinkFeedbackLed(300, 50, 5);
  } else {
    // regular operation

    // if the motor enable signal is turned off, don't calculate/send pulses
    if (motorPulseEnable) {
      // if the clock has been sleping for days, skip them until it's <24hours
      // behind, only hh:mm matters anyway
      while ((dstAdjRTCDateTime - eepromData.dateTime).days() > 0) {
        // not saving this to eeprom on purpose, no flip movement, in case of a
        // power loss this calculation will be performed again
        eepromData.dateTime = eepromData.dateTime + TimeSpan(SECONDS_PER_DAY);
      }

      // less than 24hrs, possible situations:
      //  - clock is less than MAX_CATCHUP_MINUTES behind -> regular catch up,
      //  following  code section will take care of it
      //  - clock is more than MAX_CATCHUP_MINUTES -> wait until it's e more
      //  than 24hrs behind again, then the while cycle a few lines above will
      //  bump it within the MAX_CATCHUP_MINUTES interval (note that bumping
      //  24hrs immediately would give the same result )

      // if actual time is more than one minute ahead of what has been already
      // pulsed to the motor, shoot one pulse pace pulses every
      // secsBetweenPulses seconds to avoid straining the flip clock
      long rtcEepromTimeDiffSeconds =
        (dstAdjRTCDateTime - eepromData.dateTime).totalseconds();
      if (((rtcEepromTimeDiffSeconds > secsPerMinute) && ((RTCDateTime - lastPulseTime).totalseconds() > secsBetweenPulses))) {
        if (rtcEepromTimeDiffSeconds / secsPerMinute >= MAX_CATCHUP_MINUTES) {
          Serial.println("More than " + String(MAX_CATCHUP_MINUTES) + " minutes to catch up, pausing till next day");
          eepromData.dateTime = eepromData.dateTime + TimeSpan(SECONDS_PER_DAY);
          eepromData.pausedTillNextDay = true;
          writeTimeDataToEeprom(eepromData);
        } else {
          // shoot a pulse
          // TODO make this a pulse to a 555
          sendPulse(eepromData, pulseDurationMillis);
          // record the last pulsed time, advance by one minute
          eepromData.dateTime = eepromData.dateTime + TimeSpan(secsPerMinute);
          eepromData.pausedTillNextDay = false;
          writeTimeDataToEeprom(eepromData);

          lastPulseTime = RTCDateTime;
        }
      }
      if (eepromData.pausedTillNextDay) {
        // signal I'm waiting for next day
        blinkFeedbackLed(300, 50, 2);
      }
    }

    pulseDebugStringToDisplay(eepromData, dstAdjRTCDateTime, motorPulseEnable, isDST(RTCDateTime));
  }
  // handle inputs from pushbutton
  // brief press: advance flip clock by one minute
  // 3 seconds press: align EEPromTime to RTCDateTime, to be used after manual
  // flip clock adjustment
  if (digitalRead(pushButtonPin) == HIGH) {
    if (pushButtonPressedMillis == 0) {
      pushButtonPressedMillis = millis();
    }
    // long prss
    else if (millis() - pushButtonPressedMillis >= 3000) {
      // this totals to 1 second
      blinkFeedbackLed(100, 100, 5);
      // sets the current Secs on the eeprom to the value coming from the RTC,
      // setting seconds to zero
      eepromData.dateTime = dstAdjRTCDateTime - TimeSpan(dstAdjRTCDateTime.second());
      eepromData.pausedTillNextDay = false;
      writeTimeDataToEeprom(eepromData);
      Serial.println("Manual Adjustment completed");
      pushButtonPressedMillis = 0;
    }
  }
  // short press
  else if (pushButtonPressedMillis > 0) {
    pushButtonPressedMillis = 0;
    blinkFeedbackLed(100, 0, 1);
    sendPulse(eepromData, pulseDurationMillis);
    eepromData.dateTime = eepromData.dateTime + TimeSpan(secsPerMinute);
    writeTimeDataToEeprom(eepromData);
    Serial.println("Sent a manual pulse");
  }
}