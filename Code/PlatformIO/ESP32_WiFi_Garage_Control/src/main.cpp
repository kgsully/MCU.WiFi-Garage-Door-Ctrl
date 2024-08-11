/* 
ESP32 WiFi Garage Door Controller
by Ken S. 
Last updated: 08/2024
Original:     12/2022

Code for an ESP32-WROOM based design for a WiFi enabled garage door controller capable of operating 2 garage doors. 
It includes illuminated physical pushbuttons wired in parallel with relays to allow the user to operate the 
garage doors even if the controller is unpowered or disconnected from WiFi. 
Included controls mimic those found on the manufacturer provided openers for the units I have – Open/Close, Light On/Off, and Lock. 
The ESP32 acts as a webserver hosting a website with the applicable controls / status indications for both doors. 
This project uses the WiFi manager library to aid in WiFi connection and to prevent the 
need to hard-code the WiFi network credentials into the program. OTA update functionality is also included.

Update - 08/2024:
- Refactor to separate Javascript into app.js
- Added functionality for a minimal version of the website providing only door controls without feedback display.
  This is intended to be used with an apple watch, whose webkit (at the time of this writing) has little / no
  javascript support. As a result, transactions between the web front-end and the MCU backend are accomplished
  via form submission and conditionals based upon the parameter key/value pair. Create a shortcut on the apple
  watch (Open URL --> [deviceURL]/watch) in order to open the website.


THIS SOFTWARE IS PROVIDED ''AS IS'' AND ANY
EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.  
*/

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <ESPAsyncWebServer.h>
#include <WebSocketsServer.h>
#include <AsyncElegantOTA.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <StreamString.h>

// GPIO Definitions
#define pin_wifiReset 15
#define pin_wifiStatus 2

#define pin_door1CMD 18
#define pin_door1Light 19
#define pin_door1LockSW 27
#define pin_door1ZSO 25
#define pin_door1ZSC 26

#define pin_door2CMD 33
#define pin_door2Light 32
#define pin_door2LockSW 16 
#define pin_door2ZSO 5
#define pin_door2ZSC 17

// Variable Definitions
bool door1_CMD = 0;
bool door1_Light = 0;
bool door1_ZSO = 0;
bool door1_ZSC = 0;
bool door1_LockSts = 0;
bool door2_CMD = 0;
bool door2_Light = 0;
bool door2_ZSO = 0;
bool door2_ZSC = 0;
bool door2_LockSts = 0;

bool door1_CMD_Latch = 0;
bool door1_Light_Latch = 0;
bool door2_CMD_Latch = 0;
bool door2_Light_Latch = 0;

bool pageLoadTrigger = 0;

int cmdHoldPeriod = 1000;
unsigned long door1_CMD_Time = 0;
unsigned long door1_Light_Time = 0;
unsigned long door2_CMD_Time = 0;
unsigned long door2_Light_Time = 0;
unsigned long wifi_Previous_Time = 0;
unsigned long wifi_Reconnect_Delay = 20000;

int doorStatus[6] = {0, 0, 0, 0, 0, 0};
int doorStatusBuffer[6] = {0, 0, 0, 0, 0, 0};

// Create WifiManager Object
WiFiManager wm;
//wm.setHttpPort(83);  // If necessary to change the WiFi Manager portal port use this

// Create WebServer and WebSocket Server Objects w/ Port #'s
AsyncWebServer webServer(8080); 
WebSocketsServer webSocketsServer(8081); 

const char* wm_SSID = "ESP32 WiFi Manager";
const char* wm_Password = "wmPassword";

void wifiInit() {
  WiFi.mode(WIFI_STA);  // Use if configuring for station mode, comment out if using AP mode

  bool res;
  res = wm.autoConnect(wm_SSID); // For a password protected AP, syntax is as follows wm.autoConnect(wm_SSID, wm_Password)
  if(!res) {
    Serial.println("Failed to Connect");
    // ESP.restart();
  }
  else {
    Serial.println("");
    Serial.println("WiFi Connected");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  }
  //wm.resetSettings();  // Reset WiFi settings and use manager AP
}

void notFound(AsyncWebServerRequest *request) {
  request->send_P(404, "text/plain", "Not Found");  // send_P used as the webpage is stored in Program Memory (flash) instead of RAM
}

