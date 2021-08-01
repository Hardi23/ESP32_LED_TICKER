/* Copyright (C) 2021 Joshua Oppel
*  This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, 
*  either version 3 of the License, or (at your option) any later version.
*  This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; 
*  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
*  You should have received a copy of the GNU General Public License along with this program. If not, see <http://www.gnu.org/licenses/>*/

#include <MD_MAX72xx.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <SPIFFS.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>

/*DEBUG FLAG FOR SERIAL OUTPUT*/
#define DEBUG

/***********************************************
*====== DEFINITIONS FOR LED PANEL =========*
************************************************/
// v Replace with your hardware type v
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW

#define PANEL_COUNT 4
#define DELAYTIME 100
#define CHAR_SPACING 1
#define CLK_PIN 18
#define DATA_PIN 23
#define CS_PIN 15
#define DEFAULT_INTENSITY 1

/***********************************************
 *== DEFINITIONS FOR DEBUG AND DISPLAY MODE ===*
 ***********************************************/
#define MODE_MINING 1
#define MODE_TEXT 2
#define END_TASK 'e'
#define CHECK 'c'

/************************************************
 *============== WIFI ACCESS DATA ==============*
 ************************************************/
const char ssid[] PROGMEM = "ssid";
const char pw[] PROGMEM = "password";

/************************************************
 *============ WEBSITE ACCESS DATA =============*
 ************************************************/
const char* httpUsername = "admin";
const char* httpPassword = "securePassword!";

/*******************************************************************
 *============ VARIABLES FOR LOADING AND PARSING DATA =============*
 *******************************************************************/
const String apiKey PROGMEM = "yourAPIKey";
const char headerAPI[] PROGMEM = "X-CMC_PRO_API_KEY";
const char url[] PROGMEM = "https://pro-api.coinmarketcap.com/v1/cryptocurrency/quotes/latest?symbol=ETH&convert=EUR";
const char minerAPI[] PROGMEM = "https://api.ethermine.org/miner/0378B4659D64187B6C2Fd388b87FEdf5E603bDe2/currentStats";
const char hostname[] PROGMEM = "LED Panel";
const char jsonAttributeData[] PROGMEM = "data";
const char emptyJSON[] PROGMEM = "{}";

/*******************************************************************
 *======= SETTING DELAY BETWEEN REQUESTS AND QUEUE TIMEOUT ========*
 *******************************************************************/
const unsigned long requestDelay = (5 * 60 * 1000);
const unsigned long mutexTimeout = 100 / portTICK_PERIOD_MS;
const unsigned long mutextTimeoutHTTP =  (30 * 1000) / portTICK_PERIOD_MS;

/***********************************************
*==== VARIABLE TO CHECK IF TASK IS RUNNING ====*
************************************************/
volatile bool miningTaskRunning = false;

/*****************************************************
*======= VARIABLE REPRESENTING CURRENT STATE ========*
******************************************************/
unsigned long lastUpdate = 0;
float* values;
int intensity = 1;
int speedMod = 0;
int displayMode = MODE_MINING;
bool canUseTask = true;
String nextText;
String currentText;

/***********************************************
*=========== OBJECT DECLARATIONS ==============*
************************************************/
AsyncWebServer server(80);
DynamicJsonDocument doc(2048);
MD_MAX72XX mx = MD_MAX72XX(HARDWARE_TYPE, CS_PIN, PANEL_COUNT);
QueueHandle_t xQueue1;
SemaphoreHandle_t mutex;

