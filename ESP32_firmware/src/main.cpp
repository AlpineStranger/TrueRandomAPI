#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <SafeQueue.cpp>
#include <freertos/FreeRTOS.h> //needed to use both cores on ESP32
#include <wifi_secrets.h>
// create a file wifi_secrets.h in ../include
// file should contain 2 lines
// const char *SSID = "Your WiFi name here";
// const char *PWD = "Your Wifi password here";


#define MAX_RANDOM_LIMIT 65535

// Web server running on port 80
WebServer server(80);
 
// JSON data buffer
StaticJsonDocument<250> jsonDocument;
char buffer[250];

// Stack for randoms

SafeQueue<unsigned short int> queueRandoms;
const int maxSize = 50000;

// env variables
const int GM_pin = 13; //Pin GM counter connected to
volatile boolean detected = false; //GM detector flag

void connectToWiFi() {
  Serial.print("Connecting to ");
  Serial.println(SSID);
  
  WiFi.begin(SSID, PWD);
  
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
 
  Serial.print("Connected. IP: ");
  Serial.println(WiFi.localIP());
}

void add_json_object(char *tag, int value) {
  JsonObject obj = jsonDocument.createNestedObject();
  obj["type"] = tag;
  obj["value"] = value;
}

void create_json(char *tag, int value) {  
  jsonDocument.clear();  
  jsonDocument["type"] = tag;
  jsonDocument["value"] = value;
  serializeJson(jsonDocument, buffer);
}

TaskHandle_t core0Task; //core 0 function to fill stack

void GM_ping(){
  detected = true;
}


void getInt() {
  Serial.println("Get Random Int");
  jsonDocument.clear();
  
  Serial.println("taking from queue...");

  int rnd_return = queueRandoms.dequeue();

  //enforce no entropy loss if max provided
  if (server.hasArg("max")){
    int provided_max = (server.arg("max")).toInt();
    Serial.print("Max requested: ");
    Serial.println(provided_max);

    int entropy_max = int((MAX_RANDOM_LIMIT + 1) / provided_max)*provided_max ;
    // Serial.println(entropy_max);
    // Serial.println(rnd_return);
    while (rnd_return>entropy_max)//if dequeued number is outside entropy_max then redo
    {
      rnd_return = queueRandoms.dequeue();
    }
    
    rnd_return = rnd_return % provided_max;
  }

  
  create_json((char*)"RandomInt", rnd_return);
  
  
  server.send(200, "application/json", buffer);

}

void getSize(){
  create_json((char*)"BufferSize", queueRandoms.size());
  
  
  server.send(200, "application/json", buffer);
}

void setup_routing() {	 	 
  //server.on("/getRandomInt", getRandomInt);	 	
  pinMode(GM_pin,INPUT); 
  server.on("/getRandomInt", getInt);
  server.on("/getBufferSize", getSize); 	 	 
  // start server	 	 
  server.begin();	 	 
}
 

 

void handlePost() {
  if (server.hasArg("plain") == false) {
    //handle error here
  }
  String body = server.arg("plain");
  deserializeJson(jsonDocument, body);
  
  // Respond to the client
  server.send(200, "application/json", "{}");
}

void fillQueue(void*){
  while (true)
  {
    while (queueRandoms.size() < maxSize)
    {
      // Serial.println("filling queue...");
      Serial.print("Queue size: ");
      Serial.println(queueRandoms.size());
      if(detected){
        detected = false;
        break;
      } //if not reset return back
      unsigned short int randomInt = 0 //reset counter
      
      // interrupts(); //enable interrupts
      XTOS_SET_INTLEVEL(XCHAL_EXCM_LEVEL); portbenchmarkINTERRUPT_DISABLE();

      attachInterrupt(digitalPinToInterrupt(GM_pin), GM_ping, FALLING); //Call GM_ping on falling edge
      while(not(detected)){
        randomInt++; //run counter until GM hit
        if (randomInt > MAX_RANDOM_LIMIT)
        {
          //Serial.println("tick");
          randomInt = 0;
        }
        
      }
      detachInterrupt(digitalPinToInterrupt(GM_pin));
      // noInterrupts(); //disable interrupts
      portbenchmarkINTERRUPT_RESTORE(0); XTOS_SET_INTLEVEL(0);
      queueRandoms.enqueue(randomInt);
      detected = false;
    }
  }
  
}

void setup() {	 	 
  Serial.begin(115200);	 	 
 	 	 
  connectToWiFi();	 	 
  setup_routing(); 	

  xTaskCreatePinnedToCore(
    fillQueue,
    "core0Task",
    10000,
    NULL,
    0,
    &core0Task,
    0
  );


}	 	 
  	 	 
void loop() {

  	 	 
  server.handleClient();
  	 
}