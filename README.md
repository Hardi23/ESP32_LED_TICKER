# ESP32_LED_TICKER
Ticker made from an ESP32 and a Led Matrix

## Hardware you will need:
* ESP32
* MAX72XX Led Matrix (p.e. MAX7219 on ebay)


## Libraries used
* MD_MAX72XX
* WiFi
* HTTPClient
* ArduinoJson
* SPIFFS
* ESPAsyncWebServer

## Getting started
1. Loading static files onto the ESP

   You can load the static files (webpage and so on) to the ESP directly using SPIFFS to access them.
   They are already in the correct location and you can change them to your desire.
   
   [Here](https://randomnerdtutorials.com/install-esp32-filesystem-uploader-arduino-ide/) is an example how to flash them to your ESP using the arduino IDE.
   
2. Defining hardware and other constants
 
   First up you'll need to set the hardware type of your Led Panels (You might be able to read it from the board then panels sit on, FC16 in my case)
   and supply the panel count.
   ```
   #define HARDWARE_TYPE MD_MAX72XX::FC16_HW

   #define PANEL_COUNT 4
   #define DELAYTIME 100
   #define CHAR_SPACING 1
   #define CLK_PIN 18
   #define DATA_PIN 23
   #define CS_PIN 15
   #define DEFAULT_INTENSITY 1
   ```
   The delay time changes how fast the panel is scrolling (This is just a baseline you can change it from the Webpage, same goes for the panel intensity).
   Most likely you want to keep the pinout as-is.

3. Set your wifi access data
   ```
   const char ssid[] PROGMEM = "<replace with your ssid>";
   const char pw[] PROGMEM = "<replace with your key>";
   const char hostname[] PROGMEM = "<Your desired hostname>";
   ```

4. Loading new data
     In my case I will load the current ETH values from [coinmarketcap](https://coinmarketcap.com) and mining information from [ethermine](https://ethermine.org).

     If you also want to get your data from [coinmarketcap](https://coinmarketcap.com) you'll need to create a free account and supply your access token.
   ```
   const String apiKey PROGMEM = "yourAPIKey";
   const char headerAPI[] PROGMEM = "X-CMC_PRO_API_KEY";
   const char url[] PROGMEM = "https://pro-api.coinmarketcap.com/v1/cryptocurrency/quotes/latest?symbol=ETH&convert=EUR";
   const char minerAPI[] PROGMEM = "https://api.ethermine.org/miner/<your wallet address here>/currentStats";
   ```

5. Internal communication

   This sketch uses multiple tasks to load information and display the webpage.
   That's why we use a [Queue](https://www.freertos.org/a00116.html) to communicate between the main loop and the update task.
   
   As well as a [Mutex](https://www.freertos.org/CreateMutex.html) to prohibit changing the underlying data while reading it.
   
   The Queue is also used to notify the task to end and free its resources, in case the mode is set to display TEXT.
   
6. Web access
   
   Here all the available endpoints are defined, one of them ("/api/status") to act as a REST endpoint.
   
   ```
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
   ```
   The webpage as well as the POST endpoint to change the values on your ESP are protected by HTTP Authentication if you dont want to use this feature simply remove these lines:
   ```
   if(!request->authenticate(httpUsername, httpPassword)){
          return request->requestAuthentication();
   }
   ```
   The access data is defined here:
   ```
   const char* httpUsername = "admin";
   const char* httpPassword = "securePassword!";
   ```
   
   Another thing to keep in mind is that when we serve the webpage, the html file will be processed and placeholder will be replaced with the actual values.
   Which values are replaced is defined here:
   ```
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
   ```
   
   Placeholders in the html file are defined like this:
   ```
   %PLACEHOLDER_NAME%
   ```
## The given IP will be displayed on startup

_That's about it feel free to ask questions if you encounter problems, Cheers Josh_