// WebSocket Server Event Handling
void webSocketsEvent(uint8_t clientNum, WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    
    case WStype_DISCONNECTED:
      Serial.printf("[%u] - Disconnected\n", clientNum);
      break;
    
    case WStype_CONNECTED: {
      IPAddress clientIP = webSocketsServer.remoteIP(clientNum);
      Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\n", clientNum, clientIP[0], clientIP[1], clientIP[2], clientIP[3], payload);
      }
      break;
    
    case WStype_TEXT: {
      Serial.printf("Feedback JSON Data from Website: [%u] Text: %s\n", clientNum, payload);
      String message = String((char*)(payload));
      //Serial.println(message);

      DynamicJsonDocument JSON_Recv(200);                                       // De-serialize the data
      DeserializationError jsonError = deserializeJson(JSON_Recv, message);     // Parse the parameters we expect to receive and test if the parsing succeeds
      
      if (jsonError) {
        Serial.print("deserializeJson() failed: ");
        Serial.println(jsonError.c_str());
        return;
      }

      pageLoadTrigger = JSON_Recv["LoadTrigger"];                               // Set parameter values received from Website 
      door1_CMD       = JSON_Recv["door1_CMD"];
      door1_Light     = JSON_Recv["door1_Light"];
      door2_CMD       = JSON_Recv["door2_CMD"];
      door2_Light     = JSON_Recv["door2_Light"];
    }
      break;
  }
}

// Send JSON Data to webpage through WebSockets - JSON Formatting: {"VARIABLE" : VALUE}
void sendJSONData() {
  StreamString JSON_Send;
  StaticJsonDocument<200> JSON_Send_Temp;
  JSON_Send_Temp["door1_ZSO"]     = door1_ZSO;
  JSON_Send_Temp["door1_ZSC"]     = door1_ZSC;
  JSON_Send_Temp["door1_LockSts"] = door1_LockSts;
  JSON_Send_Temp["door2_ZSO"]     = door2_ZSO;
  JSON_Send_Temp["door2_ZSC"]     = door2_ZSC;
  JSON_Send_Temp["door2_LockSts"] = door2_LockSts;
  serializeJson(JSON_Send_Temp, JSON_Send);

  Serial.print("Broadcast JSON Data to Website: ");
  Serial.println(JSON_Send);
  webSocketsServer.broadcastTXT(JSON_Send);
}

void setup() {  
  // GPIO Pin Mode Definitions
  pinMode(pin_wifiReset,    INPUT_PULLUP);  // Wifi Related GPIO
  pinMode(pin_wifiStatus,   OUTPUT);
  
  pinMode(pin_door1CMD,     OUTPUT);        // Door 1
  pinMode(pin_door1Light,   OUTPUT);
  pinMode(pin_door1LockSW,  INPUT);
  pinMode(pin_door1ZSO,     INPUT_PULLUP);
  pinMode(pin_door1ZSC,     INPUT_PULLUP);

  pinMode(pin_door2CMD,     OUTPUT);        // Door 2
  pinMode(pin_door2Light,   OUTPUT);
  pinMode(pin_door2LockSW,  INPUT);
  pinMode(pin_door2ZSO,     INPUT_PULLUP);
  pinMode(pin_door2ZSC,     INPUT_PULLUP);

  // Start Serial Monitor
  Serial.begin(115200);

  // Initialize SPIFFS
  if(!SPIFFS.begin(true)){
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }

  // Initialize / Start WiFi
  wifiInit();

  // If other webpages besides / are requested, syntax would be webServer.on("/page1", HTTP_GET, [](AsyncWebServerRequest * request) - Note the HTTP_GET
  /* webServer.on("/", [](AsyncWebServerRequest * request) {
    request->send(200, "text/html", webpage);
  }); */

  // Route for / web page
  webServer.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/index.html", String(), false);  // String(), false, processor instead of webpage
  });
  // Route to Watch Web Page
  webServer.on("/watch", HTTP_GET, [](AsyncWebServerRequest *request){
    
    if(request->hasParam("door")){
      String reqMessage;
      reqMessage = request->getParam("door")->value();
      if(reqMessage == "1") {
        door1_CMD = 1;
      } else if (reqMessage == "2") {
        door2_CMD = 1;
      }
    }
    request->send(SPIFFS, "/index-watch.html", String(), false);  // String(), false, processor instead of webpage
  });
  webServer.on("/style.css", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/style.css", "text/css");
  });
  // Route to load app.js file
  webServer.on("/app.js", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/app.js", "text/javascript");
  });
  // Route to favicon
  webServer.on("/favicon.png", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/favicon.png", "image/png");
  });
  // Route to Locked icon
  webServer.on("/locked", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/locked.png", "image/png");
  });
  // Route to Unlocked icon
  webServer.on("/unlocked", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/unlocked.png", "image/png");
  });
  // Route to Blank icon (placeholder for locked / unlocked)
  webServer.on("/blank", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/blank.png", "image/png");
  });

  AsyncElegantOTA.begin(&webServer);  // Start OTA Server - Navigate to [IP Address]/update to use
  webServer.onNotFound(notFound);
  webServer.begin(); // Start web server
  webSocketsServer.begin(); // Start web sockets server
  webSocketsServer.onEvent(webSocketsEvent);
}

