/*
Reccmmended Temps:
PLA: 160c
PETG:
ABS: 
*/


// include libraries
#include <Wire.h> //Comes with the arduino IDE
#include <LiquidCrystal_I2C.h> //https://github.com/johnrickman/LiquidCrystal_I2C
#include <ClickEncoder.h> //https://github.com/0xPIT/encoder
#include <TimerOne.h> //https://code.google.com/archive/p/arduino-timerone/downloads
 
LiquidCrystal_I2C lcd(0x27, 16, 2);
 
//menu initialization
int menuItem = 1; // menu items Set temp vs. start
int page = 1; //Main Menu Page vs. secondary page
 
//setting rotary encoder as not turned (up or down) or clicked (select)
volatile boolean up = false;
volatile boolean down = false;
volatile boolean select = false;
 
//initializing encoder
ClickEncoder *encoder;
int16_t last, value;
 
 
//Heating element and fan pins
const int heatElementPin = 47;
const int fanPin = 2;
 
//Thermocouple Pins
const int thermocoupleCSPin = 31;
const int thermocoupleSOPin = 33;
const int thermocoupleSCKPin = 35;
 
//Heating Team Variables
float set_temperature = 80; //initial temp
double temperature_read = 0.0;
float PID_error = 0;
float previous_error = 0;
float elapsedTime, Time, timePrev;
float PID_value = 0;
int button_pressed = 0;
int menu_activated=0;
float last_set_temperature = 0;
 
//PID constants
//////////////////////////////////////////////////////////
int kp = 90;   int ki = 30;   int kd = 80;
//////////////////////////////////////////////////////////
int PID_p = 0;    int PID_i = 0;    int PID_d = 0;
float last_kp = 0;
float last_ki = 0;
float last_kd = 0;
int PID_values_fixed = 0;
 
String state = "off"; //variable for heating vs. cooling vs. success (rolling out)
 
//Feed Team Variables
const int stepPin = 22;
const int dirPin = 23;
 
const int stepPin2 = 24;
const int dirPin2 = 25;
const int stepsprev = 200;
 
//Stepper motor counters for cooling and success states
int cC = 0;
int cS = 0;
 
String roll = "off"; //variable for rolling in/out filaments
 
void setup() {
 
  encoder = new ClickEncoder(50, 53, 52);
  encoder->setAccelerationEnabled(false);
 
  lcd.init();
  lcd.begin(16, 2);
  lcd.backlight();
  lcd.clear();
  Serial.begin(9600);
 
  Timer1.initialize(1000);
  Timer1.attachInterrupt(timerIsr);
 
  last = encoder->getValue();
 
 //sets pins as either inputs or outputs
 pinMode(heatElementPin, OUTPUT);
 pinMode(fanPin, OUTPUT);
 pinMode(thermocoupleCSPin, OUTPUT);
 pinMode(thermocoupleSOPin, INPUT);
 pinMode(thermocoupleSCKPin, OUTPUT);
 pinMode(stepPin, OUTPUT);
 pinMode(dirPin, OUTPUT);
 pinMode(stepPin2, OUTPUT);
 pinMode(dirPin2, OUTPUT);
 
 TCCR2B = TCCR2B & B11111000 | 0x03;    // pin 3 and 11 PWM frequency of 928.5 Hz
 Time = millis();
 
}
 
