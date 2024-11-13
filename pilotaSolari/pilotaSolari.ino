#include <LiquidCrystal_I2C.h>
#include <RTClib.h>


//#define SECONDSOFFSET 1731283200 //11 Novembre 2024 00:00
//#define SECONDSOFFSET 1722470400 //1 Agosto 2024 00:00
//#define SECONDSOFFSET 0
//#define SECONDSOFFSET (1711850400-5) //5 secondi prima del subentro ora legale 2024
//#define SECONDSOFFSET (1729994400-5) //5 secondi prima della fine di  ora legale 2024

#define DEBUG_MODE
//#define ALWAYS_DST
//#define NEVER_DST
//#define FAST_DEBUG


const int motorPulseEnablePin = 6; 
const int motorPulseUpPin = 7; 
const int motorPulseDownPin = 8;
const int pushButtonPin = 9; 
const int feedbackLedPin = 4; 
int secsPerMinute = 60;


unsigned long pushButtonPressedMillis = 0; 


//last direction of the pulse, to be saved to EEPROM
int nextPulseDirection = 0;

//fordebug, remove
DateTime mockEEpromTime = DateTime((uint32_t)0);

//tracks last pulse sent to motor, to enforce max pace set by secsBetweenPulses
DateTime lastPulseTime = DateTime((uint32_t)0);
unsigned long secsBetweenPulses = 3;

//duration of motor pulses, note shorter between circuit 555 timer and this applies 
int pulseDurationMillis=300;

LiquidCrystal_I2C lcd(0x27, 16, 2);
RTC_DS3231 rtc;

void setup() {
  #ifdef FAST_DEBUG
    secsPerMinute = 60;
    secsBetweenPulses = 0;
    pulseDurationMillis=10;
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
    for (int i=0;i<30;i++)
    {
    digitalWrite(feedbackLedPin, HIGH);
    delay(50);
    digitalWrite(feedbackLedPin, LOW);
    delay(300);
    }
    //reset
    asm volatile ("  jmp 0"); 
  }

  //if the rtc module is new, sets time. this only makes sense when connected to a PC
  if (rtc.lostPower()) {
    Serial.println("Power lost, resetting time. (check battery maybe?");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }
  
  //REMOVE ONCE FINISHED
  //fordubug, simula messa in fase manuale, rimuovere
  DateTime rtctime=getRTCTime();
  setEEpromTime(rtctime-TimeSpan(rtctime.second()));

}

void blinkFeedbackLed(int onMillis, int offMillis, int iterations)
{
      for (int i = 0; i < iterations; i++) 
      {
        digitalWrite(feedbackLedPin, HIGH);
        delay(onMillis);
        digitalWrite(feedbackLedPin, LOW);
        delay(offMillis);
      }
      digitalWrite(feedbackLedPin, LOW);
}

//returns true if give DateTime is DST, applies Central Europe rules
bool isDST(DateTime time) {
  #ifdef DEBUG_MODE
    #ifdef ALWAYS_DST
    time=DateTime((String(time.year())+"-04-01%00:00:00").c_str());
    #endif
    #ifdef NEVER_DST
    time=DateTime((String(time.year())+"-11-01%00:00:00").c_str());
    #endif
  #endif
  // Calculate the last Sunday of March
  DateTime dstStart=DateTime((String(time.year())+"-03-31%02:00:00").c_str());
  while (dstStart.dayOfTheWeek() != 1) { // 1 represents Sunday
    dstStart =dstStart-TimeSpan(SECONDS_PER_DAY);
  }
    // Calculate the last Sunday of October
  DateTime dstEnd=DateTime((String(time.year())+"-10-31%03:00:00").c_str());
  while (dstEnd.dayOfTheWeek() != 1) { // 1 represents Sunday
    dstEnd =dstEnd-TimeSpan(SECONDS_PER_DAY);
  }
  return (time >= dstStart && time <= dstEnd);
}

//displays debug info on LCD
void pulseDebugStringToDisplay(DateTime EEpromTime, DateTime RTCTime, int pulseDirection, bool motorPulseEnable, bool isDST) {
  lcd.setCursor(0, 0);

  lcd.print("R:"+String(RTCTime.hour()<10?"0":"")+String(RTCTime.hour())+":"+(RTCTime.minute()<10?"0":"")+String(RTCTime.minute())+":"+(RTCTime.second()<10?"0":"")+String(RTCTime.second())+" ");
  lcd.setCursor(12, 0);
  lcd.print(String(pulseDirection == 0 ? "/" : (pulseDirection > 0 ? "+" : "-")) + " " + String(motorPulseEnable ? "E" : "D"));
  lcd.setCursor(0, 1);
  lcd.print("E:"+String(EEpromTime.hour()<0?"0":"")+String(EEpromTime.hour())+":"+(EEpromTime.minute()<10?"0":"")+String(EEpromTime.minute())+":"+(EEpromTime.second()<10?"0":"")+String(EEpromTime.second())+" ");
  lcd.setCursor(12, 1);
  lcd.print(isDST ? "DST" : "STD");
}