/*********************************
*======== SETUP START ===========*
**********************************/
void setup() {
  #ifdef DEBUG
    Serial.begin(115200);
    while (!Serial) {
      Serial.println(F("Waking up..."));
    }
  #endif
  
  /*==========*/
  /*SETUP WIFI*/
  /*==========*/
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(hostname);
  WiFi.begin(ssid, pw);
  
  #ifdef DEBUG
    Serial.print(F("Connecting to WiFi .."));
  #endif
  
  while (WiFi.status() != WL_CONNECTED) {
    #ifdef DEBUG
      Serial.print('.');
    #endif
    delay(1000);
  }
  
  #ifdef DEBUG
    Serial.println(WiFi.localIP());
  #endif

  /*====================================*/
  /*SETUP SPIFFS TO ACCESS SAVED CONTENT*/
  /*====================================*/
  SPIFFS.begin();

  /*===================================*/
  /*   SETUP SERVER ACTIONS ON ACCESS  */
  /* WEBSITE ACCESS AS WELL AS UPDATES */
  /*ARE PROTECTED BY HTTP AUTHENICATION*/
  /*===================================*/
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        if(!request->authenticate(httpUsername, httpPassword)){
          return request->requestAuthentication();
        }
        request->send(SPIFFS, "/index.html", String(), false, processor);
  });
  server.on("/", HTTP_POST, [](AsyncWebServerRequest *request){
        if(!request->authenticate(httpUsername, httpPassword)){
          return request->requestAuthentication();
        }
        handleHTTPRequest(request);
        request->send(SPIFFS, "/index.html", String(), false, processor);
  });
  server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(SPIFFS, "/favicon.ico", "image/gif");
  });
  server.on("/stylesheet.css", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(SPIFFS, "/stylesheet.css", "text/css");
  });
  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "text/json", buildStatusJson());
  });

  /*=======================================*/
  /*SET lastUpdate TO IMMEDIATELY LOAD DATA*/
  /*=======================================*/
  lastUpdate = millis() - requestDelay;

  /*=======================================*/
  /*ALLOCATE SPACE TO SAVE LOADED VARIABLES*/
  /*=======================================*/
  values = (float*) malloc(4 * sizeof(float));

  /*=======================================*/
  /**** CREATE MUTEX AND BLOCKING QUEUE ****/
  /*=======================================*/
  mutex = xSemaphoreCreateMutex();
  if(mutex == NULL){
    #ifdef DEBUG
      Serial.println("Critical error mutext cannot be created!");
    #endif  
    while(true){
      delay(1000);
    }
  }

  xQueue1 = xQueueCreate(5, sizeof(char));
  if(xQueue1 == NULL){
    canUseTask = false;
    #ifdef DEBUG
      Serial.println("Could not create IPC Queue!");
    #endif  
  }
  /*=====================================*/
  /* START EVERYTHING AND DISPLAY OUR IP */
  /*=====================================*/
  server.begin();
  
  if(canUseTask){
    startMiningTask();
  }
  
  mx.begin();
  mx.control(MD_MAX72XX::INTENSITY, DEFAULT_INTENSITY);
  currentText = WiFi.localIP().toString();
}
/*******************************
*======== SETUP END ===========*
********************************/

/*******************************
*======== LOOP START ==========*
********************************/
void loop() {
  /*==========================================*/
  /*CHECK IF WE SHOULD QUEUE AN UPDATE MESSAGE*/
  /*==========================================*/
  if(miningTaskRunning && lastUpdate + requestDelay < millis()){
    char message = CHECK;
    if(xQueue1 != NULL){
      xQueueSend(xQueue1, (void *) &message, (TickType_t) 0);
      
      //Setting lastUpdate in case it takes longer for our task to update than one loop cycle
      lastUpdate = millis();
    }
  }
  /*=================*/
  /**** SHOW TEXT ****/
  /*=================*/
  if(xSemaphoreTake(mutex, mutexTimeout) == pdTRUE){
    if(!nextText.isEmpty()){
      #ifdef DEBUG
        Serial.println("Changing current text");
        Serial.println(currentText);
        Serial.println(nextText);
      #endif
      currentText = nextText;
      nextText = "";
    }
    xSemaphoreGive(mutex);
  }
  #ifdef DEBUG
   else{
      Serial.println("Failed to obtain mutex, while trying to draw text");
   }
  #endif
  scrollText(currentText);
}
/*******************************
*======== LOOP END ===========*
********************************/

