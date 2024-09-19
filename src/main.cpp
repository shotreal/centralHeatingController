#include <Arduino.h>
#include <OpenTherm.h>
#include <Wire.h>
#include <LiquidCrystal_PCF8574.h>
#include <ESP8266WiFi.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <TimeLib.h>
#include <Timezone.h>
#include <ArduinoOTA.h>
#include <ArduinoHA.h>
#include "credentials.h" // Include credentials file

// Pin Definitions
const int inPin = 4;   // Input pin for OpenTherm
const int outPin = 5;  // Output pin for OpenTherm

// OpenTherm object
OpenTherm ot(inPin, outPin);

// Wi-Fi and NTP setup
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "de.pool.ntp.org", 0);

// LCD setup
LiquidCrystal_PCF8574 lcd(0x27);

// OTA setup
WiFiClient client;

// Version information
const char* version = "1.8.5";

// Response and request IDs
unsigned long request = 0;
int requestID = 0;

// Heating mode enumeration
enum HeatingMode {
  OTemp_AUTO,
  BOOST
};
HeatingMode heatingMode = OTemp_AUTO;

// Hot water mode enumeration
enum HotWaterMode {
  AUTOMATIC,
  MANUAL
};
HotWaterMode hotWaterMode = AUTOMATIC;

// Time of day enumeration
enum TimeOfDay {
  NIGHT,
  MORNING,
  DAY,
  EVENING
};
TimeOfDay timeOfDay = EVENING;
unsigned long lastSendTime = 0;
float daytime = 0.0;
float morningStart = 6.0;
float dayStart = 10.0;
float afternoonStart = 16.0;
float nightStart = 21.0;

// Day of the week and Legionella program day
int dayOfWeek = 1;
int legionellaProgramDay = 0;

// Flags for enabling various functions
bool enableHeatingProgram = true;
bool enableHotWaterProgram = true;
bool enableLegionellaProgram = true;

bool enableCentralHeating = false;
bool enableHotWater = false;
bool enableCooling = false;

// Flags indicating current system states
bool isEnabledCentralHeating = false;
bool isEnabledHotWater = false;
bool isEnabledFlame = false;

// Temperature thresholds
float heatingThreshold = 17.0;
float boilerTempSP = 0.0;
float boilerTempBoost = 55.0;

float dhwTemp = 0.0;
// Hot water temperature setpoints
float dhwTempSP = 0.0;
float dhwTempNightSP = 20.0;
float dhwTempMorningSP = 40.0;
float dhwTempEveningSP = 46.0;
float dhwTempDaySP = 35.0;
float dhwLegionellenSP = 72.0;
float dhwTempBoostSP = 50.0;

// Flags for forcing specific temperatures
bool dhwForceTemp = false;
bool heatForceTemp = false;

// Temperature readings
float outsideTemp = -4.0;
float returnWaterTemp = 0.0;
float boilerTemp = 0.0;
float flowRate = 0.0;
float exhaustTemp = 0.0;

// Temperature adjustment factors
float nightOffsetFactor = 1.0;  // No Night Offset
float tempShiftValue = -1.5;

// Temperature control parameters
float steepness = -0.5;
float zeroSetpoint = 43.0;

// Display-related variables
String state = "Error";
unsigned int data = 0xFFFF;
String wifiRSSI = " NC";
String timeString = "";
;

// Create a Timezone object for your specific time zone
TimeChangeRule myDST = { "DST", Last, Sun, Mar, 2, 120 };  // Daylight Saving Time rule
TimeChangeRule mySTD = { "STD", Last, Sun, Oct, 3, 60 };   // Standard Time rule
Timezone myTZ(myDST, mySTD);
unsigned long lastTimeUpdate = 0;

// Custom characters for LCD display
byte burningFire[8] = {
  B01110,
  B10001,
  B01110,
  B00100,
  B01010,
  B10001,
  B10001,
  B01110
};

// Stopped Burning Fire Symbol
byte stoppedFire[8] = {
  B00000,
  B01110,
  B10001,
  B00000,
  B00100,
  B01010,
  B10001,
  B01110
};

HADevice device;
HAMqtt mqtt(client, device, 22);


HASensorNumber HAOutsideTemp("hzg-tAussen", HASensorNumber::PrecisionP2);
HASensorNumber HABoilerTemp("hzg-tVorlauf", HASensorNumber::PrecisionP2);
HASensorNumber HAReturnWaterTemp("hzg-tRuecklauf", HASensorNumber::PrecisionP2);
HASensorNumber HAExhaustTemp("hzg-tAbgas", HASensorNumber::PrecisionP2);
HASensorNumber HADomesticHotWaterTemp("hzg-tBrauchwasser", HASensorNumber::PrecisionP2);

