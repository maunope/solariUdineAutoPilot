#include <LiquidCrystal_I2C.h>         //https://github.com/johnrickman/LiquidCrystal_I2C
#include <LowPower.h>                  //https://github.com/rocketscream/Low-Power
#include <RTClib.h>                    //https://github.com/adafruit/RTClib
#include <Regexp.h>                    //https://github.com/nickgammon/Regexp
#include <SparkFun_External_EEPROM.h>  //https://github.com/sparkfun/SparkFun_External_EEPROM_Arduino_Library


#define DEBUG_MODE

//never leave this flag on on regular operation, it pretty much defeats the purpose of using this board 
//#define SET_COMPILE_TIME_TO_RTC

//struct for the EEProm index page
struct EepromIndexDescriptor {
  unsigned long signature;
  int pageOffset = 0;
};

// struct for clock status to be saved to the eeprom
struct EepromData {
  //initializing at WINT_MAX ensures that if the conrtoller is run without further initialization
  //it won't send pulses
  DateTime dateTime = DateTime(WINT_MAX);
  int nextPulsePolarity = 0;
  int currentWrites = 0;
  bool pausedTillNextDay = false;
};


const unsigned long EEPROM_SIGNATURE = 0xD24F789F;
const int EEPROM_PAGE_SIZE_BYTES = 64;
const int MAX_WRITE_PER_EEPROM_PAGE = 10080;  // approx one week of regular operation
const int EEPROM_SIZE_BYTES = 32768;
const int EEPROM_TYPE = 256;
const int EEPROM_ADDRESS_BYTES = 2;
// to avoid straining the clock, if time is more than 120 seconds off will
// remain still until the next day
const int MAX_CATCHUP_MINUTES = 120;
const int SEC_IN_MINUTE = 60;
const int NO_SLEEP_AFT_COMM_SECS= 10;
const int PUSH_BUTTON_PIN = 0;
const int MOTOR_PULSE_ENABLE_PIN = 6;
const int MOTOR_PULSE_UP_PIN = 7;
const int MOTOR_PULSE_DOW_PIN = 8;
const int FEEDBACK_LED_PIN = 9;
const int WAIT_NEXT_PULSE_CYCLE_PIN = 10;
// duration of motor pulses, note shorter between circuit 555 timer and this
// applies
const int PULSE_DURATION_MILLIS = 400;
// minimun delay between pulses, overridden by manual advances. real Solari
// Udine controllers use 4s
const int SECS_BETWEEN_PULSES = 3;


DateTime bootTime = DateTime((unsigned long)0);
DateTime lastCommandReceived = DateTime((unsigned long)0);
// when true the clock will not self adjust until  RTC times is one full day
// ahead, then eeprom time will be bumped 24hrs, within MAX_CATCHUP_MINUTES, and
// self adjustment will take place
bool pausedTillNextDay = false;
// tracks last pulse sent to motor, to enforce max pace set by SECS_BETWEEN_PULSES
DateTime lastPulseTime = DateTime((uint32_t)0);
// these two are true when a pulse was delayed to way until the hardware pulse
// time protection has cycled
bool bookedPulseManual = false;
bool bookedPulseAuto = false;
// how long the push button has been pressed
unsigned long pushButtonPressedMillis = 0;



//Optional LCD Display
LiquidCrystal_I2C lcd(0x27, 16, 2);
//Real Time Clock Module
RTC_DS3231 rtc;
//External eeprom to save current clock status
ExternalEEPROM extEeprom;