void loop()
{
 //Reads value from thermocouple and saves to double temperature_read
 readThermocouple();
 
 //rotary encoder code
 readRotaryEncoder();
 
 //double click to control acceleration with steps of 5
 ClickEncoder::Button b = encoder->getButton();
 if (b != ClickEncoder::Open)
 {
   switch (b)
   {
      case ClickEncoder::Clicked:
         select=true;
         break;
      case ClickEncoder::DoubleClicked: 
         encoder->setAccelerationEnabled(!encoder->getAccelerationEnabled());
         break;
   }
 }
 
//calls draw menu method
drawMenu();
 
//when rotary encoder turned counterclockwise, up
if (up && page == 1 ) //main menu page switch menu item
{
    up = false; //resetting button
    menuItem--;
    delay(100);
    if (menuItem==0)
    {
      menuItem=2;
      delay(100);
    }
}
else if (up && page == 2 && menuItem == 1 && encoder->getAccelerationEnabled()==0 ) //on Set Temp page increasing temp, Set temp with acceleration
{
    up = false;
    set_temperature++;
    if (set_temperature > 200) //max temp 200c
    {
      set_temperature = 200;
    }
    // delay(100);
}
 
else if (up && page == 2 && menuItem == 1 && encoder->getAccelerationEnabled()==1) 
{
    up = false;
    set_temperature += 5;
    if (set_temperature > 200) //max temp 200c
    {
      set_temperature = 200;
    }
    // delay(100);
}
 
//when rotary encoder turned clockwise, down
if (down && page == 1) //main menu page switch menu item
{
    down = false;
    menuItem++;
    delay(100);
    if (menuItem==3)
    {
      menuItem=1;
      delay(100);
    }      
}
else if (down && page == 2 && menuItem == 1 && encoder->getAccelerationEnabled()==0 ) //on Set Temp page decreasing temp 
{
    down = false;
    set_temperature--;
    if (set_temperature < 0) //min temp 0c
    {
      set_temperature = 0;
    }
    // delay(100);
}
 
else if (down && page == 2 && menuItem == 1 && encoder->getAccelerationEnabled()==1) 
{
    down = false;
    set_temperature -= 5;
    if (set_temperature < 0) //min temp 0c
    {
      set_temperature = 0;
    }
    // delay(100);
}
 
//when rotary encoder clicked, select
if (select && page == 1 && menuItem == 1) //changing to secondary page for Set Temp
{  
    select = false;
    page=2;
    delay(100);
}
else if (select && page == 1 && menuItem == 2) //clicking start, begin whole process on secondary page 
{
    select = false;
    page = 2;
    if (roll.equals("off"))//only when starting new process after full cycle bc you placed new filaments in manually
    {
    roll = "idle"; //roll is in a standby state while heating
    }
  }
else if (select && page == 2 && menuItem == 1) //changing from secondary page to main menu for Set Temp
{
    select = false;
    page=1;
    delay(100);
 }
else if (select && page == 2 && menuItem == 2) //click out from Start secondary page to main menu at any point in the process
 {  
     readThermocouple();
     select = false;
     page=1;
     menuItem = 1;
     state = "off";
     roll = "off";
}
 
if(state.equals("off")) // when off before you click start or after
{
  cC=0;
  cS=0;
  heatingOff();
  analogWrite(fanPin, 0);
}  
 
if(roll.equals("idle")) //rolling out filaments initially before heating
{
   state = "heat";
}
 
if(state.equals("heat")) //heating, will roll in filaments to fuse once temp high enough
 {
    heat();
    if(temperature_read >= (set_temperature))
    {
      
    roll = "in"; //rolling filaments back in
    state = "cool"; //go straight to cooling
   
    }
 }
 
if(roll.equals("in")) //rolling in filaments to fuse, then cooling
{ 
 
  if(cC==0)
  {
  rollingInOpp();
  cC++;
  }
  delay (1000);
  rollingOsc1();
  rollingOsc2();
  state = "cool";
}
 
if(state.equals("cool")) //cooling, will switch to success once temp low enough
{
   //heating element off, fans on
   cool();
   if(temperature_read <= (set_temperature-10))
   {
  state = "success";
   }
}
 
if(state.equals("success")) //success, everything off and roll out
{ analogWrite(fanPin, 255);
  heatingOff();
  if(cS==0)
  {
    rollingOutSame();
    cS++;
  }
  roll = "off"; //needed to differentiate between process being complete vs cutting off process to restart with fused filament
 
}
}
 
//method to draw menu
void drawMenu() {
  //in the main menu Set Temp is selected
  if (page == 1 && menuItem == 1) {
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print(">Set temp");
    lcd.setCursor(0,1);
    lcd.print("Start");
  }
  //in the main menu Start is selected
  else if (page == 1 && menuItem == 2) {
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("Set temp");
    lcd.setCursor(0,1);
    lcd.print(">Start");
  }
//when Start is clicked, the temperature is shown and phase of the process is stated
  else if (page == 2 && menuItem == 2) {
    //(heating, cooling, success, etc)
    if (state.equals("heat")){
       readThermocouple();
       lcd.clear();
       lcd.setCursor(0,0);
       lcd.print(temperature_read);
       lcd.print(" C");
       lcd.setCursor(0,1);
       lcd.print("Heating");
    }
   
    else if(state.equals("cool")){
       readThermocouple();
       lcd.clear();
       lcd.setCursor(0,0);
       lcd.print(temperature_read);
       lcd.print(" C");
       lcd.setCursor(0,1);
       lcd.print("Cooling");
    }
    else if(state.equals("success")){
       lcd.clear();
       lcd.setCursor(0,0);
       lcd.print("Success! ");
       lcd.print(temperature_read);
       lcd.print(" C");
       lcd.setCursor(0,1);
       lcd.print("Click to exit");
    }
  }
 
  //when change temp is selected, desired temp is shown
  else if (page == 2 && menuItem == 1) {
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("Set Temp: ");
    lcd.setCursor(0,1);
    lcd.print(set_temperature);
    lcd.print(" C");
  }
 
  //delay(500);            
  //lcd.clear();
}
 
 
 