HANumber tSetDomesticHotWaterMorning("hzg-tSetWaterMorning", HANumber::PrecisionP0);
HANumber tSetDomesticHotWaterDay("hzg-tSetWaterDay", HANumber::PrecisionP0);
HANumber tSetDomesticHotWaterEvening("hzg-tSetWaterAfternoon", HANumber::PrecisionP0);
HANumber tSetDomesticHotWaterNight("hzg-tSetWaterNight", HANumber::PrecisionP0);
HANumber tSetDomesticHotWaterLegionella("hzg-tSetWaterKegionella", HANumber::PrecisionP0);
HANumber tSetDomesticHotWaterBoost("hzg-tSetWaterBoost", HANumber::PrecisionP0);
HANumber tSetBoilerBoostTemp("hzg-tSetBoilerBoostTemp", HANumber::PrecisionP0);

HASelect sMorningBegin("hzg-morningTime");
HASelect sDayBegin("hzg-Day");
HASelect sAfternoonBegin("hzg-AfternoonTime");
HASelect sNightBegin("hzg-NightTime");
HASelect sLegionellaDay("hzg-LegionellaDay");

// devices types go here
HASwitch boostSwitchHeating("hzg-Boost-Heizung");
HASwitch boostSwitchHotWater("hzg-Boost-Warmwasser");
HASwitch enableHeatingProgramSwitch ("hzg-Enable-Heating-Program");
HASwitch enableHotWaterProgramSwitch ("hzg-Enable-Hot-Water-Program");
HASwitch enableLegionellaProgramSwitch ("hzg-Enable-Legionella-Program");



// Callback for rocking the tSetDomesticHotWaterMorning command
void onSetDomesticHotWaterMorningCommand(HANumeric number, HANumber* sender) {
  if (!number.isSet()) {
  } else {
    dhwTempMorningSP = number.toFloat();
  }
  sender->setState(HANumeric(dhwTempMorningSP, 0));
}

// Callback for surfing the tSetDomesticHotWaterDay command
void onSetDomesticHotWaterDayCommand(HANumeric number, HANumber* sender) {
  if (!number.isSet()) {
  } else {
    dhwTempDaySP = number.toFloat();
  }
  sender->setState(HANumeric(dhwTempDaySP, 0));
}

// Callback for dancing to the tSetDomesticHotWaterEvening command
void onSetDomesticHotWaterEveningCommand(HANumeric number, HANumber* sender) {
  if (!number.isSet()) {
  } else {
    dhwTempEveningSP = number.toFloat();
  }
  sender->setState(HANumeric(dhwTempEveningSP, 0));
}

// Callback for setting sail with the tSetDomesticHotWaterNight command
void onSetDomesticHotWaterNightCommand(HANumeric number, HANumber* sender) {
  if (!number.isSet()) {
  } else {
    dhwTempNightSP = number.toFloat();
  }
  sender->setState(HANumeric(dhwTempNightSP, 0));
}

// Callback for setting sail with the tSetDomesticHotWaterLegionella command
void onSetDomesticHotWaterLegionellaCommand(HANumeric number, HANumber* sender) {
  if (!number.isSet()) {
  } else {
    dhwLegionellenSP = number.toFloat();
  }
  sender->setState(HANumeric(dhwLegionellenSP, 0));
}

// Callback for setting sail with the tSetDomesticHotWaterLegionella command
void onSetDomesticHotWaterBoostCommand(HANumeric number, HANumber* sender) {
  if (!number.isSet()) {
  } else {
    dhwTempBoostSP = number.toFloat();
  }
  sender->setState(HANumeric(dhwTempBoostSP, 0));
}

// Callback function for tSetBoilerBoostTemp
void onSetBoilerBoostTempCommand(HANumeric number, HANumber* sender) {
  // Perform any additional actions here
  if (!number.isSet()) {
  } else {
    boilerTempBoost = number.toFloat();
  }
  sender->setState(HANumeric(boilerTempBoost, 0));
}

//Callback for setting morning begin
void onSMorningBegin(int8_t index, HASelect* sender) {
  int startHour = 4;  //Select stars at 4:00 -> When updating also update udpateHA function!
  morningStart = (index / 2) + startHour;
  sender->setState(round((daytime - startHour) * 2));  // report the selected option back to the HA panel
}
// Callback for setting day begin
void onSDayBegin(int8_t index, HASelect* sender) {
  int startHour = 8;  // Select starts at 8:00 -> When updating also update udpateHA function!
  dayStart = (index / 2) + startHour;
  sender->setState(round((daytime - startHour) * 2));  // Report the selected option back to the HA panel
}

// Callback for setting afternoon begin
void onSAfternoonBegin(int8_t index, HASelect* sender) {
  int startHour = 15;  // Select starts at 12:00 -> When updating also update udpateHA function!
  afternoonStart = (index / 2) + startHour;
  sender->setState(round((daytime - startHour) * 2));  // Report the selected option back to the HA panel
}

