//finire di verificare funzionamento orae



//#define SECONDSOFFSET 1731283200 //11 Novembre 2024 00:00
//#define SECONDSOFFSET 1722470400 //1 Agosto 2024 00:00
//#define SECONDSOFFSET 0
//#define SECONDSOFFSET (1711850400-5) //5 secondi prima del subentro ora legale 2024
//#define SECONDSOFFSET (1729994400-5) //5 secondi prima della fine di  ora legale 2024



#define DEBUG_MODE
//#define ALWAYS_DST
//#define NEVER_DST
//#define FAST_DEBUG


#include <LiquidCrystal_I2C.h>
#include <RTClib.h>
//#include <TimeLib.h>




const int generalEnablePin = 6; //Pin per arresto movimento
const int motorPulseUpPin = 7; //Pin per invio impulso positivo
const int motorPulseDownPin = 8; //Pin per invio impulso negativo
const int buttonPin = 9; //pin per comandi
const int feedbackLedPin = 4; // pin per il led feedback esito comandi

unsigned long pressedTime = 0;
 
int secsPerMinute = 60;
unsigned long secondsBetweenPulses = 3000;
int pulseduration=300;


int lastGeneralEnableStatus = 1;

int lastPulseDirection = 0;

DateTime mockEEpromTime = DateTime((uint32_t)0);

DateTime lastPulseTime = DateTime((uint32_t)0);

LiquidCrystal_I2C lcd(0x27, 16, 2);
RTC_DS3231 rtc;

void setup() {


  #ifdef FAST_DEBUG
    secsPerMinute = 60;
    secondsBetweenPulses = 0;
    pulseduration=10;
  #endif


  pinMode(generalEnablePin, INPUT);
  pinMode(motorPulseUpPin, OUTPUT);
  pinMode(motorPulseDownPin, OUTPUT);
  pinMode(feedbackLedPin, OUTPUT);
  pinMode(buttonPin, INPUT);

  digitalWrite(motorPulseUpPin, LOW);
  digitalWrite(motorPulseDownPin, LOW);

  Serial.begin(9600);


  lcd.init(); // initialize the lcd
  lcd.backlight(); // Turn on backlight
  lcd.setCursor(0, 1); // set the cursor to column 0, line 1
  lcd.print("                "); // Print a message to the LC
  lcd.setCursor(0, 2); // set the cursor to column 0, line 1
  lcd.print("                "); // Print a message to the LC



  if (!rtc.begin()) {
    lcd.setCursor(0, 1); // set the cursor to column 0, line 1
    lcd.print("RTC Error");
    Serial.println("RTC Error");
    delay(60000);
    asm volatile ("  jmp 0"); 
  }
  if (rtc.lostPower()) {
    Serial.println("Power lost, resetting time. (check battery maybe?");
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }
  //fordubug, simula messa in fase manuale, rimuovere
  setEEpromTime(DateTime(floor(getRTCTime().secondstime() / secsPerMinute) * secsPerMinute));

}

// Function to calculate the DST start and end dates for Central Europe
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

void pulseDebugStringToDisplay(DateTime EEpromTime, DateTime RTCTime, int pulseDirection, bool generalEnable, bool isDST) {
  lcd.setCursor(0, 0);

  lcd.print("R:"+String(RTCTime.hour()>10?"":"0")+String(RTCTime.hour())+":"+(RTCTime.minute()>10?"":"0")+String(RTCTime.minute())+":"+(RTCTime.second()>10?"":"0")+String(RTCTime.second()));
  lcd.setCursor(12, 0);
  lcd.print(String(pulseDirection == 0 ? "/" : (pulseDirection > 0 ? "+" : "-")) + " " + String(generalEnable ? "E" : "D"));
  lcd.setCursor(0, 1);
  lcd.print("E:"+String(EEpromTime.hour()>10?"":"0")+String(EEpromTime.hour())+":"+(EEpromTime.minute()>10?"":"0")+String(EEpromTime.minute())+":"+(EEpromTime.second()>10?"":"0")+String(EEpromTime.second()));
  lcd.setCursor(12, 1);
  lcd.print(isDST ? "DST" : "STD");
}


DateTime getRTCTime() {
  //return SECONDSOFFSET + (millis() / 1000);
  #ifdef DEBUG_MODE
   if (!rtc.begin()) {
    return DateTime((uint32_t)0);
  }
  #endif
  return rtc.now();

}

DateTime getEEpromTime() {
  return mockEEpromTime;
}

int setEEpromTime(DateTime time) {
  mockEEpromTime = time;
  return 1;
}

int setLastPulseDirection(int direction) {
  lastPulseDirection = direction;
  return 1;
}

