#include <ESP8266WiFi.h>          //ESP8266 Core WiFi Library (you most likely already have this in your sketch)
#include <DNSServer.h>            //Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h>     //Local WebServer used to serve the configuration portal
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic
#include <PubSubClient.h>         //MQTT Handler
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ArduinoJson.h>          // https://github.com/bblanchon/ArduinoJson
#include <Adafruit_Sensor.h>
#include <Adafruit_AM2320.h>


////**********START CUSTOM PARAMS******************//

#include "config.h"

//Define the pins
const int RELAY_PIN = D1;
const int DOOR_PIN = D2;
const int SDA_PIN = D6;
const int SCL_PIN = D5;
const int ACCESS_DOOR_PIN = D7;

//Define MQTT Params. 
#define door_topic "garage/door"
#define button_topic "garage/button"
#define temp_topic "garage/sensor1"
#define access_door_topic "garage/access_door"

//************END CUSTOM PARAMS********************//
//This can be used to output the date the code was compiled
const char compile_date[] = __DATE__ " " __TIME__;

//Setup the web server for http OTA updates. 
ESP8266WebServer httpServer(80);
ESP8266HTTPUpdateServer httpUpdater;

WiFiClient espClient;

//Initialize MQTT
PubSubClient client(espClient);

//Setup Variables
String switch1;
String strTopic;
String strPayload;
char* door_state = "UNDEFINED";
char* last_state = "";
int open_count = 0;
long lastTempReport = 0;
char* access_door_state = "UNDEFINED";
char* access_last_state = "UNDEFINED";
int access_open_count = 0;

//Wifi Manager will try to connect to the saved AP. If that fails, it will start up as an AP
//which you can connect to and setup the wifi
WiFiManager wifiManager;
long lastMsg = 0;
long lastAccessMsg = 0;

Adafruit_AM2320 am2320 = Adafruit_AM2320();

void setup() {
  //Set Relay(output) and Door(input) pins
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  pinMode(DOOR_PIN, INPUT);
  pinMode(ACCESS_DOOR_PIN, INPUT);

  Serial.begin(115200);

  //Set the wifi config portal to only show for 3 minutes, then continue.
  wifiManager.setConfigPortalTimeout(180);
  wifiManager.autoConnect(CONFIG_OTA_HOST);

  //sets up the mqtt server, and sets callback() as the function that gets called
  //when a subscribed topic has data
  client.setServer(CONFIG_MQTT_HOST, CONFIG_MQTT_PORT);
  client.setCallback(callback); //callback is the function that gets called for a topic sub

  //setup http firmware update page.
  MDNS.begin(CONFIG_OTA_HOST);
  httpUpdater.setup(&httpServer, CONFIG_OTA_PATH, CONFIG_OTA_USER, CONFIG_OTA_PASS);
  httpServer.begin();
  MDNS.addService("http", "tcp", 80);
  Serial.printf("HTTPUpdateServer ready! Open http://%s.local%s in your browser and login with username '%s' and your password\n", CONFIG_OTA_HOST, CONFIG_OTA_PATH, CONFIG_OTA_USER);

  // Setup AM2320 - using D5 as SCL and D6 as SDA
  Serial.println("setup am2320");
  //hangs without i2c device
  if(!am2320.begin(SDA_PIN,SCL_PIN)){
     Serial.println("Device error!");
  }
  Serial.println("done");

}

void loop() {
  //If MQTT client can't connect to broker, then reconnect
  if (!client.connected()) {
    reconnect();
  }
  checkDoorState();
  checkAccessDoorState();
  
  reportTempPress();
  
  client.loop(); //the mqtt function that processes MQTT messages
  
  httpServer.handleClient(); //handles requests for the firmware update page
}

void callback(char* topic, byte* payload, unsigned int length) {
  //if the 'garage/button' topic has a payload "OPEN", then 'click' the relay
  payload[length] = '\0';
  strTopic = String((char*)topic);
  if (strTopic == button_topic)
  {
    switch1 = String((char*)payload);
    if (switch1 == "OPEN")
    {
      //'click' the relay
      Serial.println("PUSHING BUTTON");
      digitalWrite(RELAY_PIN, HIGH);
      delay(600);
      digitalWrite(RELAY_PIN, LOW);
    }
  }
}