// Callback for setting night begin
void onSNightBegin(int8_t index, HASelect* sender) {
  int startHour = 18;  // Select starts at 15:00 -> When updating also update udpateHA function!
  nightStart = (index / 2) + startHour;
  sender->setState(round((daytime - startHour) * 2));  // Report the selected option back to the HA panel
}

// Callback for setting legionella Day
void onSLegionellaDay(int8_t index, HASelect* sender) {
  legionellaProgramDay = index;
  sender->setState(legionellaProgramDay);  // Report the selected option back to the HA panel
}

void onSwitchCommand(bool state, HASwitch* sender) {
  if (sender == &boostSwitchHeating) {
    heatingMode = OTemp_AUTO;
    if (state) heatingMode = BOOST;
  } else if (sender == &boostSwitchHotWater) {
    hotWaterMode = AUTOMATIC;
    if (state) hotWaterMode = MANUAL;
  } else if (sender == &enableHeatingProgramSwitch) {
    enableHeatingProgram = state;
  } else if (sender == &enableHotWaterProgramSwitch) {
    enableHotWaterProgram = state;
    if (state) enableLegionellaProgram = true;
  } else if (sender == &enableLegionellaProgramSwitch) {
    enableLegionellaProgram = state;
    if(!state) enableHotWaterProgram = false;
  }
  lastSendTime = millis()-100000; //force update of all values to HA
  sender->setState(state);  // report state back to the Home Assistant
}

void processResponseCallback(unsigned long response, OpenThermResponseStatus status) {
  unsigned long rCopy = response;
  OpenThermMessageID rID = (OpenThermMessageID) ((rCopy >> 16) & 0xFF);  // extract only lower 8 bits
  float oldTempBuffer = 0.0;
  if (rID == OpenThermMessageID::Status) {
    if (status == OpenThermResponseStatus::SUCCESS) {
      isEnabledCentralHeating = ot.isCentralHeatingActive(response);
      isEnabledHotWater = ot.isHotWaterActive(response);
      isEnabledFlame = ot.isFlameOn(response);
      state = "noFlame ";
      if (isEnabledFlame) state = "FlameOn ";
    }
    if (status == OpenThermResponseStatus::NONE) {
      Serial.println("Error: OpenTherm is not initialized");
      state = "no Init ";
    } else if (status == OpenThermResponseStatus::INVALID) {
      Serial.println("Error: Invalid response " + String(response, HEX));
      state = String(response, HEX);
    } else if (status == OpenThermResponseStatus::TIMEOUT) {
      Serial.println("Error: Response timeout");
      state = "Timeout ";
    }
  }

  //Set water temp or set boiler temp need to be send successfuly
  if (rID == OpenThermMessageID::TdhwSet || rID == OpenThermMessageID::TSet) {
  }

  if (rID == OpenThermMessageID::Toutside) {
    if (status == OpenThermResponseStatus::SUCCESS) {
      outsideTemp = (outsideTemp * 9 + ot.getFloat(response)) / 10;
    }
  }

  if (rID == OpenThermMessageID::Tboiler) {
    if (status == OpenThermResponseStatus::SUCCESS) {
      boilerTemp = ot.getFloat(response);
    }
  }

  if (rID == OpenThermMessageID::Texhaust) {
    if (status == OpenThermResponseStatus::SUCCESS) {
      exhaustTemp = ot.getFloat(response);
    }
  }

  if (rID == OpenThermMessageID::Tdhw) {
    if (status == OpenThermResponseStatus::SUCCESS) {
      dhwTemp = ot.getFloat(response);
    }
  }

  if (rID == OpenThermMessageID::Tret) {
    if (status == OpenThermResponseStatus::SUCCESS) {
      returnWaterTemp = ot.getFloat(response);
    }
  }
}

//void ICACHE_RAM_ATTR handleInterruptCallback() { <- Old
void IRAM_ATTR handleInterruptCallback() {
  ot.handleInterrupt();
}

