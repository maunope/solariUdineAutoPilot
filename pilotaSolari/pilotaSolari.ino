//sei arrivato a "isdsttimeadjusted"



#include <LiquidCrystal_I2C.h>
#include <time.h>


const int generalEnablePin = 6; //Pin per arresto movimento
const int motorPulseUpPin = 7; //Pin per invio impulso positivo
const int motorPulseDownPin = 8; //Pin per invio impulso negativo
const int buttonPin = 9; //pin per comandi
const int feedbackLedPin = 4; // pin per il led feedback esito comandi




unsigned long pressedTime = 0;


const long delayBetweenPulses=3000; 
const long minute = 60000;

int lastGeneralEnableStatus =1;

int lastPulseDirection = 0;

long mockEEpromMillis=0;

long lastPulseTime=0;


LiquidCrystal_I2C lcd(0x27, 16, 2);



void setup() {
  pinMode(generalEnablePin, INPUT); 
  pinMode(motorPulseUpPin, OUTPUT); 
  pinMode(motorPulseDownPin, OUTPUT); 
  pinMode(feedbackLedPin, OUTPUT);
  pinMode(buttonPin, INPUT);

  digitalWrite(motorPulseUpPin, LOW); 
  digitalWrite(motorPulseDownPin, LOW);   

  Serial.begin(9600);

   //fordubug, simula messa in fase manuale, rimuovere
  setEEpromMillis(getRTMMillis());


  lcd.init(); // initialize the lcd
  lcd.backlight(); // Turn on backlight
  lcd.setCursor(0, 1);// set the cursor to column 0, line 1
  lcd.print("                ");// Print a message to the LC
  lcd.setCursor(0, 2);// set the cursor to column 0, line 1
  lcd.print("                ");// Print a message to the LC
}

// Helper function to determine the day of the week for a given date
int getDayOfWeek(int year, int month, int day) {
  // This uses Zeller's congruence algorithm
  int q = day;
  int m = (month < 3) ? month + 12 : month;
  int K = year % 100;
  int J = year / 100;
  int h = (q + ((13 * (m + 1)) / 5) + K + (K / 4) + (J / 4) + (5 * J)) % 7;
  return (h + 5) % 7; // Adjust to 0=Sunday, 1=Monday, etc.
}

// Function to calculate the DST start and end dates for Central Europe
void calculateDST(int year, int &dstStartMonth, int &dstStartDay, int &dstEndMonth, int &dstEndDay) {
  dstStartMonth = 3; // March
  dstEndMonth = 10;  // October

  // Calculate the last Sunday of March
  for (int day = 31; day >= 25; day--) {
    if (getDayOfWeek(year, dstStartMonth, day) == 0) { // Sunday
      dstStartDay = day;
      break;
    }
  }

  // Calculate the last Sunday of October
  for (int day = 31; day >= 25; day--) {
    if (getDayOfWeek(year, dstEndMonth, day) == 0) { // Sunday
      dstEndDay = day;
      break;
    }
  }
}




void pulseDebugStringToDisplay(long EEPromMillis, long RTMMillis, int pulseDirection, bool generalEnable)
{
  
  /*
  lcd.setCursor(0, 0);// set the cursor to column 0, line 1
  lcd.print("                ");// Print a message to the LC
  lcd.setCursor(0, 1);// set the cursor to column 0, line 1
  lcd.print("                ");// Print a message to the LC
*/

  lcd.setCursor(0, 0);
  lcd.print("R:"+String(RTMMillis/1000)+(RTMMillis/1000<100?" ":"")+" "+(pulseDirection==0?" ":(pulseDirection>0?"+":"-"))+" "+(generalEnable?"E":"D"));// Print a message to the LC
  lcd.setCursor(0, 1);      
  lcd.print("E:"+String(EEPromMillis/1000)+(EEPromMillis/1000<100?" ":""));
}

//mock
long getRTMMillis()
{
  return millis();
}