int getLastPulseDirection() {
  return lastPulseDirection;
}

void sendPulse(int direction) {
  digitalWrite(motorPulseUpPin, LOW);
  digitalWrite(motorPulseDownPin, LOW);
  if (direction > 0) {
    digitalWrite(motorPulseUpPin, HIGH);
    digitalWrite(motorPulseDownPin, LOW);
  } else {
    digitalWrite(motorPulseUpPin, LOW);
    digitalWrite(motorPulseDownPin, HIGH);
  }
  delay(pulseduration); // 
  digitalWrite(motorPulseUpPin, LOW);
  digitalWrite(motorPulseDownPin, LOW);
}

DateTime dstAdjustedTime(DateTime time)
{
  return (isDST(time)?time+TimeSpan(secsPerMinute*60):time);
}

void loop() {

  //self reset every week, just in case I f*cked up and some variable would overflow left unchecked
  if (millis() >= 604800000UL) { 
    asm volatile ("  jmp 0"); 
  }


  bool generalEnable = digitalRead(generalEnablePin) == HIGH;

  


  DateTime RTCTime = getRTCTime();




   

  DateTime EEpromTime = getEEpromTime();
  DateTime dstAdjRTCTime= dstAdjustedTime(RTCTime);
  bool dst = isDST(RTCTime);
  
 
 
  if (generalEnable) {
     //Serial.println("RTC time="+String(RTCTime)+" DST Adjusted RTCTime="+ String(dstAdjRTCTime) + " EEpromTime=" + String(EEpromTime) + " diff:" + String((long)(dstAdjRTCTime - EEpromTime)) + " lastPulseTime=" + String(lastPulseTime) + " now=" + String(RTCTime) + " diff=" + String(RTCTime - lastPulseTime));
    //Serial.println(String((dstAdjRTCTime-EEpromTime).totalseconds()));
    if (((dstAdjRTCTime-EEpromTime).totalseconds()  > secsPerMinute) && ((RTCTime - lastPulseTime).totalseconds() > secondsBetweenPulses)) {
      //shoot a pulse
      //TODO make this a pulse to a 555
      //Serial.println("Sending a pulse, RTC time="+String(RTCTime)+" DST Adjusted RTCTime=" + String(dstAdjRTCTime) + " EEpromTime=" + String(EEpromTime) + " diff:" + String((long)(dstAdjRTCTime - EEpromTime)) + " lastPulseTime=" + String(lastPulseTime) + " now=" + String(RTCTime) + " diff=" + String(RTCTime - lastPulseTime));
      int lastPulseDirection = getLastPulseDirection();
      sendPulse(lastPulseDirection);
      setLastPulseDirection(lastPulseDirection > 0 ? -1 : 1);

      setEEpromTime(EEpromTime + TimeSpan(secsPerMinute));
      lastPulseTime = RTCTime;

    }
    pulseDebugStringToDisplay(EEpromTime, RTCTime, lastPulseDirection, generalEnable, dst);
  } else {
    pulseDebugStringToDisplay(EEpromTime, RTCTime, lastPulseDirection, generalEnable, dst);
  }

  // Check if the button is pressed
  if (digitalRead(buttonPin) == HIGH) {
    // If it's pressed, record the time
    if (pressedTime == 0) {
      pressedTime = millis();
    } else if (millis() - pressedTime >= 3000) {

     //Serial.println(String(millis()) + " " + String(pressedTime) + " " + String(millis() - pressedTime));
      for (int i = 0; i < 5; i++) //this totals to 1 second
      {
        digitalWrite(feedbackLedPin, HIGH);
        delay(100);
        digitalWrite(feedbackLedPin, LOW);
        delay(100);
      }

      //sets the current Secs on the eeprom to the value coming from the RTC, considering DST, setting seconds to zero
      setEEpromTime(DateTime((floor(dstAdjustedTime(getRTCTime()).secondstime()) / secsPerMinute) * secsPerMinute));

      Serial.println("Messa in fase manuale");
      pressedTime = 0; // Reset the timer
    }
  } else{
    // If the button is released
    if (pressedTime > 0) {

      pressedTime = 0; // Reset the timer
      digitalWrite(feedbackLedPin, HIGH);
      delay(100);
      digitalWrite(feedbackLedPin, LOW);

      Serial.println("Sending a manual pulse");
      int lastPulseDirection = getLastPulseDirection();
      sendPulse(lastPulseDirection);
      setLastPulseDirection(lastPulseDirection > 0 ? -1 : 1);
      setEEpromTime(EEpromTime + TimeSpan(secsPerMinute));
    }
  }



}