#include <EEPROM.h>
#include <LiquidCrystal_I2C.h>
#include <RTClib.h>


//#define SECONDSOFFSET 1731283200 //11 Novembre 2024 00:00
//#define SECONDSOFFSET 1722470400 //1 Agosto 2024 00:00
//#define SECONDSOFFSET 0
//#define SECONDSOFFSET (1711850400-5) //5 secondi prima del subentro ora legale 2024
//#define SECONDSOFFSET (1729994400-5) //5 secondi prima della fine di  ora legale 2024

//#define DEBUG_MODE
//#define ALWAYS_DST
//#define NEVER_DST
//#define FAST_DEBUG

const unsigned long EEPROM_SIGNATURE = 0xD24F789F; 
const int EEPROM_PAGE_SIZE_BYTES = 4;
const int MAX_WRITE_PER_EEPROM_PAGE = 10080; //approx one week of regular operation
const int EEPROM_SIZE_BYTES = 1024;

//to avoid straining the clock, if time is more than 120 seconds off will remain still until the next day
const int MAX_CATCHUP_MINUTES =  120; 
bool pausedTillNextDay=false;

struct EepromIndexDescriptor {
  unsigned long signature;
  int pageOffset = 0;
};

struct EepromData {
  DateTime dateTime = DateTime(WINT_MAX);
  int nextPulsePolarity = 0;
  int currentWrites = 0;
  bool pausedTillNextDay =false;
};

const int motorPulseEnablePin = 6;
const int motorPulseUpPin = 7;
const int motorPulseDownPin = 8;
const int pushButtonPin = 9;
const int feedbackLedPin = 4;
int secsPerMinute = 60;

unsigned long pushButtonPressedMillis = 0;

//last direction of the pulse, to be saved to EEPROM
int nextPulsePolarity = 0;

//tracks last pulse sent to motor, to enforce max pace set by secsBetweenPulses
DateTime lastPulseTime = DateTime((uint32_t) 0);
unsigned long secsBetweenPulses = 3;

//duration of motor pulses, note shorter between circuit 555 timer and this applies 
int pulseDurationMillis = 300;

LiquidCrystal_I2C lcd(0x27, 16, 2);
RTC_DS3231 rtc;