void loop() {
  // WIFI Handling
  if((WiFi.status() == WL_CONNECTED) && (WiFi.getMode() != 2)) {       // Set WiFi Status LED. WiFi.getMode of 2 = AP mode
    digitalWrite(pin_wifiStatus, HIGH);
  } else {
    digitalWrite(pin_wifiStatus, LOW);
  }
  
  unsigned long currentTime = millis();     // Check for WiFi disconnection and attempt to reconnect
  if((WiFi.status() != WL_CONNECTED) && (currentTime - wifi_Previous_Time >= wifi_Reconnect_Delay)) {
    Serial.println("Disconnected from WiFi Network. Attempting to Reconnect");
    WiFi.disconnect();
    WiFi.reconnect();
    wifi_Previous_Time = currentTime;
  }

  if(!digitalRead(pin_wifiReset)) {
    WiFi.disconnect();
    digitalWrite(pin_wifiStatus, LOW);
    wm.resetSettings();
    wifiInit();
  }

  // Websockets Server handling / loop
  webSocketsServer.loop();

  // Listen for Web Page Load Trigger. If received, send JSON data to update status indicators
  if(pageLoadTrigger){
    sendJSONData();
    pageLoadTrigger = 0;
  }

  // Buffer door status before reading values from GPIO
  for (int i = 0; i < 6; i++) {
    doorStatusBuffer[i] = doorStatus[i];
  }
  
  // Process Inputs - Door 1 (invert due to internal pullup for logic purposes)
  door1_LockSts = !digitalRead(pin_door1LockSW);  // Configured as inverted because if signal is not present on GPIO, door is locked
  door1_ZSO     = !digitalRead(pin_door1ZSO);
  door1_ZSC     = !digitalRead(pin_door1ZSC);

  // Process Inputs - Door 2 (invert due to internal pullup for logic purposes)
  door2_LockSts = !digitalRead(pin_door2LockSW);  // Configured as inverted because if signal is not present on GPIO, door is locked
  door2_ZSO     = !digitalRead(pin_door2ZSO);
  door2_ZSC     = !digitalRead(pin_door2ZSC);
  
  // Build input status array
  doorStatus[0] = door1_LockSts; doorStatus[1] = door1_ZSO; doorStatus[2] = door1_ZSC;
  doorStatus[3] = door2_LockSts; doorStatus[4] = door2_ZSO; doorStatus[5] = door2_ZSC;

  // Compare input status array with buffered values. If different, 
  for (int j = 0; j < 6; j++) {
    if (doorStatus[j] != doorStatusBuffer[j]) {
      sendJSONData();
      break;
    }
  }

  if (door1_CMD) {
    digitalWrite(pin_door1CMD, HIGH);
    // One-Shot to set initial time
    if (door1_CMD_Latch != 1) {
      door1_CMD_Time = millis();
    }
    door1_CMD_Latch = 1;
    // Timer to hold output HIGH
    if (millis() - door1_CMD_Time > cmdHoldPeriod) {
      digitalWrite(pin_door1CMD, LOW);
      door1_CMD = 0;
      door1_CMD_Latch = 0;
    } 
  }

  if (door1_Light) {
    digitalWrite(pin_door1Light, HIGH);
    // One-Shot to set initial time
    if (door1_Light_Latch != 1) {
      door1_Light_Time = millis();
    }
    door1_Light_Latch = 1;
    // Timer to hold output HIGH
    if (millis() - door1_Light_Time > cmdHoldPeriod) {
      digitalWrite(pin_door1Light, LOW);
      door1_Light = 0;
      door1_Light_Latch = 0;
    }
  }

  if (door2_CMD) {
    digitalWrite(pin_door2CMD, HIGH);
    // One-Shot to set initial time
    if (door2_CMD_Latch != 1) {
      door2_CMD_Time = millis();
    }
    door2_CMD_Latch = 1;
    // Timer to hold output HIGH
    if (millis() - door2_CMD_Time > cmdHoldPeriod) {
      digitalWrite(pin_door2CMD, LOW);
      door2_CMD = 0;
      door2_CMD_Latch = 0;
    }
  }

  if (door2_Light) {
    digitalWrite(pin_door2Light, HIGH);
    // One-Shot to set initial time
    if (door2_Light_Latch != 1) {
      door2_Light_Time = millis();
    }
    door2_Light_Latch = 1;
    // Timer to hold output HIGH
    if (millis() - door2_Light_Time > cmdHoldPeriod) {
      digitalWrite(pin_door2Light, LOW);
      door2_Light = 0;
      door2_Light_Latch = 0;
    }
  }
}