void queryDataFromTherme() {
  //Communicate OPENTHERM
  if (ot.isReady()) {
    unsigned long aReq = 0;
    if (requestID == 0) {
      aReq = ot.buildSetBoilerStatusRequest(enableCentralHeating, enableHotWater, enableCooling);
    }
    if (requestID == 1) {
      //aReq = request = ot.buildRequest(OpenThermRequestType::WRITE, OpenThermMessageID::TSet, ot.temperatureToData(boilerTempSP));
      aReq = ot.buildRequest(OpenThermRequestType::WRITE, OpenThermMessageID::TSet, ot.temperatureToData(boilerTempSP));
    }
    if (requestID == 2) {
      //aReq = request = ot.buildRequest(OpenThermRequestType::WRITE, OpenThermMessageID::TdhwSet, ot.temperatureToData(dhwTempSP));
      aReq = ot.buildRequest(OpenThermRequestType::WRITE, OpenThermMessageID::TdhwSet, ot.temperatureToData(dhwTempSP));
    }
    if (requestID == 3) {
      aReq = ot.buildRequest(OpenThermRequestType::READ, OpenThermMessageID::Toutside, data);
    }
    if (requestID == 4) {
      aReq = ot.buildRequest(OpenThermRequestType::READ, OpenThermMessageID::Tboiler, data);
    }
    if (requestID == 5) {
      aReq = ot.buildRequest(OpenThermRequestType::READ, OpenThermMessageID::Texhaust, data);
    }
    if (requestID == 6) {
      aReq = ot.buildRequest(OpenThermRequestType::READ, OpenThermMessageID::Tdhw, data);
    }
    if (requestID == 7) {
      aReq = ot.buildRequest(OpenThermRequestType::READ, OpenThermMessageID::Tret, data);
    }
    /*
      if(requestID == 8){
      aReq = ot.buildRequest(OpenThermRequestType::READ, OpenThermMessageID::BurnerStarts,data);
      }
    */
    if (ot.sendRequestAync(aReq)) {
      requestID++;
      if (requestID == 8)
        requestID = 0;
    }
  }
}


time_t getTime() {
  timeClient.update();
  TimeChangeRule* tcr;
  return myTZ.toLocal(timeClient.getEpochTime(), &tcr);
}

String getTimeString(time_t currentTime) {
  setTime(currentTime);
  int currentHour = hour();
  int currentMinute = minute();
  String hourString = (currentHour < 10) ? " " + String(currentHour) : String(currentHour);
  String minuteString = (currentMinute < 10) ? "0" + String(currentMinute) : String(currentMinute);
  return hourString + ":" + minuteString;
}

void manageHeating() {
  if (enableHeatingProgram){
    if (heatingMode == OTemp_AUTO) {
      enableCentralHeating = false;
      if (outsideTemp < heatingThreshold) enableCentralHeating = true;
      //boilerTempSP     = steepness * outsideTemp + zeroSetpoint; //Heizungskennlinie
      //    y = 39.42857 - 0.7885714 -0.01828571^2 -0.001371429^3
      //    new -0.5x-0.0005x^3+32
      int x = outsideTemp + tempShiftValue;
      int xx = x * x;
      int xxx = x * x * x;
      //boilerTempSP = 41.02857 - 0.4419048*x -0.01828571*xx -0.002438095*xxx;
      boilerTempSP = -0.5 * x - 0.0005 * xxx + 36;
      if (timeOfDay == 0) boilerTempSP = boilerTempSP * nightOffsetFactor;
      //if (boilerTempSP<31 && boilerTempSP >27) boilerTempSP = 31; //Disable boiler pwm
      //if (boilerTempSP<31 && boilerTempSP <=27) boilerTempSP = 24; //Disable boiler pwm
    } else if (heatingMode == BOOST) {
      enableCentralHeating = true;
      boilerTempSP = boilerTempBoost;
    }
  }
}
void manageHotWater() {

  if (enableHotWaterProgram && hotWaterMode == AUTOMATIC) {
    enableHotWater = false;
    dhwTempSP = dhwTempNightSP;
    if (timeOfDay == MORNING) {
      enableHotWater = true;
      dhwTempSP = dhwTempMorningSP;
    }
    if (timeOfDay == DAY) {
      enableHotWater = true;
      dhwTempSP = dhwTempDaySP;
    }
    if (timeOfDay == EVENING) {
      enableHotWater = true;
      dhwTempSP = dhwTempEveningSP;
    }
    if (timeOfDay == NIGHT) {
      enableHotWater = true;
      dhwTempSP = dhwTempNightSP;
    }
  }
  else if (enableHotWaterProgram && hotWaterMode == MANUAL) {
    enableHotWater = true;
    dhwTempSP = dhwTempBoostSP;
  }
  
  if (enableLegionellaProgram && dayOfWeek == legionellaProgramDay && timeOfDay==EVENING) {
    enableHotWater = true;
    dhwTempSP = dhwLegionellenSP;
  }
}

void manageDayAndTime() {
  if (WiFi.status() == WL_CONNECTED){
    timeClient.update();
    lastTimeUpdate = timeClient.getEpochTime();
  }
  if (lastTimeUpdate > 0) {
    dayOfWeek = timeClient.getDay();
    timeString = getTimeString(getTime());
    float hours = hour(getTime()) + minute(getTime()) / 60;
    timeOfDay = NIGHT;
    if (hours >= morningStart) timeOfDay = MORNING;
    if (hours >= dayStart) timeOfDay = DAY;
    if (hours >= afternoonStart) timeOfDay = EVENING;
    if (hours >= nightStart) timeOfDay = NIGHT;
  }
  //usefull defaults if no time is available
  else{
    dayOfWeek = 1;
    timeOfDay = EVENING;
    heatingMode = OTemp_AUTO;
    hotWaterMode = AUTOMATIC;
  }

}