void setup() {

  // Init pins
  pinMode(MOTOR_PULSE_ENABLE_PIN, INPUT);
  pinMode(MOTOR_PULSE_UP_PIN, OUTPUT);
  pinMode(MOTOR_PULSE_DOW_PIN, OUTPUT);
  pinMode(FEEDBACK_LED_PIN, OUTPUT);
  pinMode(PUSH_BUTTON_PIN, INPUT);
  pinMode(WAIT_NEXT_PULSE_CYCLE_PIN, INPUT);
  digitalWrite(MOTOR_PULSE_UP_PIN, LOW);
  digitalWrite(MOTOR_PULSE_DOW_PIN, LOW);
  digitalWrite(FEEDBACK_LED_PIN, LOW);

  // Init Serial and i2c modules
  Serial.begin(9600);

  lcd.init();
  lcd.backlight();
  lcd.clear();

  // check if the eeprom is available and initialize its parameters, check for
  // valid time in the eeprom is repeated in the loop
  if (!extEeprom.begin()) {
    debugMessage("External eeprom Error");
    blinkFeedbackLed(300, 300, 30);
    // reset
    asm volatile("  jmp 0");
  }
  extEeprom.setMemoryType(EEPROM_TYPE);
  extEeprom.setMemorySizeBytes(EEPROM_SIZE_BYTES);
  extEeprom.setAddressBytes(EEPROM_ADDRESS_BYTES);  // Set address bytes and page size after  MemorySizeBytes()
  extEeprom.setPageSizeBytes(EEPROM_PAGE_SIZE_BYTES);

  // Under no circumnstance read/write the RTC module when not ready!
  // if RTC is unavailabe sends a blink pattern and then resets
  if (!rtc.begin()) {
    debugMessage("RTC Error");
    blinkFeedbackLed(50, 300, 30);
    // reset
    asm volatile("  jmp 0");
  }
  
  

#ifdef SET_COMPILE_TIME_TO_RTC
  rtc.adjust(getStandardTime(DateTime(F(__DATE__), F(__TIME__))));
#endif

  //update boot time if it's not set
  if (bootTime == DateTime((unsigned long)0)) {
    bootTime = getRTCDateTime();
  }
}



// blinks the feedback led alternating given on/off time, for the given number
// of times.
void blinkFeedbackLed(int onMillis, int offMillis, int iterations) {
  for (int i = 0; i < iterations; i++) {
    digitalWrite(FEEDBACK_LED_PIN, HIGH);
    delay(onMillis);
    digitalWrite(FEEDBACK_LED_PIN, LOW);
    delay(offMillis);
  }
  digitalWrite(FEEDBACK_LED_PIN, LOW);
}


//printa a message to Serial port if in DEBUG_MODE
void debugMessage(String message) {
#ifdef DEBUG_MODE
  Serial.println(message);
#endif
}