void scrollText(String text){
  uint8_t charWidth;
  uint8_t cBuf[8];  // this should be ok for all built-in fonts
  mx.clear();

  for(uint8_t i = 0; i < text.length(); i++){
    charWidth = mx.getChar(text.charAt(i), sizeof(cBuf) / sizeof(cBuf[0]), cBuf);
    
    for (uint8_t i=0; i<=charWidth; i++)  // allow space between characters
    {
      mx.transform(MD_MAX72XX::TSL);
      if (i < charWidth)
        mx.setColumn(0, cBuf[i]);
      delay(DELAYTIME - speedMod);
    }
  }

  for(uint8_t i = 0; i < PANEL_COUNT * 8; i++){
    mx.transform(MD_MAX72XX::TSL);
    mx.setColumn(0, 0);
    delay(DELAYTIME - speedMod);
  }
}

/*****************************************
*= METHOD TO CREATE TASK IF NOT RUNNING =*
******************************************/
void startMiningTask(){
  if(!miningTaskRunning){
    #ifdef DEBUG
      Serial.println("Starting mining task");
    #endif
    xTaskCreate(
      taskUpdateData,
      "MiningInfo Runner",
      8192,
      (void * ) 1,
      1,
      NULL
    );
  }
}

/*********************
*= UPDATE TASK CODE =*
**********************/
void taskUpdateData(void * param){
  miningTaskRunning = true;
  if(xQueue1 != NULL){
    while(true){
      char messageType;
      if(xQueueReceive(xQueue1, &(messageType), (TickType_t) portMAX_DELAY) == pdPASS){
        if(messageType == END_TASK){
          break;
        }else if(messageType == CHECK){
          loadValues();
          buildString();
          //ledMatrix.setNextText(newText);
          #ifdef DEBUG
            Serial.println(ESP.getFreeHeap());
          #endif
        }
      }
    }
  }
  #ifdef DEBUG
    else{
      Serial.println("Queue is null");
    }
    Serial.println("Ending mining task");
  #endif
  miningTaskRunning = false;
  vTaskDelete( NULL );
}

/*************************************************************
*= METHOD TO BUILD JSON DATA ACCESSED BY /api/status (REST) =*
**************************************************************/
String buildStatusJson(){
  DynamicJsonDocument serDoc(1024);
  serDoc["mode"] = (displayMode == MODE_MINING) ? F("MINING") : F("TEXT");
  serDoc["speed_mod"] = speedMod;
  serDoc["intensity"] = intensity;
  serDoc["core"] = xPortGetCoreID();
  serDoc["free_heap_total"] = ESP.getFreeHeap();
  serDoc["cur_text"] = currentText;
  String retVal;
  serializeJson(serDoc, retVal);
  return retVal;
}

/**************************
*= HTTP REQUEST HANDLING =*
***************************/
void handleHTTPRequest(AsyncWebServerRequest *request){
  
  #ifdef DEBUG
    Serial.println("New request!");
  #endif
  
  if(request->hasParam("intensity", true)){
    AsyncWebParameter* p = request->getParam("intensity", true);
    intensity = p->value().toInt();
    if(intensity > 15){
      intensity = 15;
    }
    if(intensity < 0){
      intensity = 0;
    }
    
    #ifdef DEBUG
      Serial.print("Intensity: ");
      Serial.println(intensity);
    #endif
    mx.control(MD_MAX72XX::INTENSITY, intensity);
    //ledMatrix.setIntensity(intensity);
  }
  if(request->hasParam("speed", true)){
    AsyncWebParameter* p = request->getParam("speed", true);
    speedMod = p->value().toInt();
    if(speedMod > 50){
      speedMod = 50;
    }
    if(speedMod < -50){
      speedMod = -50;
    }
    
    #ifdef DEBUG
      Serial.print("SpeedMod: ");
      Serial.println(speedMod);
    #endif
    
  }
  if(request->hasParam("mode", true)){
    if(request->getParam("mode", true)->value() == "text"){
      if(request->hasParam("text", true)){
        if(miningTaskRunning){
          char message = END_TASK;
          xQueueSend(xQueue1, (void *) &message, (TickType_t) 0);
        }
        if(xSemaphoreTake(mutex, (TickType_t) mutexTimeout) == pdTRUE){
          nextText = request->getParam("text", true)->value().c_str();
          xSemaphoreGive(mutex);
          displayMode = MODE_TEXT;
        }
        #ifdef DEBUG
          else{
            Serial.println("Failed to obtain mutex, while setting text via http"); 
          }
        #endif
        //ledMatrix.setNextText(newText);
      }
    }else if(request->getParam("mode", true)->value() == "mining"){
      if(canUseTask && !miningTaskRunning){
        startMiningTask();
        lastUpdate = millis() - requestDelay;
        displayMode = MODE_MINING;
      }
    }
  }
}