void updateHA() {
  if (lastSendTime < millis() -60000) {

    //update state of the switches and sensors
    HAOutsideTemp.setValue(outsideTemp);
    HABoilerTemp.setValue(boilerTemp);
    HAReturnWaterTemp.setValue(returnWaterTemp);
    HAExhaustTemp.setValue(exhaustTemp);
    HADomesticHotWaterTemp.setValue(dhwTemp);
    
    tSetDomesticHotWaterMorning.setState(dhwTempMorningSP);
    tSetDomesticHotWaterDay.setState(dhwTempDaySP);
    tSetDomesticHotWaterEvening.setState(dhwTempEveningSP);
    tSetDomesticHotWaterNight.setState(dhwTempNightSP);
    tSetDomesticHotWaterLegionella.setState(dhwLegionellenSP);
    tSetDomesticHotWaterBoost.setState(dhwTempBoostSP);

    tSetBoilerBoostTemp.setState(boilerTempBoost);

    int morning = round((morningStart - 4) * 2);
    int day = round((dayStart - 8) * 2);
    int afternoon = round((afternoonStart - 15) * 2);
    int night = round((nightStart - 18) * 2);

    sMorningBegin.setState(morning);
    sDayBegin.setState(day);
    sAfternoonBegin.setState(afternoon);
    sNightBegin.setState(night);
    sLegionellaDay.setState(legionellaProgramDay);

    boostSwitchHeating.setState((bool)heatingMode);
    boostSwitchHotWater.setState((bool)hotWaterMode);
    enableHeatingProgramSwitch.setState(enableHeatingProgram);
    enableHotWaterProgramSwitch.setState(enableHotWaterProgram);
    enableLegionellaProgramSwitch.setState(enableLegionellaProgram);

    //update availability of the switches
    if(enableHotWaterProgram){
      tSetDomesticHotWaterMorning.setAvailability(true);
      tSetDomesticHotWaterDay.setAvailability(true);
      tSetDomesticHotWaterEvening.setAvailability(true);
      tSetDomesticHotWaterNight.setAvailability(true);
      tSetDomesticHotWaterBoost.setAvailability(true);
      sMorningBegin.setAvailability(true);
      sDayBegin.setAvailability(true);
      sAfternoonBegin.setAvailability(true);  
      sNightBegin.setAvailability(true);
      boostSwitchHotWater.setAvailability(true);

    }
    else{
      tSetDomesticHotWaterMorning.setAvailability(false);
      tSetDomesticHotWaterDay.setAvailability(false);
      tSetDomesticHotWaterEvening.setAvailability(false);
      tSetDomesticHotWaterNight.setAvailability(false);
      tSetDomesticHotWaterBoost.setAvailability(false);
      sMorningBegin.setAvailability(false);
      sDayBegin.setAvailability(false);
      sAfternoonBegin.setAvailability(false);
      sNightBegin.setAvailability(false);
      boostSwitchHotWater.setAvailability(false);
    }
    if(enableLegionellaProgram){
      tSetDomesticHotWaterLegionella.setAvailability(true);
      sLegionellaDay.setAvailability(true);
    }
    else{
      tSetDomesticHotWaterLegionella.setAvailability(false);
      sLegionellaDay.setAvailability(false);  
    }
    if(enableHeatingProgram){
      tSetBoilerBoostTemp.setAvailability(true);
      boostSwitchHeating.setAvailability(true);
    }
    else{
      tSetBoilerBoostTemp.setAvailability(false);
      boostSwitchHeating.setAvailability(false);
    }


    lastSendTime = millis();
  }
}

void showSplash() {

  lcd.clear();
  lcd.print("MQTTherm Arduino");

  lcd.setCursor(0, 1);
  lcd.print("(C) 2018 - 2024");
  lcd.setCursor(0, 2);
  lcd.print("Thomas Bach");
  lcd.setCursor(0, 3);
  lcd.print("v. ");
  lcd.print(version);
  delay(1000);
  lcd.clear();
}