// writes EepromData struct to the eeprom, spreading writes over pages to
// maximize the eeprom life. if the eeprom has no record of the current page, a
// new index page is created and writes restart from the first available page
int writeTimeDataToEeprom(EepromData& eepromData) {
  const int eepromDataPagesSizeBytes = ceil(sizeof(EepromData) / (float)EEPROM_PAGE_SIZE_BYTES) * EEPROM_PAGE_SIZE_BYTES;
  const int eepromIndexDescriptorSizeBytes = ceil(sizeof(EepromIndexDescriptor) / (float)EEPROM_PAGE_SIZE_BYTES) * EEPROM_PAGE_SIZE_BYTES;

  // debugMessage(String(sizeof(EepromData))+" a
  // "+String(eepromDataPagesSizeBytes)+" b
  // "+String((sizeof(EepromIndexDescriptor)))+" c
  // "+String(eepromIndexDescriptorSizeBytes));
  EepromIndexDescriptor eepromIndexDescriptor;
  eepromIndexDescriptor.signature = 0;

  // check if first 4 bytes contain the required signature string
  extEeprom.get(0, eepromIndexDescriptor);

  /*  debugMessage(String(EEPROM_SIGNATURE, HEX) +
      " eepromIndexDescriptor.signature: " +
      String(eepromIndexDescriptor.signature, HEX) +
      " eepromIndexDescriptor.pageOffset: " +
      String(eepromIndexDescriptor.pageOffset));*/
  // check if first 4 bytes contain the required signature string

  if (eepromIndexDescriptor.signature != EEPROM_SIGNATURE) {
    // EEPROM is not initialized, write the signature and default address and
    // set 0 writes in counter
    debugMessage("EEprom not initialized, setting index page");

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
    // debugMessage(String(currentEepromData.currentWrites) + " ->next page "
    // + String(eepromIndexDescriptor.pageOffset)); switch forward one page, if
    // we reached the top, start back from first page after index
    eepromIndexDescriptor.pageOffset =
      (eepromIndexDescriptor.pageOffset + eepromDataPagesSizeBytes < EEPROM_SIZE_BYTES)
        ? eepromIndexDescriptor.pageOffset + eepromDataPagesSizeBytes
        : eepromIndexDescriptorSizeBytes;
    eepromIndexDescriptor.signature = EEPROM_SIGNATURE;
    extEeprom.put(0, eepromIndexDescriptor);
    // reset writes counter in currentEepromData
    eepromData.currentWrites = 0;
    // debugMessage("New page offset: " +
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
int readEepromData(EepromData& eepromData) {
  EepromIndexDescriptor eepromIndexDescriptor;
  extEeprom.get(0, eepromIndexDescriptor);

  // check if first 4 bytes contain the required signature string
  if (eepromIndexDescriptor.signature != EEPROM_SIGNATURE) {
    debugMessage("EEprom is not initialized: got " + String(eepromIndexDescriptor.signature, HEX) + " instead of " + String(EEPROM_SIGNATURE, HEX));
    // EEPROM is not initialized, return 0
    eepromData.dateTime = WINT_MAX;
    eepromData.nextPulsePolarity = 0;
    return -1;  // Indicate that EEPROM is not initialized
  }

  EepromData timeDataFromEEprom;
  extEeprom.get(eepromIndexDescriptor.pageOffset, eepromData);
  return 1;  // Data read successfully
}

// returns true if give DateTime is DST, applies Central Europe rules
bool isDST(DateTime time) {
  // Calculate the last Sunday of March
  DateTime dstStart = DateTime((String(time.year()) + "-03-31%02:00:00").c_str());
  //debugMessage(String("abba ")+String(dstStart.dayOfTheWeek()));
  while (dstStart.dayOfTheWeek() != 0) {  // 1 represents Sunday
    dstStart = dstStart - TimeSpan(SECONDS_PER_DAY);
  }
  // Calculate the last Sunday of October
  DateTime dstEnd = DateTime((String(time.year()) + "-10-31%02:00:00").c_str());
  while (dstEnd.dayOfTheWeek() != 0) {  // 1 represents Sunday
    dstEnd = dstEnd - TimeSpan(SECONDS_PER_DAY);
  }
  //debugMessage("idstmethod time: "+time.timestamp()+" dststart: "+dstStart.timestamp()+" dstEnd: "+dstEnd.timestamp() +" isDST: "+String((time >= dstStart && time <= dstEnd)));
  return (time >= dstStart && time <= dstEnd);
}

// reads  time form rtc module. RTC always container standard time, DST is
// applied during runtime to be called After RTC module initiation only! in
// debug mode, inits communication with rtc on each read and returns 0 in case
// of failure.
DateTime getRTCDateTime() {
// return SECONDSOFFSET + (millis() / 1000);
#ifdef DEBUG_MODE
  if (!rtc.begin()) {
    return DateTime((uint32_t)0);
  }
#endif
  return rtc.now();
}

// given a DateTime, returns its winter time
DateTime getStandardTime(DateTime time) {
  return (isDST(time) ? time - TimeSpan(SEC_IN_MINUTE * 60) : time);
}
// returns the DST adjusted value of the given DateTime
DateTime getDSTAdjustedTime(DateTime time) {
  return (isDST(time) ? time + TimeSpan(SEC_IN_MINUTE * 60) : time);
}

// sends a pulse for to the motor for the given duration and polarity
// and updates next polarity in eepromData
// actual motor pulse won't exceed what the 555 timer in the circuit allows
void sendPulse(EepromData& eepromData, int PULSE_DURATION_MILLIS) {
  int polarity = eepromData.nextPulsePolarity != 0 ? eepromData.nextPulsePolarity : 1;

  digitalWrite(MOTOR_PULSE_UP_PIN, LOW);
  digitalWrite(MOTOR_PULSE_DOW_PIN, LOW);
  if (polarity > 0) {
    digitalWrite(MOTOR_PULSE_UP_PIN, HIGH);
    digitalWrite(MOTOR_PULSE_DOW_PIN, LOW);
  } else {
    digitalWrite(MOTOR_PULSE_UP_PIN, LOW);
    digitalWrite(MOTOR_PULSE_DOW_PIN, HIGH);
  }
  delay(PULSE_DURATION_MILLIS);

  digitalWrite(MOTOR_PULSE_UP_PIN, LOW);
  digitalWrite(MOTOR_PULSE_DOW_PIN, LOW);
  eepromData.nextPulsePolarity = polarity > 0 ? -1 : 1;
}

// displays debug info on LCD, does not compensate DST, send compensated
// DateTime
void pulseDebugStringToDisplay(EepromData eepromData, DateTime dstAdjDateTime, bool motorPulseEnable, bool isDST) {
#ifdef DEBUG_MODE
  String dstAdjDateTimeString = dstAdjDateTime.timestamp();
  lcd.setCursor(0, 0);
  lcd.print("R:" + dstAdjDateTimeString.substring(dstAdjDateTimeString.indexOf("T") + 1));
  lcd.setCursor(12, 0);
  lcd.print(String(eepromData.nextPulsePolarity == 0 ? "/" : (eepromData.nextPulsePolarity > 0 ? "+" : "-")) + " " + String(motorPulseEnable ? "E" : "D"));
  lcd.setCursor(0, 1);
  String eepromDateTimeString = eepromData.dateTime.timestamp();
  lcd.print("E:" + eepromDateTimeString.substring(eepromDateTimeString.indexOf("T") + 1));
  lcd.setCursor(12, 1);
  lcd.print(isDST ? "D" : "W");
  lcd.setCursor(14, 1);
  lcd.print(eepromData.pausedTillNextDay ? "P" : "R");
#endif
}

// crude serial commands parser, please behave, there's 100 ways this can break
void parseSerialCommands(String command) {
  // match state object
  MatchState ms;
  ms.Target(command.c_str()) a
  debugMessage(command);
  if (ms.Match(">>DATETIME[0-9]+")) {
    int year = command.substring(10, 14).toInt();
    int month = command.substring(14, 16).toInt();
    int day = command.substring(16, 18).toInt();
    int hour = command.substring(18, 20).toInt();
    int minute = command.substring(20, 22).toInt();
    int second = command.substring(22, 24).toInt();
    DateTime manualDateTime = DateTime(year, month, day, hour, minute, second);

    //if you're dialing in serial comments, likely somehtign went wrong, checking RTC is answering just in case
    if (rtc.begin()) {
      rtc.adjust(manualDateTime);
      Serial.println("RTC module time adjusted");
    } else {
      Serial.println("Unable to set time, RTC not available");
    }
  } else if (command.equals(">>COMPILEDATETIME")) {
    if (rtc.begin()) {
      rtc.adjust(getStandardTime(DateTime(F(__DATE__), F(__TIME__))));
      Serial.println("RTC module time adjusted to sketch compile date time: (not DST compensated) " + getRTCDateTime().timestamp());
    } else {
      Serial.println("Unable to set time, RTC not available");
    }
  } else if (command.equals("<<RTCDATETIME")) {
    if (rtc.begin()) {
      DateTime RTCDateTime = getRTCDateTime();
      String RTCDateTimeString = RTCDateTime.timestamp();
      Serial.println(RTCDateTimeString.substring(RTCDateTimeString.indexOf("T") + 1));
    } else {
      Serial.println("Unable to get time, RTC not available");
    }
  } else if (command.equals("<<EEPROMDATA")) {
    EepromData eepromData;
    if (readEepromData(eepromData) > 0) {
      DateTime eepromDateTime = eepromData.dateTime;
      debugMessage(eepromDateTime.timestamp()
                   + String(eepromData.nextPulsePolarity > 0 ? "+" : "-") + " "
                   + String(isDST(eepromData.dateTime) ? "D" : "W") + " "
                   + String(eepromData.pausedTillNextDay ? "P" : "R")
                   + " wrOnPp: " + String(eepromData.currentWrites));
    } else {
      Serial.println("Unable to read eeprom data");
    }
  } else if (command.equals("<<COMPILETIME")) {
    Serial.println("Software compiled on: " + getStandardTime(DateTime(F(__DATE__), F(__TIME__))).timestamp());
  } else if (!command.equals("")){
    Serial.println("Unknown command: " + command);
  }
}


// - reset every week
// - check if serial commands were given
// - if automatic advancement is enabled, check eeprom time Vs. RTC module time
// and advance if needed
// - handle push button input
// - Go in deep sleep for a while if motor enable is ON 
void loop() {

  if (Serial.available() > 0) {
    String command = "";
    int i = 0;
    while (Serial.available() > 0 && i < 64) {
      char c = Serial.read();
      if (c == '\n') {
        break;  // Break when newline character is received
      }
      command += c;
      i++;
    }
    parseSerialCommands(command);
  }

  bool motorPulseEnable = digitalRead(MOTOR_PULSE_ENABLE_PIN) == HIGH;

  // RTCDateTime is the time coming from the battery backed Real Time Clock
  // module, always in standard time
  DateTime RTCDateTime = getRTCDateTime();
  // DST adjusted version of RTCDateTime
  DateTime dstAdjRTCDateTime = getDSTAdjustedTime(RTCDateTime);

  // self reset every week, just in case I f*cked up and some variable would
  // overflow left unchecked
  //Serial.println(String((RTCDateTime-bootTime).totalseconds())+" "+String(SECONDS_PER_DAY*7));
  if ((RTCDateTime - bootTime).totalseconds() >= SECONDS_PER_DAY * 7) {
    asm volatile("  jmp 0");
  }

  // EEPromTimeData is the last recorded time, polarity and no of writes on the
  // current eeprom page  that was pulsed to the moto
  EepromData eepromData;
  readEepromData(eepromData);
  // if EEprom has no valid time set, skip clock movement and notify user
  // debugMessage("serial "+String(eepromData.dateTime.unixtime())+" "+String(eepromData.pausedTillNextDay)+" "+String(eepromData.dateTime.isValid()));
  if (!eepromData.dateTime.isValid()) {
#ifdef DEBUG_MODE
    lcd.setCursor(0, 0);
    lcd.print("Adj. time");
#endif
    debugMessage("No valid time set in eeprom, please perform manual adjustment");

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
      // SECS_BETWEEN_PULSES seconds to avoid straining the flip clock

      // bookedPulse means a pulse (manual or automatd) has to be dealyed
      // because hardware protection cycle hadn't finished ther

      long rtcEepromTimeDiffSeconds = (dstAdjRTCDateTime - eepromData.dateTime).totalseconds();
      //Serial.println(RTCDateTime.timestamp()+" "+lastPulseTime.timestamp());
      if (((rtcEepromTimeDiffSeconds > SEC_IN_MINUTE) && ((RTCDateTime - lastPulseTime).totalseconds() > SECS_BETWEEN_PULSES)) || bookedPulseAuto) {
        debugMessage(String(rtcEepromTimeDiffSeconds) + " " + String(bookedPulseAuto));
        //check if we need to wait till tomorrow
        if ((rtcEepromTimeDiffSeconds / SEC_IN_MINUTE >= MAX_CATCHUP_MINUTES) && !bookedPulseAuto) {
          if (eepromData.pausedTillNextDay == false) {
            eepromData.pausedTillNextDay = true;
            writeTimeDataToEeprom(eepromData);
          }
          blinkFeedbackLed(300, 50, 2);
        } else {
          // shoot a pulse
          if (digitalRead(WAIT_NEXT_PULSE_CYCLE_PIN) == LOW) {
            sendPulse(eepromData, PULSE_DURATION_MILLIS);
            // record the last pulsed time, advance by one minute
            eepromData.dateTime = eepromData.dateTime + TimeSpan(SEC_IN_MINUTE);
            eepromData.pausedTillNextDay = false;
            writeTimeDataToEeprom(eepromData);
            lastPulseTime = RTCDateTime;
            bookedPulseAuto = false;
          } else {
            debugMessage("Delaying synchronization pulse");
            bookedPulseAuto = true;
          }
        }
      }
    }

    //debugMessage(String("DebugB ")+String(isDST(RTCDateTime))+" dstAdjRTCDateTime: "+String(dstAdjRTCDateTime.timestamp())+" RTCDateTime: "+String(RTCDateTime.timestamp()));
    pulseDebugStringToDisplay(eepromData, dstAdjRTCDateTime, motorPulseEnable, isDST(RTCDateTime));
  }
  // handle inputs from pushbutton
  // brief press: advance flip clock by one minute
  // 3 seconds press: align EEPromTime to RTCDateTime, to be used after manual
  // flip clock adjustment
  if (digitalRead(PUSH_BUTTON_PIN) == HIGH) {

    lastCommandReceived = RTCDateTime;
    //lastButtonPushMillis=millis();
    if (pushButtonPressedMillis == 0) {
      pushButtonPressedMillis = millis();
    }
    // long press
    else if (millis() - pushButtonPressedMillis >= 3000) {
      // this totals to 1 second
      blinkFeedbackLed(100, 100, 5);
      // sets the current Secs on the eeprom to the value coming from the RTC,
      // setting seconds to zero
      eepromData.dateTime = dstAdjRTCDateTime - TimeSpan(dstAdjRTCDateTime.second());
      eepromData.pausedTillNextDay = false;
      writeTimeDataToEeprom(eepromData);
      //this helps when debugging, clock will start self adjustig immediately
      lastPulseTime = dstAdjRTCDateTime;
      debugMessage("Manual Adjustment completed");
      delay(200);  //avoid bounces
      pushButtonPressedMillis = 0;
    }
  }
  // short press
  else if (pushButtonPressedMillis > 0 || bookedPulseManual) {
    debugMessage("Manual pulse requested");
    pushButtonPressedMillis = 0;
    if (digitalRead(WAIT_NEXT_PULSE_CYCLE_PIN) == LOW) {
      blinkFeedbackLed(100, 0, 1);
      sendPulse(eepromData, PULSE_DURATION_MILLIS);
      eepromData.dateTime = eepromData.dateTime + TimeSpan(SEC_IN_MINUTE);
      writeTimeDataToEeprom(eepromData);
      debugMessage("Sent a manual pulse " + String((bookedPulseManual ? "booked" : "")));
      bookedPulseManual = false;
    } else {
      debugMessage("Delaying manual pulse");
      bookedPulseManual = true;
    }
  }


//go to sleep in low power mode fo deepSleepSeconds
//doesn't sleeep for 20 seconds after a button press
//doesn't sleep if there's a delayed pulse to send out
//doesn't sleep for 2x pulse delay after a pulse was sent
//won't sleep if motorPulseEnable is off, this helps debugging :-)

//Serial.println(String(millis())+" "+String(lastButtonPushMillis)+" "+String(sleepDisableMillis)+" "+String((RTCDateTime - lastPulseTime).totalseconds())+" "+String(2*SECS_BETWEEN_PULSES)+" "+String(bookedPulseAuto)+" "+String(bookedPulseManual)+" "+String(motorPulseEnable));
#ifdef DEBUG_MODE
  //motorPulseEnable = digitalRead(MOTOR_PULSE_ENABLE_PIN) == HIGH;
  /*
    won't go to sleep if Motor enable is disabled AND:
      - more than 1.2*SECS_BETWEEN_PULSES has elapsed since last pulse (allows uninterrupted catchup)
      - mote than NO_SLEEP_AFT_COMM_SECS has elapsed since last command received (keeps interaction responsive)
      - there aren't  booked pulses to deliver
  */
  if (motorPulseEnable && (RTCDateTime - lastPulseTime).totalseconds() > (1.2 * SECS_BETWEEN_PULSES) && (RTCDateTime - lastCommandReceived).totalseconds() > NO_SLEEP_AFT_COMM_SECS && !(bookedPulseAuto || bookedPulseManual)) {
    
    
   // Terminate all I2C connections
    //Wire.end(); 
    
    // Set SDA and SCL pins to input pullup mode
   // pinMode(2, INPUT_PULLUP);
    //pinMode(3, INPUT_PULLUP);

    //this allows close to 95% sleep time, considering the loop takes 50ms, while retaining ease of use
    //don't change the modules left ON, or RTC will start to drift (tested on a knock off Arduino Leonardo w/ a chep *ss RS2321 bough off Amazon )
    //LowPower.idle(SLEEP_1S, ADC_ON, TIMER4_OFF, TIMER3_OFF, TIMER1_OFF, TIMER0_OFF, SPI_ON, USART1_OFF, TWI_ON, USB_OFF);
    LowPower.powerDown(SLEEP_4S, ADC_OFF, BOD_ON);
    //Wire.begin();
   // rtc.begin();
    if (digitalRead(MOTOR_PULSE_ENABLE_PIN) == LOW)  //If I've just come out of sleep
    {
      blinkFeedbackLed(200, 0, 1);
      //If we're here, hten the switch went from OFF to ON, which counts as a command
      lastCommandReceived = RTCDateTime;
    }
    delay(200);
    
    #ifdef DEBUG_MODE
    //this should never be reached
    lcd.init();
    lcd.clear();
    #endif
  }
#endif
}