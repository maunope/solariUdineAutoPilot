//finire di verificare funzionamento orae

//#define SECONDSOFFSET 1731283200 //11 Novembre 2024 00:00
//#define SECONDSOFFSET 1722470400 //1 Agosto 2024 00:00
//#define SECONDSOFFSET 0
#define SECONDSOFFSET (1711850400-5) //5 secondi prima del subentro ora legale 2024
//#define SECONDSOFFSET (1729994400-5) //5 secondi prima della fine di  ora legale 2024



//#define FASTDEBUG


#include <LiquidCrystal_I2C.h>

#include <TimeLib.h>


const int generalEnablePin = 6; //Pin per arresto movimento
const int motorPulseUpPin = 7; //Pin per invio impulso positivo
const int motorPulseDownPin = 8; //Pin per invio impulso negativo
const int buttonPin = 9; //pin per comandi
const int feedbackLedPin = 4; // pin per il led feedback esito comandi

unsigned long pressedTime = 0;
 
int secsPerMinute = 60;
unsigned long delayBetweenPulses = 3;
int pulseduration=300;






int lastGeneralEnableStatus = 1;

int lastPulseDirection = 0;

time_t mockEEpromTime = 0;

time_t lastPulseTime = 0;

LiquidCrystal_I2C lcd(0x27, 16, 2);

void setup() {

  #ifdef FASTDEBUG
  secsPerMinute = 60;
  delayBetweenPulses = 0;
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

  
  //fordubug, simula messa in fase manuale, rimuovere
  setEEpromTime(floor(getRTMTime() / secsPerMinute) * secsPerMinute);

  lcd.init(); // initialize the lcd
  lcd.backlight(); // Turn on backlight
  lcd.setCursor(0, 1); // set the cursor to column 0, line 1
  lcd.print("                "); // Print a message to the LC
  lcd.setCursor(0, 2); // set the cursor to column 0, line 1
  lcd.print("                "); // Print a message to the LC
}

// Function to calculate the DST start and end dates for Central Europe
bool isDST(time_t currentTime) {

  time_t dstStart, dstEnd;

  // Calculate the last Sunday of March
  tmElements_t startTm;
  startTm.Year = year(currentTime) - 1970;
  startTm.Month = 3;
  startTm.Day = 31; // Start with the last day of March
  startTm.Hour = 2; // 2 AM because the change happens at night
  startTm.Minute = 0;
  startTm.Second = 0;
  time_t tempTime = makeTime(startTm);
  while (weekday(tempTime) != 1) { // 1 represents Sunday
    tempTime -= SECS_PER_DAY;
  }
  dstStart = tempTime;

  // Calculate the last Sunday of October
  tmElements_t endTm;
  endTm.Year = year(currentTime) - 1970;
  endTm.Month = 10;
  endTm.Day = 31; // Start with the last day of October
  endTm.Hour = 3; // 3 AM because the change happens at night
  endTm.Minute = 0;
  endTm.Second = 0;
  tempTime = makeTime(endTm);
  while (weekday(tempTime) != 1) { // 1 represents Sunday
    tempTime -= SECS_PER_DAY;
  }
  dstEnd = tempTime;
  //Serial.println(String(currentTime)+" "+String(dstStart)+" "+String(dstEnd));
  return (currentTime >= dstStart && currentTime <= dstEnd);
}

void pulseDebugStringToDisplay(time_t EEpromTime, time_t RTMTime, int pulseDirection, bool generalEnable, bool isDST) {

  /*
  lcd.setCursor(0, 0);// set the cursor to column 0, line 1
  lcd.print("                ");// Print a message to the LC
  lcd.setCursor(0, 1);// set the cursor to column 0, line 1
  lcd.print("                ");// Print a message to the LC
*/

  lcd.setCursor(0, 0);
  String RTMTimeString=String(RTMTime % 3600);
  for (int i=RTMTimeString.length();i<5;i++)
  {
    RTMTimeString=RTMTimeString+" ";
  }
  lcd.print("R:" + RTMTimeString);
  lcd.setCursor(7, 0);
  lcd.print(String(pulseDirection == 0 ? "/" : (pulseDirection > 0 ? "+" : "-")) + " " + String(generalEnable ? "E" : "D"));
  lcd.setCursor(0, 1);
  String EEpromTimeString=String(EEpromTime % 3600);
  for (int i=EEpromTimeString.length();i<5;i++)
  {
    EEpromTimeString=EEpromTimeString+" ";
  }
  lcd.print("E:" + EEpromTimeString);
  lcd.setCursor(7, 1);
  lcd.print(isDST ? "DST" : "STD");
}

//mock
time_t getRTMTime() {
  return SECONDSOFFSET + (millis() / 1000);
}

time_t getEEpromTime() {
  return mockEEpromTime;
}

int setEEpromTime(time_t secs) {
  mockEEpromTime = secs;
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

time_t dstAdjustedTime(time_t inputTime)
{
  return (isDST(inputTime)?inputTime+secsPerMinute*60:inputTime);
}

void loop() {

  //self reset every week, 
  if (millis() >= 604800000UL) { 
    // Perform a software reset
    asm volatile ("  jmp 0"); 
  }


  bool generalEnable = digitalRead(generalEnablePin) == HIGH;

  time_t RTMTime = getRTMTime();

  time_t EEpromTime = getEEpromTime();
  time_t dstAdjRTMTime= dstAdjustedTime(RTMTime);
  bool dst = isDST(RTMTime);
  
 
  if (generalEnable) {
     //Serial.println("RTM time="+String(RTMTime)+" DST Adjusted RTMTime="+ String(dstAdjRTMTime) + " EEpromTime=" + String(EEpromTime) + " diff:" + String((long)(dstAdjRTMTime - EEpromTime)) + " lastPulseTime=" + String(lastPulseTime) + " now=" + String(RTMTime) + " diff=" + String(RTMTime - lastPulseTime));
    if (((long)(dstAdjRTMTime-EEpromTime)  > secsPerMinute) && ((RTMTime - lastPulseTime) > delayBetweenPulses)) {
      //shoot a pulse
      //TODO make this a pulse to a 555
      Serial.println("Sending a pulse, RTM time="+String(RTMTime)+" DST Adjusted RTMTime=" + String(dstAdjRTMTime) + " EEpromTime=" + String(EEpromTime) + " diff:" + String((long)(dstAdjRTMTime - EEpromTime)) + " lastPulseTime=" + String(lastPulseTime) + " now=" + String(RTMTime) + " diff=" + String(RTMTime - lastPulseTime));
      int lastPulseDirection = getLastPulseDirection();
      sendPulse(lastPulseDirection);
      setLastPulseDirection(lastPulseDirection > 0 ? -1 : 1);

      setEEpromTime(EEpromTime + secsPerMinute);
      lastPulseTime = RTMTime;

    }
    pulseDebugStringToDisplay(EEpromTime, RTMTime, lastPulseDirection, generalEnable, dst);
  } else {
    pulseDebugStringToDisplay(EEpromTime, RTMTime, lastPulseDirection, generalEnable, dst);
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

      //sets the current Secs on the eeprom to the value coming from the RTM, considering DST 
      setEEpromTime(floor(dstAdjustedTime(getRTMTime()) / secsPerMinute) * secsPerMinute);

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
      setEEpromTime(EEpromTime + secsPerMinute);
    }
  }

  //for debug, run every second
  //delay(100);
}