/****************************
*= HTML TEMPLATE PROCESSOR =*
*****************************/
String processor(const String& var){
  if(var == "CUR_INTENSITY"){
    return String(intensity);
  }else if(var == "CUR_SPEED_MOD"){
    return String(speedMod);
  }else if(var == "MINING_CHECKED"){
    if(displayMode == MODE_MINING){
      return F("checked");
    }
  }else if(var == "TEXT_CHECKED"){
    if(displayMode == MODE_TEXT){
      return F("checked");
    }
  }else if(var == "CUR_CORE"){
    return String(xPortGetCoreID());
  }
  return String();
}

/**************************************************************
*= METHOD TO BUILD STRING THAT IS DISPLAYED ON THE LED PANEL =*
***************************************************************/
void buildString(){
  String buf = "";
  buf += F("ETH:");
  buf += String(values[0], 2);
  buf += F("EUR ");
  if(values[1] >= 0){
    buf += F("+");
  }
  buf += String(values[1], 2);
  buf += F("% ");
  buf += F("@");
  buf += String(values[2], 1);
  buf += F(" MH Balance ");
  buf += String(values[3], 5);
  buf += F("ETH ");
  buf += F("|| ");
  buf += String(values[3] * values[0], 2);
  buf += F("EUR");
  
  #ifdef DEBUG
    Serial.println(F("Built new String:"));
    Serial.println(buf);
  #endif

  if(xSemaphoreTake(mutex, (TickType_t) mutexTimeout) == pdTRUE){
    nextText = buf.c_str();
    xSemaphoreGive(mutex);
  }
  #ifdef DEBUG
    else{
      Serial.println("Failed to obtain mutex, while creating new text"); 
    }
  #endif
  
}

/******************************************************
*= METHOD TO LOAD REQUIRED DATA INTO VARIABLES ARRAY =*
*******************************************************/
void loadValues(){
  HTTPClient http;
  http.begin(url);
  http.addHeader(headerAPI, apiKey);
  int httpResponseCode = http.GET();
  String response = emptyJSON;
  if(httpResponseCode > 0){
    response = http.getString();
  }
  http.end();
  
  #ifdef DEBUG
    Serial.println(response);
  #endif
  
  deserializeJson(doc, response);
  values[0] = double(doc["data"]["ETH"]["quote"]["EUR"]["price"]);
  values[1] = double(doc["data"]["ETH"]["quote"]["EUR"]["percent_change_24h"]);
  doc.clear();
  
  http.begin(minerAPI);
  http.addHeader(headerAPI, "null");
  httpResponseCode = http.GET();
  if(httpResponseCode > 0){
    response = http.getString();
  }
  http.end();
  deserializeJson(doc,response);
  float hashRate = (doc["data"]["currentHashrate"]);
  hashRate /= float(1000000);
  float balance = (doc["data"]["unpaid"]);
  balance /= float(10E17);
  doc.clear();
  
  #ifdef DEBUG
    Serial.println(response);
    Serial.println(hashRate);
  #endif
  
  values[2] = hashRate;
  values[3] = balance;

  lastUpdate = millis();
  #ifdef DEBUG
    Serial.print(F("New values: "));
    Serial.println(values[0]);
    Serial.println(values[1]);
    Serial.println(values[2]);
    Serial.println(values[3]);
  #endif
}
