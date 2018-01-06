#include <ESP8266WiFi.h>          //ESP8266 Core WiFi Library (you most likely already have this in your sketch)
#include <DNSServer.h>            //Local DNS Server used for redirecting all requests to the configuration portal
#include <ESP8266WebServer.h>     //Local WebServer used to serve the configuration portal
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager WiFi Configuration Magic
#include <PubSubClient.h>         //MQTT Handler
#include <ESP8266mDNS.h>
#include <ESP8266HTTPUpdateServer.h>
#include <ArduinoJson.h>          // https://github.com/bblanchon/ArduinoJson
#include "BMP280.h"               // https://github.com/mahfuz195/BMP280-Arduino-Library
#include "Wire.h"


////**********START CUSTOM PARAMS******************//

//Define parameters for the http firmware update
const char* host = "GarageESP";
const char* update_path = "/WebFirmwareUpgrade";
const char* update_username = "admin";
const char* update_password = "SECRET";  // Use your favorite password here

//Define the pins
const int RELAY_PIN = D1;
const int DOOR_PIN = D2;
const int SDA_PIN = D6;
const int SCL_PIN = D5;

//Define MQTT Params. 
#define mqtt_server "192.168.254.118"  // Use your mosquitto IP or host name here
#define door_topic "garage/door"
#define button_topic "garage/button"
#define temp_topic "garage/sensor1"
const char* mqtt_user = "pi";      // Use your mosquitto username here
const char* mqtt_pass = "SECRET";  // Use your mosquitto password here

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

//Wifi Manager will try to connect to the saved AP. If that fails, it will start up as an AP
//which you can connect to and setup the wifi
WiFiManager wifiManager;
long lastMsg = 0;

BMP280 bmp;

void setup() {
  //Set Relay(output) and Door(input) pins
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  pinMode(DOOR_PIN, INPUT);

  Serial.begin(115200);

  //Set the wifi config portal to only show for 3 minutes, then continue.
  wifiManager.setConfigPortalTimeout(180);
  wifiManager.autoConnect(host);

  //sets up the mqtt server, and sets callback() as the function that gets called
  //when a subscribed topic has data
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback); //callback is the function that gets called for a topic sub

  //setup http firmware update page.
  MDNS.begin(host);
  httpUpdater.setup(&httpServer, update_path, update_username, update_password);
  httpServer.begin();
  MDNS.addService("http", "tcp", 80);
  Serial.printf("HTTPUpdateServer ready! Open http://%s.local%s in your browser and login with username '%s' and your password\n", host, update_path, update_username);

  // Setup BMP280 - using D5 as SCL and D6 as SDA
  if(!bmp.begin(SDA_PIN,SCL_PIN)){ 
    Serial.println("BMP init failed!");
    while(1);
  }
  else Serial.println("BMP init success!");
  
  bmp.setOversampling(4);

}

void loop() {
  //If MQTT client can't connect to broker, then reconnect
  if (!client.connected()) {
    reconnect();
  }
  checkDoorState();
  
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
    client.publish(door_topic, door_state);
    Serial.println(door_state);
  }
  //pub every minute, regardless of a change.
  long now = millis();
  if(lastMsg > now) lastMsg = now;
  if ((now - lastMsg) > 60000) {
    lastMsg = now;
    client.publish(door_topic, door_state);
  }
}

void reportTempPress(){
  
  double T,P;
  
  long now = millis();

  if(now < lastTempReport) lastTempReport = now;

  if ((now - lastTempReport) > 600000 || lastTempReport == 0){  // Report first time then every 10 minutes
    lastTempReport = now;

    char result = bmp.startMeasurment();
   
    if(result!=0){
      delay(result);
      result = bmp.getTemperatureAndPressure(T,P);
      
        if(result!=0)
        {
          
          Serial.print("T = \t");Serial.print(T,2); Serial.print(" degC\t");
          Serial.print("P = \t");Serial.print(P,2); Serial.println(" mBar");
          
          // create a JSON object
          // doc : https://github.com/bblanchon/ArduinoJson/wiki/API%20Reference
          StaticJsonBuffer<200> jsonBuffer;
          JsonObject& root = jsonBuffer.createObject();
          // INFO: the data must be converted into a string; a problem occurs when using floats...
          root["temperature"] = (String)T;
          root["pressure"] = (String)P;
          root.prettyPrintTo(Serial);
          Serial.println("");
          char data[200];
          root.printTo(data, root.measureLength() + 1);
          client.publish(temp_topic, data, true);
         
        }
        else {
          Serial.println("Error2: Failed to read from BMP280.");
        }
    }
    else {
      Serial.println("Error1: Failed to read from BMP280.");
    }
  }
}

void reconnect() {
  //Reconnect to Wifi and to MQTT. If Wifi is already connected, then autoconnect doesn't do anything.
  
  wifiManager.autoConnect(host);
  
  Serial.print("Attempting MQTT connection...");
  if (client.connect(host, mqtt_user, mqtt_pass)) {
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