void setup() {
  #ifdef FAST_DEBUG
  secsPerMinute = 60;
  secsBetweenPulses = 0;
  pulseDurationMillis = 10;
  #endif

  //Init pins 
  pinMode(motorPulseEnablePin, INPUT);
  pinMode(motorPulseUpPin, OUTPUT);
  pinMode(motorPulseDownPin, OUTPUT);
  pinMode(feedbackLedPin, OUTPUT);
  pinMode(pushButtonPin, INPUT);
  digitalWrite(motorPulseUpPin, LOW);
  digitalWrite(motorPulseDownPin, LOW);
  digitalWrite(feedbackLedPin, LOW);

  //Init Serial and i2c modules
  Serial.begin(9600);

  lcd.init();
  lcd.backlight();
  lcd.clear();

  //Under no circumnstance read/write the RTC module when not ready!
  //if RTC is unavailabe sends a blink pattern and then resets
  if (!rtc.begin()) {
    Serial.println("RTC Error");
    blinkFeedbackLed(50, 300, 30);
    //reset
    asm volatile("  jmp 0");
  }

  //if the rtc module is new, sets time. this only makes sense when connected to a PC
  if (rtc.lostPower()) {
    Serial.println("Power lost, resetting time. (check battery maybe?");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

}

//blinks the feedback led alternating given on/off time, for the given number of times. 
void blinkFeedbackLed(int onMillis, int offMillis, int iterations) {
  for (int i = 0; i < iterations; i++) {
    digitalWrite(feedbackLedPin, HIGH);
    delay(onMillis);
    digitalWrite(feedbackLedPin, LOW);
    delay(offMillis);
  }
  digitalWrite(feedbackLedPin, LOW);
}

int writeTimeDataToEeprom(EepromData & eepromData) {

  const int eepromDataPagesSizeBytes = ceil(sizeof(EepromData) / (float) EEPROM_PAGE_SIZE_BYTES) * EEPROM_PAGE_SIZE_BYTES;
  const int eepromIndexDescriptorSizeBytes = ceil(sizeof(EepromIndexDescriptor) / (float) EEPROM_PAGE_SIZE_BYTES) * EEPROM_PAGE_SIZE_BYTES;

  //Serial.println(String(sizeof(EepromData))+" a "+String(eepromDataPagesSizeBytes)+" b "+String((sizeof(EepromIndexDescriptor)))+" c "+String(eepromIndexDescriptorSizeBytes));
  EepromIndexDescriptor eepromIndexDescriptor;
  eepromIndexDescriptor.signature = 0;

  Serial.println(String(EEPROM_SIGNATURE, HEX) + " eepromIndexDescriptor.signature: " + String(eepromIndexDescriptor.signature, HEX) + " eepromIndexDescriptor.pageOffset: " + String(eepromIndexDescriptor.pageOffset));

  //check if first 4 bytes contain the required signature string
  EEPROM.get(0, eepromIndexDescriptor);

  Serial.println(String(EEPROM_SIGNATURE, HEX) + " eepromIndexDescriptor.signature: " + String(eepromIndexDescriptor.signature, HEX) + " eepromIndexDescriptor.pageOffset: " + String(eepromIndexDescriptor.pageOffset));
  //check if first 4 bytes contain the required signature string

  if (eepromIndexDescriptor.signature != EEPROM_SIGNATURE) {
    // EEPROM is not initialized, write the signature and default address and set 0 writes in counter
    Serial.println("EEprom not initialized, setting index page");

    eepromIndexDescriptor.signature = EEPROM_SIGNATURE;
    //set to the number of bytes containing the descriptor
    eepromIndexDescriptor.pageOffset = eepromIndexDescriptorSizeBytes;

    EEPROM.put(0, eepromIndexDescriptor);

    //write a blank time, setting to WINT_MAX ensures this dummy time will prevent unwanted pulses
    EepromData blankTimeData;
    blankTimeData.dateTime = WINT_MAX;
    blankTimeData.nextPulsePolarity = 0;
    blankTimeData.currentWrites = 0;
    EEPROM.put(eepromIndexDescriptor.pageOffset, eepromIndexDescriptor);
  }

  EepromData currentEepromData;
  EEPROM.get(eepromIndexDescriptor.pageOffset, currentEepromData);

  //switch to next page if the number of writes on the current one exceeds the desided value
  if (currentEepromData.currentWrites > MAX_WRITE_PER_EEPROM_PAGE) {
    //Serial.println(String(currentEepromData.currentWrites) + " ->next page " + String(eepromIndexDescriptor.pageOffset));
    //switch forward one page, if we reached the top, start back from seconda page (first is index) 
    eepromIndexDescriptor.pageOffset = (eepromIndexDescriptor.pageOffset + eepromDataPagesSizeBytes < EEPROM_SIZE_BYTES) ? eepromIndexDescriptor.pageOffset + eepromDataPagesSizeBytes : eepromIndexDescriptorSizeBytes;
    eepromIndexDescriptor.signature = EEPROM_SIGNATURE;
    EEPROM.put(0, eepromIndexDescriptor);
    //reset writes counter in currentEepromData
    eepromData.currentWrites = 0;
    //Serial.println("New page offset: " + String(eepromIndexDescriptor.pageOffset));
  } else {
    //write to eeprom and track we have +1 write on the current page
    eepromData.currentWrites = eepromData.currentWrites + 1;
  }

  EEPROM.put(eepromIndexDescriptor.pageOffset, eepromData);

  return 1;
}

// Function to read data from EEPROM
int readEepromData(EepromData & eepromData) {

  EepromIndexDescriptor eepromIndexDescriptor;
  EEPROM.get(0, eepromIndexDescriptor);

  //check if first 4 bytes contain the required signature string
  if (eepromIndexDescriptor.signature != EEPROM_SIGNATURE) {
    Serial.println("EEprom is not initialized");
    // EEPROM is not initialized, return 0
    eepromData.dateTime = WINT_MAX;
    eepromData.nextPulsePolarity = 0;
    return -1; // Indicate that EEPROM is not initialized
  }

  EepromData timeDataFromEEprom;
  EEPROM.get(eepromIndexDescriptor.pageOffset, eepromData);
  return 1; // Data read successfully

}

//returns true if give DateTime is DST, applies Central Europe rules
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
  DateTime dstStart = DateTime((String(time.year()) + "-03-31%02:00:00").c_str());
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

//displays debug info on LCD
void pulseDebugStringToDisplay(EepromData eepromData, DateTime RTCDateTime, bool motorPulseEnable, bool isDST) {
  lcd.setCursor(0, 0);

  lcd.print("R:" + String(RTCDateTime.hour() < 10 ? "0" : "") + String(RTCDateTime.hour()) + ":" + (RTCDateTime.minute() < 10 ? "0" : "") + String(RTCDateTime.minute()) + ":" + (RTCDateTime.second() < 10 ? "0" : "") + String(RTCDateTime.second()) + " ");
  lcd.setCursor(12, 0);
  lcd.print(String(eepromData.nextPulsePolarity == 0 ? "/" : (eepromData.nextPulsePolarity > 0 ? "+" : "-")) + " " + String(motorPulseEnable ? "E" : "D"));
  lcd.setCursor(0, 1);
  lcd.print("E:" + String(eepromData.dateTime.hour() < 0 ? "0" : "") + String(eepromData.dateTime.hour()) + ":" + (eepromData.dateTime.minute() < 10 ? "0" : "") + String(eepromData.dateTime.minute()) + ":" + (eepromData.dateTime.second() < 10 ? "0" : "") + String(eepromData.dateTime.second()) + " ");
  lcd.setCursor(12, 1);
  lcd.print(isDST ? "D" : "W");
  lcd.setCursor(14, 1);
  lcd.print(eepromData.pausedTillNextDay ? "P" : "R");
}

//reads  time form rtc module.
//to be called After initiation only! 
//in debug mode, inits communication with rtc on each read and returns 0 in case of failure
DateTime getRTCDateTime() {
  //return SECONDSOFFSET + (millis() / 1000);
  #ifdef DEBUG_MODE
  if (!rtc.begin()) {
    return DateTime((uint32_t) 0);
  }
  #endif
  return rtc.now();

}

//sends a pulse for to the motor for the given duration and polarity 
//and updates next polarity in eepromData
//actual motor pulse won't exceed what the 555 timer in the circuit allows
void sendPulse(EepromData & eepromData, int pulseDurationMillis) {

  int polarity = eepromData.nextPulsePolarity != 0 ? eepromData.nextPulsePolarity : 1;

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

//returns the DST adjusted value of the given DateTime
DateTime dstAdjustedTime(DateTime time) {
  return (isDST(time) ? time + TimeSpan(secsPerMinute * 60) : time);
}





void loop() {

  //self reset every week, just in case I f*cked up and some variable would overflow left unchecked
  if (millis() >= 604800000UL) {
    asm volatile("  jmp 0");
  }

  bool motorPulseEnable = digitalRead(motorPulseEnablePin) == HIGH;

  //RTCDateTime is the time coming from the battery backed Real Time Clock Module
  DateTime RTCDateTime = getRTCDateTime();
  //EEPromTimeData is the last recorded time, polarity and no of writes on the current eeprom page  that was pulsed to the moto

  EepromData eepromData;
  readEepromData(eepromData);
  //if EEprom has no valid time set, skip clock movement and notify user
  if (!eepromData.dateTime.isValid()) {
    lcd.setCursor(0, 0);
    lcd.print("Adj. time");
    Serial.println("No valid time set in eeprom, please adjust clock");
    blinkFeedbackLed(300, 50, 5);
  } else {
    //regular operation

    //DST adjusted version of RTCDateTime
    DateTime dstAdjRTCDateTime = dstAdjustedTime(RTCDateTime);
    bool dst = isDST(RTCDateTime);

    //if the motor enable signal is turned off, don't calculate/send pulses
    if (motorPulseEnable) {
      
      //if actual time is more than one minute ahead of what has been already pulsed to the motor, shoot one pulse
      //pace pulses every secsBetweenPulses seconds to avoid straining the flip clock
      long rtcEepromTimeDiffSeconds=(dstAdjRTCDateTime - eepromData.dateTime).totalseconds();
      if (((rtcEepromTimeDiffSeconds > secsPerMinute) && ((RTCDateTime - lastPulseTime).totalseconds() > secsBetweenPulses))) {
        if(rtcEepromTimeDiffSeconds/secsPerMinute>=MAX_CATCHUP_MINUTES)
        {
          Serial.println("More than "+String(MAX_CATCHUP_MINUTES)+" minutes to catch up, pausing till next day");
          eepromData.dateTime = eepromData.dateTime + TimeSpan(SECONDS_PER_DAY);
          eepromData.pausedTillNextDay=true;
          writeTimeDataToEeprom(eepromData);
        }
        else
        {
         
          //shoot a pulse
          //TODO make this a pulse to a 555
          sendPulse(eepromData, pulseDurationMillis);
          //record the last pulsed time, advance by one minute
          eepromData.dateTime = eepromData.dateTime + TimeSpan(secsPerMinute);
          eepromData.pausedTillNextDay=false;
          writeTimeDataToEeprom(eepromData);

          lastPulseTime = RTCDateTime;
        }
      }
      if (eepromData.pausedTillNextDay)
      {
        //signal I'm waiting for next day
        blinkFeedbackLed(300, 50, 2);
      }
    }

    pulseDebugStringToDisplay(eepromData, RTCDateTime, motorPulseEnable, dst);
  }
  //handle inputs from pushbutton
  //brief press: advance flip clock by one minute
  //3 seconds press: align EEPromTime to RTCDateTime, to be used after manual flip clock adjustment
  if (digitalRead(pushButtonPin) == HIGH) {

    if (pushButtonPressedMillis == 0) {
      pushButtonPressedMillis = millis();
    }
    //long prss
    else if (millis() - pushButtonPressedMillis >= 3000) {
      //this totals to 1 second
      blinkFeedbackLed(100, 100, 5);
      //sets the current Secs on the eeprom to the value coming from the RTC, considering DST, setting seconds to zero
      eepromData.dateTime = RTCDateTime - TimeSpan(RTCDateTime.second());
      eepromData.pausedTillNextDay = false;
      writeTimeDataToEeprom(eepromData);
      Serial.println("Manual Adjustment completed");
      pushButtonPressedMillis = 0;
    }
  }
  //short press
  else if (pushButtonPressedMillis > 0) {
    pushButtonPressedMillis = 0;
    blinkFeedbackLed(100, 0, 1);
    sendPulse(eepromData, pulseDurationMillis);
    eepromData.dateTime = eepromData.dateTime + TimeSpan(secsPerMinute);
    writeTimeDataToEeprom(eepromData);
    Serial.println("Sent a manual pulse");
  }
}