void showMain() {
  String ipStr = "not connected";
  if(WiFi.status() == WL_CONNECTED){
  ipStr =WiFi.localIP().toString();
  }
  //lcd.clear();
  //1st row
  lcd.setCursor(0, 0);
  lcd.print(ipStr);

  lcd.setCursor(17, 0);
  lcd.print(wifiRSSI);

  //2nd row
  lcd.setCursor(0, 1);
  lcd.print(state);

  lcd.setCursor(8, 1);

  if (isEnabledCentralHeating) lcd.print("CH On");
  else lcd.print("     ");
  lcd.setCursor(14, 1);
  if (isEnabledHotWater) lcd.print("HW On");
  else lcd.print("     ");

  //if (isEnabledCentralHeating) lcd.write((uint8_t)0);  // Display the burning fire symbol
  //else lcd.write((uint8_t)1);                          // Display the stopped burning fire symbol

  //3rd row - col, row
  lcd.setCursor(0, 2);
  lcd.print("SP");
  lcd.setCursor(2, 2);
  lcd.print(String(boilerTempSP, 0));
  lcd.setCursor(5, 2);
  lcd.print("BO");
  lcd.setCursor(7, 2);
  lcd.print(String(boilerTemp, 0));
  lcd.setCursor(10, 2);
  lcd.print("RW");
  lcd.setCursor(12, 2);
  lcd.print(String(returnWaterTemp, 0));
  lcd.setCursor(15, 2);
  lcd.print("EX");
  lcd.setCursor(17, 2);
  lcd.print(String(exhaustTemp, 0));

  //4th row
  lcd.setCursor(0, 3);
  lcd.print("SP");
  lcd.setCursor(2, 3);
  lcd.print(String(dhwTempSP, 0));
  lcd.setCursor(5, 3);
  lcd.print("WT");
  lcd.setCursor(7, 3);
  lcd.print(String(dhwTemp, 0));
  lcd.setCursor(10, 3);
  lcd.print("OT");
  lcd.print("    ");
  lcd.setCursor(12, 3);
  lcd.print(String(outsideTemp, 0));
  lcd.setCursor(15, 3);
  lcd.print(timeString);
  delay(150);
}