void checkDoorState() {
  //Checks if the door state has changed, and MQTT pub the change
  
  last_state = door_state; //get previous state of door
  
  if (digitalRead(DOOR_PIN) == 0){ // get new state of door
    open_count++;
    if(open_count > 5){
      door_state = "OPENED";
      open_count = 0;
    }
  }
  else if (digitalRead(DOOR_PIN) == 1){
    open_count = 0;
    door_state = "CLOSED"; 
  }

  if (last_state != door_state) { // if the state has changed then publish the change
    if (!client.connected()) {
      reconnect();
    }
    client.publish(door_topic, door_state);
    Serial.println(door_state);
  }
  //pub every minute, regardless of a change.
  long now = millis();
  if(lastMsg > now) lastMsg = now;
  if ((now - lastMsg) > 60000) {
    lastMsg = now;
    if (!client.connected()) {
      reconnect();
    }
    client.publish(door_topic, door_state);
  }
}

void checkAccessDoorState() {
  //Checks if the door state has changed, and MQTT pub the change
  
  if (digitalRead(ACCESS_DOOR_PIN) == 0){ // get new state of door
    access_open_count++;
    if(access_open_count > 10){
      access_door_state = "ON";
      access_open_count = 0;
    }
  }
  else {
    access_open_count = 0;
    access_door_state = "OFF"; 
  }

  if (access_last_state != access_door_state) { // if the state has changed then publish the change
    if (!client.connected()) {
      reconnect();
    }
    client.publish(access_door_topic, access_door_state);
    Serial.print("Old access state: ");
    Serial.println(access_last_state);
    Serial.print("New access_state: ");
    Serial.println(access_door_state);
    access_last_state = access_door_state; //set previous state of door
  } 
  
  //pub every minute, regardless of a change.
  long now = millis();
  if(lastAccessMsg > now) lastAccessMsg = now;
  if ((now - lastAccessMsg) > 60000) {
    lastAccessMsg = now;
    if (!client.connected()) {
      reconnect();
    }
    client.publish(access_door_topic, access_door_state);
    Serial.print("1 minute access update: ");
    Serial.println(access_door_state);
  }
}

void reportTempPress(){
  
  float T, H;
  
  long now = millis();

  if(now < lastTempReport) lastTempReport = now;

  if ((now - lastTempReport) > 600000 || lastTempReport == 0){  // Report first time then every 10 minutes
    lastTempReport = now;
    T = am2320.readTemperature();
    H = am2320.readHumidity();
    
    if(H!=0) // Functions return 0 with error. Humidity should never be 0, right? 
    {
      Serial.print("T = \t");Serial.print(T,2); Serial.print(" degC\t");
      Serial.print("H = \t");Serial.print(H,2); Serial.println(" %");

      publishResults(T,H);
    }
    else {
      Serial.println("Error2: Failed to read from AM2320.");
    } 
  }
}

void publishResults(float T, float H){
          
  // create a JSON object
  // doc : https://github.com/bblanchon/ArduinoJson/wiki/API%20Reference
  StaticJsonBuffer<200> jsonBuffer;
  JsonObject& root = jsonBuffer.createObject();
  // INFO: the data must be converted into a string; a problem occurs when using floats...
  root["temperature"] = (String)T;
  root["humidity"] = (String)H;
  root.prettyPrintTo(Serial);
  Serial.println("");
  char data[200];
  root.printTo(data, root.measureLength() + 1);
  if (!client.connected()) {
    reconnect();
  }
  client.publish(temp_topic, data, true);
}

void reconnect() {
  //Reconnect to Wifi and to MQTT. If Wifi is already connected, then autoconnect doesn't do anything.
  
  wifiManager.autoConnect(CONFIG_OTA_HOST);
  
  Serial.print("Attempting MQTT connection...");
  if (client.connect(CONFIG_MQTT_CLIENT_ID, CONFIG_MQTT_USER, CONFIG_MQTT_PASS)) {
    Serial.println("connected");
    client.subscribe("garage/#");
  } else {
    Serial.print("failed, rc=");
    Serial.print(client.state());
    Serial.println(" try again in 5 seconds");
    // Wait 5 seconds before retrying
    delay(5000);
  }
}