void readRotaryEncoder()
{
  value += encoder->getValue();
 
  if (value/2 < last) {
    last = value/2;
    down = true;
    delay(1);
  }else   if (value/2 > last) {
    last = value/2;
    up = true;
    delay(1);
  }
}
 
 
void timerIsr() {
  encoder->service();
}
 
void readThermocouple()
{
 uint16_t v;
 
 digitalWrite(thermocoupleCSPin, LOW);
 delay(2);
 
 // Read in 16 bits,
 //  15    = 0 always
 //  14..2 = 0.25 degree counts MSB First
 //  2     = 1 if thermocouple is open circuit
 //  1..0  = uninteresting status
  v = shiftIn(thermocoupleSOPin, thermocoupleSCKPin, MSBFIRST);
 v <<= 8;
 v |= shiftIn(thermocoupleSOPin, thermocoupleSCKPin, MSBFIRST);
  digitalWrite(thermocoupleCSPin, HIGH);
  delay(200);
 if (v & 0x4)
 {  
   // Bit 2 indicates if the thermocouple is disconnected
   temperature_read = NAN;    
 }
 // The lower three bits (0,1,2) are discarded status bits
 v >>= 3;
 // The remaining bits are the number of 0.25 degree (C) counts
 
 temperature_read = v*0.25;
}
 
void heat()
{
  //PID Control Calculations
 ///////////////////////////////////////////////////////
 //Error between set and read temperature
 PID_error = (set_temperature+5) - temperature_read; // added 5 to set_temp bcs error before was not allowing temp to reach set_temp, this should account for that
 //Calculate P value
 PID_p = 0.01*kp * PID_error;
 //Calculate I value
 PID_i = 0.01*PID_i + (ki * PID_error);
 
 //For derivative, time is needed to calculate rate of change of speed
 timePrev = Time;                          
 Time = millis();                    
 elapsedTime = (Time - timePrev) / 1000;
 //Calculate D value
 PID_d = 0.01*kd*((PID_error - previous_error)/elapsedTime);
 
 //Total PID value is the sum of P + I + D
 PID_value = PID_p + PID_i + PID_d;
 if(PID_value < 0)
 {    PID_value = 0;    }
 if(PID_value > 255)
 {    PID_value = 255;  }
 ///////////////////////////////////////////////////////
 
//heating element PWM at 255 is off
analogWrite(heatElementPin, 255-PID_value);
analogWrite(fanPin, 0);
}
 
void cool()
{
   analogWrite(heatElementPin, 255);
   analogWrite(fanPin, 255);
}
 
void heatingOff()
{
   analogWrite(heatElementPin, 255);
}
 
void rollingInOpp() //rolling the 2 filaments back in in opposite directions to fuse together
{
 feed(5000, 0.25, 1.0, 1.0);
}
 
void rollingOsc1() //Oscillations for while the filament cools to reduce it getting stuck
{
 feed(2500, 0.25, 1.0, 0.0);
}
void rollingOsc2() //Oscillations for while the filament cools to reduce it getting stuck
{
 feed(2500, 0.25, 0.0, 1.0);
}
void rollingOutSame() //rolling the fused filament out in the same direction
{
 feed(15000, 3, 1.0, 0.0);
}
 
void feed(double time_del, double rotations, double dir1, double dir2){
  digitalWrite(dirPin,dir1);
  digitalWrite(dirPin2,dir2);
 
  for (int i = 0; i < (rotations*200); i++) {
  digitalWrite(stepPin, HIGH);
  delayMicroseconds(time_del);
  digitalWrite(stepPin, LOW);
  delayMicroseconds(time_del);
 
  digitalWrite(stepPin2, HIGH);
  delayMicroseconds(time_del);
  digitalWrite(stepPin2, LOW);
  delayMicroseconds(time_del);
  }
}