long getEEpromMillis()
{
  return mockEEpromMillis;
}

int setEEpromMillis(long millis)
{
    mockEEpromMillis=millis;
}

int setLastPulseDirection(int direction)
{
  lastPulseDirection=direction;
  return 1;
}

int getLastPulseDirection()
{
  return lastPulseDirection;
}


void sendPulse(int direction)
{
      digitalWrite(motorPulseUpPin, LOW); 
      digitalWrite(motorPulseDownPin, LOW); 
      if (direction>0)
      {
        digitalWrite(motorPulseUpPin, HIGH); 
        digitalWrite(motorPulseDownPin, LOW); 
      }
      else
      {
        digitalWrite(motorPulseUpPin, LOW); 
        digitalWrite(motorPulseDownPin, HIGH); 
      }
      delay(300); // 
      digitalWrite(motorPulseUpPin, LOW); 
      digitalWrite(motorPulseDownPin, LOW); 
}


void loop() {

  bool generalEnable=digitalRead(generalEnablePin) == HIGH;

  if (generalEnable and lastGeneralEnableStatus==0 )
  {
      lastGeneralEnableStatus=1;
       Serial.println("General enable is UP");
  }
  if (!generalEnable and lastGeneralEnableStatus==1 )
  {
      lastGeneralEnableStatus=0;
       Serial.println("General enable is DOWN");
  }
  


  long RTMMillis=getRTMMillis();
  long EEpromMillis=getEEpromMillis();

  if (generalEnable)
  {
    if ( (RTMMillis-EEpromMillis>minute)   && ((RTMMillis-lastPulseTime)>delayBetweenPulses) ) {
        //shoot a pulse
        //TODO make this a pulse to a 555
        Serial.println("Sending a pulse, RTMMillis="+String(RTMMillis)+ " EEpromMillis="+String(EEpromMillis)+" lastPulseTime="+String(lastPulseTime)+" now="+String(RTMMillis)+" diff="+String(RTMMillis-lastPulseTime));
        int lastPulseDirection=getLastPulseDirection();
        sendPulse(lastPulseDirection);
        setLastPulseDirection(lastPulseDirection>0?-1:1);

        setEEpromMillis(floor((EEpromMillis/minute)*minute)+minute);
        lastPulseTime=RTMMillis;

  

    }
    pulseDebugStringToDisplay(EEpromMillis, RTMMillis ,lastPulseDirection, generalEnable);
  }
  else
  {
       pulseDebugStringToDisplay(EEpromMillis, RTMMillis, 0, generalEnable);     
  }

 


    // Check if the button is pressed
  if (digitalRead(buttonPin) == HIGH) {
     
    // If it's pressed, record the time
    if (pressedTime == 0) {
      pressedTime = millis();
    } else if (millis() - pressedTime >= 3000) {


      for (int i=0;i<5;i++) //this totals to 1 second
      {
          digitalWrite(feedbackLedPin, HIGH);
          delay(100);
          digitalWrite(feedbackLedPin, LOW);
          delay(100);
      }
      
      //sets the current millis on the eeprom to the value coming from the RTM
     long RTMMillis=getRTMMillis();
     setEEpromMillis(floor(RTMMillis/minute)*minute);


     
      Serial.println("Messa in fase manuale");
      pressedTime = 0; // Reset the timer
    }
  } else {
    // If the button is released
    if (pressedTime > 0) {

      digitalWrite(feedbackLedPin, HIGH);
      delay(100);
      digitalWrite(feedbackLedPin, LOW);


        Serial.println("Sending a manual pulse");
        int lastPulseDirection=getLastPulseDirection();
        sendPulse(lastPulseDirection);
        setLastPulseDirection(lastPulseDirection>0?-1:1);
        EEpromMillis=getEEpromMillis();
        setEEpromMillis(floor(EEpromMillis/minute)*minute+minute);

      pressedTime = 0; // Reset the timer
    }
  }

  //for debug, run every second
  //delay(100);
}