void setup() {


  WiFi.disconnect();
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  WiFi.setAutoReconnect(true);
  WiFi.hostname(hostname);

  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname(hostname);
  ArduinoOTA.setPassword(otaPassword);

  /*
  ArduinoOTA.onStart([]() {
    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
*/
  ArduinoOTA.begin();

  byte mac[WL_MAC_ADDR_LENGTH];
  WiFi.macAddress(mac);
  device.setUniqueId(mac, sizeof(mac));

  Wire.begin(12, 14);
  Wire.beginTransmission(0x27);
  lcd.begin(20, 4);  // initialize the lcd
  lcd.setBacklight(255);
  lcd.home();
  lcd.clear();
  lcd.createChar(0, burningFire);
  lcd.createChar(1, stoppedFire);

  ot.begin(handleInterruptCallback, processResponseCallback);
  showSplash();

  mqtt.begin(mqttServer);

  // set device's details (optional)
  device.setName(hostname);
  device.setSoftwareVersion(version);
  device.setModel(hostname);
  device.setManufacturer("Thomas");

  // HANumber tSetDomesticHotWaterMorning - Morgensonne erwacht ☀️
  tSetDomesticHotWaterMorning.setIcon("mdi:thermometer-water");
  tSetDomesticHotWaterMorning.setName("WT Morgen");
  tSetDomesticHotWaterMorning.onCommand(onSetDomesticHotWaterMorningCommand);
  tSetDomesticHotWaterMorning.setMin(10);  // Mindesttemperatur, um den Tag zu beginnen!
  tSetDomesticHotWaterMorning.setMax(65);  // Maximale Temperatur, weil es heiß wird!
  tSetDomesticHotWaterMorning.setStep(1);  // In 5-Grad-Schritten aufwärmen!
  tSetDomesticHotWaterMorning.setMode(HANumber::ModeSlider);
  tSetDomesticHotWaterMorning.setState(HANumeric(dhwTempMorningSP, 1));  // Aktuelle Stimmung: Toasty 🌡️
  tSetDomesticHotWaterMorning.setAvailability(false);

  // HANumber tSetDomesticHotWaterDay - Nachmittagskühle vertreiben 🌤️
  tSetDomesticHotWaterDay.setIcon("mdi:thermometer-water");
  tSetDomesticHotWaterDay.setName("WT Tag");
  tSetDomesticHotWaterDay.onCommand(onSetDomesticHotWaterDayCommand);
  tSetDomesticHotWaterDay.setMin(10);  // Mindesttemperatur, um es warm zu halten!
  tSetDomesticHotWaterDay.setMax(65);  // Maximale Temperatur, um die Hitze zu steigern!
  tSetDomesticHotWaterDay.setStep(1);  // In 5-Grad-Schritten brutzeln!
  tSetDomesticHotWaterDay.setMode(HANumber::ModeSlider);
  tSetDomesticHotWaterDay.setState(HANumeric(dhwTempDaySP, 1));  // Aktuelle Stimmung: Gemütlich 🔥
  tSetDomesticHotWaterDay.setAvailability(false);

  // HANumber tSetDomesticHotWaterEvening - Abendglanz erwärmen 🌇
  tSetDomesticHotWaterEvening.setIcon("mdi:thermometer-water");
  tSetDomesticHotWaterEvening.setName("WT Abend");
  tSetDomesticHotWaterEvening.onCommand(onSetDomesticHotWaterEveningCommand);
  tSetDomesticHotWaterEvening.setMin(10);  // Mindesttemperatur, weil es kühl wird!
  tSetDomesticHotWaterEvening.setMax(65);  // Maximale Temperatur, um den Abend anzufeuern!
  tSetDomesticHotWaterEvening.setStep(1);  // Sanft in 5-Grad-Schritten aufsteigen!
  tSetDomesticHotWaterEvening.setMode(HANumber::ModeSlider);
  tSetDomesticHotWaterEvening.setState(HANumeric(dhwTempEveningSP, 1));  // Aktuelle Wärme: Einladend 🌅
  tSetDomesticHotWaterEvening.setAvailability(false);

  // HANumber tSetDomesticHotWaterNight - Nachtstille genießen 🌙
  tSetDomesticHotWaterNight.setIcon("mdi:thermometer-water");
  tSetDomesticHotWaterNight.setName("WT Nacht");
  tSetDomesticHotWaterNight.onCommand(onSetDomesticHotWaterNightCommand);
  tSetDomesticHotWaterNight.setMin(10);  // Mindesttemperatur, um die Nacht zu überstehen!
  tSetDomesticHotWaterNight.setMax(65);  // Maximale Temperatur, um die Kälte zu vertreiben!
  tSetDomesticHotWaterNight.setStep(1);  // In 5-Grad-Schritten abkühlen!
  tSetDomesticHotWaterNight.setMode(HANumber::ModeSlider);
  tSetDomesticHotWaterNight.setState(HANumeric(dhwTempNightSP, 1));  // Aktuelle Stimmung: Kuschelig 🌌
  tSetDomesticHotWaterNight.setAvailability(false);
  
  // HANumber tSetDomesticHotWaterLegionella - Legionellenabwehr im Superheldenstil 🦸
  tSetDomesticHotWaterLegionella.setIcon("mdi:thermometer-water");
  tSetDomesticHotWaterLegionella.setName("WT Legionellen");
  tSetDomesticHotWaterLegionella.onCommand(onSetDomesticHotWaterLegionellaCommand);
  tSetDomesticHotWaterLegionella.setMin(60);  // Mindesttemperatur, um diese fiesen Keime fernzuhalten!
  tSetDomesticHotWaterLegionella.setMax(75);  // Maximale Temperatur, weil wir Superhelden-heiß sind!
  tSetDomesticHotWaterLegionella.setStep(1);  // In 5-Grad-Schritten in Aktion treten!
  tSetDomesticHotWaterLegionella.setMode(HANumber::ModeSlider);
  tSetDomesticHotWaterLegionella.setState(HANumeric(dhwLegionellenSP, 1));  // Legionellenabwehrlevel: Superheld 💪
  tSetDomesticHotWaterLegionella.setAvailability(false);


  tSetDomesticHotWaterBoost.setIcon("mdi:thermometer-water");
  tSetDomesticHotWaterBoost.setName("WT Boost");
  tSetDomesticHotWaterBoost.onCommand(onSetDomesticHotWaterBoostCommand);
  tSetDomesticHotWaterBoost.setMin(40);
  tSetDomesticHotWaterBoost.setMax(75);
  tSetDomesticHotWaterBoost.setStep(1);
  tSetDomesticHotWaterBoost.setMode(HANumber::ModeSlider);
  tSetDomesticHotWaterBoost.setState(HANumeric(dhwTempBoostSP, 1));
  tSetDomesticHotWaterBoost.setAvailability(false);

  // HANumber tSetBoilerBoostTemp - Boiler Boost Temperature
  tSetBoilerBoostTemp.setIcon("mdi:thermometer-water");
  tSetBoilerBoostTemp.setName("Hzg Tmp Boost");
  tSetBoilerBoostTemp.onCommand(onSetBoilerBoostTempCommand);
  tSetBoilerBoostTemp.setMin(40);
  tSetBoilerBoostTemp.setMax(65);
  tSetBoilerBoostTemp.setStep(1);
  tSetBoilerBoostTemp.setMode(HANumber::ModeSlider);
  tSetBoilerBoostTemp.setState(HANumeric(boilerTempBoost, 1));
  tSetBoilerBoostTemp.setAvailability(false);


  // HASensorNumber HAOutsideTemp - Außentemperatur, das Wetter vor der Haustür ☀️
  HAOutsideTemp.setUnitOfMeasurement("°C");
  HAOutsideTemp.setIcon("mdi:thermometer");
  HAOutsideTemp.setName("Außentemperatur");

  // HASensorNumber HABoilerTemp - Vorlauftemperatur, wenn es heiß wird 🌡️
  HABoilerTemp.setUnitOfMeasurement("°C");
  HABoilerTemp.setIcon("mdi:thermometer");
  HABoilerTemp.setName("Vorlauftemperatur");

  // HASensorNumber HAReturnWaterTemp - Rücklauftemperatur, wenn die Wärme zurückkommt 🔥
  HAReturnWaterTemp.setUnitOfMeasurement("°C");
  HAReturnWaterTemp.setIcon("mdi:thermometer");
  HAReturnWaterTemp.setName("Rücklauftemperatur");

  // HASensorNumber HAExhaustTemp - Abgastemperatur, wenn der Rauch aufsteigt 🌬️
  HAExhaustTemp.setUnitOfMeasurement("°C");
  HAExhaustTemp.setIcon("mdi:thermometer");
  HAExhaustTemp.setName("Abgastemperatur");

  // HASensorNumber HADomesticHotWaterTemp - Warmwassertemperatur, wenn du es warm magst 🔥
  HADomesticHotWaterTemp.setUnitOfMeasurement("°C");
  HADomesticHotWaterTemp.setIcon("mdi:thermometer");
  HADomesticHotWaterTemp.setName("Warmwassertemperatur");

  // set available options
  sMorningBegin.setOptions("4:00;4:30;5:00;5:30;6:00;6:30;7:00;7:30;8:00;8:30;9:00;9:30");
  sMorningBegin.onCommand(onSMorningBegin);
  sMorningBegin.setIcon("mdi:weather-sunset-up");
  sMorningBegin.setName("Morgen ab");
  sMorningBegin.setAvailability(false);

  // set available options
  sDayBegin.setOptions("8:00;8:30;9:00;9:30;10:00;10:30;11:00;11:30;12:00;12:30");
  sDayBegin.onCommand(onSDayBegin);
  sDayBegin.setIcon("mdi:weather-sunny");
  sDayBegin.setName("Tag ab");
  sDayBegin.setAvailability(false);

  // set available options
  sAfternoonBegin.setOptions("15:00;15:30;16:00;16:30;17:00;17:30;18:00;18:30;19:00;19:30");
  sAfternoonBegin.onCommand(onSAfternoonBegin);
  sAfternoonBegin.setIcon("mdi:weather-sunset-down");
  sAfternoonBegin.setName("Abend ab");
  sAfternoonBegin.setAvailability(false);

  // set available options
  sNightBegin.setOptions("18:00;18:30;19:00;19:30;20:00;20:30;21:00;21:30;22:00;22:30;23:00;23:30");
  sNightBegin.onCommand(onSNightBegin);
  sNightBegin.setIcon("mdi:weather-night");
  sNightBegin.setName("Nacht ab");
  sNightBegin.setAvailability(false);

  sLegionellaDay.setOptions("Sonntag;Montag;Dinstag;Mittwoch;Donnerstag;Freitag;Samstag");
  sLegionellaDay.onCommand(onSLegionellaDay);
  sLegionellaDay.setIcon("mdi:virus-off-outline");
  sLegionellaDay.setName("Wochentag Legionellenprogramm");
  sLegionellaDay.setAvailability(false);

  boostSwitchHeating.setName("Heizungs Booster");
  boostSwitchHeating.setIcon("mdi:radiator");
  boostSwitchHeating.onCommand(onSwitchCommand);
  boostSwitchHeating.setAvailability(false);

  boostSwitchHotWater.setName("Warmwasser Booster");
  boostSwitchHotWater.setIcon("mdi:water-boiler");
  boostSwitchHotWater.onCommand(onSwitchCommand);
  boostSwitchHotWater.setAvailability(false);

  enableHeatingProgramSwitch.setName("Heizung");
  enableHeatingProgramSwitch.setIcon("mdi:radiator");
  enableHeatingProgramSwitch.onCommand(onSwitchCommand);

  enableHotWaterProgramSwitch.setName("Warmwasserbereitung");
  enableHotWaterProgramSwitch.setIcon("mdi:water-boiler");
  enableHotWaterProgramSwitch.onCommand(onSwitchCommand);

  enableLegionellaProgramSwitch.setName("Legionellenprogramm");
  enableLegionellaProgramSwitch.setIcon("mdi:virus-off-outline");
  enableLegionellaProgramSwitch.onCommand(onSwitchCommand);
}

void loop() {

  ArduinoOTA.handle();

  ot.process();
  queryDataFromTherme();
  manageHeating();
  manageHotWater();
  manageDayAndTime();

  if (WiFi.status() == WL_CONNECTED) {
    wifiRSSI = String(WiFi.RSSI());
    mqtt.loop();
    updateHA();
  } else {
    wifiRSSI = " NC";
  }
  showMain();
}