//reads  time form rtc module.
//to be called After initiation only! 
//in debug mode, inits communication with rtc on each read and returns 0 in case of failure
DateTime getRTCTime() {
  //return SECONDSOFFSET + (millis() / 1000);
  #ifdef DEBUG_MODE
   if (!rtc.begin()) {
    return DateTime((uint32_t)0);
  }
  #endif
  return rtc.now();

}

//mock method
DateTime getEEpromTime() {
  return mockEEpromTime;
}

//mock method
int setEEpromTime(DateTime time) {
  mockEEpromTime = time;
  return 1;
}

//mockmethod
int setNextPulseDirection(int direction) {
  nextPulseDirection = direction;
  return 1;
}

//mockmethod
int getNextPulseDirection() {
  return nextPulseDirection;
}


//sends a pulse for to the motor for the given duration and polarity 
//if 0 polarity is given, forces to positive
//actual motor pulse won't exceed what the 555 timer in the circuit allows
int sendPulse(int polarity, int pulseDurationMillis) {
  
 polarity=polarity!=0?polarity:1;

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
  return polarity > 0 ? -1 : 1;
}

//returns the DST adjusted value of the given DateTime
DateTime dstAdjustedTime(DateTime time)
{
  return (isDST(time)?time+TimeSpan(secsPerMinute*60):time);
}

void loop() {

  //self reset every week, just in case I f*cked up and some variable would overflow left unchecked
  if (millis() >= 604800000UL) { 
    asm volatile ("  jmp 0"); 
  }


  bool motorPulseEnable = digitalRead(motorPulseEnablePin) == HIGH;
  
  //RTCTime is the time coming from the battery backed Real Time Clock Module
  DateTime RTCTime = getRTCTime();
  //EEPromTime is the last recorded time that was pulsed to the moto
  DateTime EEpromTime = getEEpromTime();
  //DST adjusted version of RTCTime
  DateTime dstAdjRTCTime= dstAdjustedTime(RTCTime);
  bool dst = isDST(RTCTime);
  
  //if the motor enable signal is turned off, don't calculate/send pulses
  if (motorPulseEnable) {

    //if actual time is more than one minute ahead of what has been already pulsed to the motor, shoot one pulse
    //pace pulses every secsBetweenPulses seconds to avoid straining the flip clock
    if (((dstAdjRTCTime-EEpromTime).totalseconds()  > secsPerMinute) && ((RTCTime - lastPulseTime).totalseconds() > secsBetweenPulses)) {
      //shoot a pulse
      //TODO make this a pulse to a 555

      setNextPulseDirection(sendPulse(getNextPulseDirection(),pulseDurationMillis));
      //record the last pulsed time, advance by one minute
      setEEpromTime(EEpromTime + TimeSpan(secsPerMinute));
      lastPulseTime = RTCTime;

    }
    
  } 

  pulseDebugStringToDisplay(EEpromTime, RTCTime, getNextPulseDirection(), motorPulseEnable, dst);

  //handle inputs from pushbutton
  //brief press: advance flip clock by one minute
  //3 seconds press: align EEPromTime to RTCTime, to be used after manual flip clock adjustment
  if (digitalRead(pushButtonPin) == HIGH) {
    
    if (pushButtonPressedMillis == 0) {
      pushButtonPressedMillis = millis();
    } 
    //long prss
    else if (millis() - pushButtonPressedMillis >= 3000) {
      //this totals to 1 second
      blinkFeedbackLed(100,100,5);
      //sets the current Secs on the eeprom to the value coming from the RTC, considering DST, setting seconds to zero
      setEEpromTime(RTCTime-TimeSpan(RTCTime.second()));
      Serial.println("Manual Adjustment completed");
      pushButtonPressedMillis = 0; 
    }
  } 
  //short press
  else if (pushButtonPressedMillis > 0) {
      pushButtonPressedMillis = 0; 
      blinkFeedbackLed(100,0,1);

      setNextPulseDirection(sendPulse(getNextPulseDirection(),pulseDurationMillis));
      setEEpromTime(EEpromTime + TimeSpan(secsPerMinute));
      Serial.println("Sending a manual pulse");
  }
}
  



