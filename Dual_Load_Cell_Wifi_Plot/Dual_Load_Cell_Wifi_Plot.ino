/* Links for Codes Used
Load Cells:
  https://github.com/olkal/HX711_ADC
Web Server:
  https://github.com/esp8266/Arduino/tree/master/libraries/ESP8266WebServer
Websockets:
  https://www.hackster.io/brzi/nodemcu-websockets-tutorial-3a2013
Plotting on Graphs:
  https://www.meccanismocomplesso.org/en/jqplot-hot-to-draw-real-time-data-on-a-line-chart/
*/

#include <ESP8266WiFi.h> // wifi
#include <WebSocketsServer.h> // Socket Server
#include <ESP8266WebServer.h> // Web Server
#include "FS.h" // SPIFFS file handler


// Libraries for Load Cell
#include <HX711_ADC.h>
#if defined(ESP8266)|| defined(ESP32) || defined(AVR)
#include <EEPROM.h>
#endif

//Variables Load Cell:
const int HX711_dout_1 = 2; //mcu > this is pin D4
const int HX711_sck_1 = 14; //mcu > This is pin D5
const int HX711_dout_2 = 12; //mcu >This i pin D6
const int HX711_sck_2 = 13; //mcu > This is pin D7
float total = 0;

//HX711 constructor (dout pin, sck pin)
HX711_ADC LoadCell_1(HX711_dout_1, HX711_sck_1); //HX711 1
HX711_ADC LoadCell_2(HX711_dout_2, HX711_sck_2); //HX711 2

const int calVal_eepromAdress_1 = 0; // eeprom adress for calibration value load cell 1 (4 bytes)
const int calVal_eepromAdress_2 = 4; // eeprom adress for calibration value load cell 2 (4 bytes)
unsigned long t = 0;

// Timing Stuff
int lastMillis = 0;


//Variables WebSocket
const char* ssid = "Kroon"; // Your local wifi network name
const char* password = "kroonguest"; // Your local wifi password
String getContentType(String filename); // file MIME content type
bool handleFileRead(String path);       // identifier of a file known as a handle
char* c = "empty";
int count = 0;
ESP8266WebServer server(80);    // Create a webserver listening on port 80 for browser connections
WebSocketsServer webSocket(81); // create a websocket server on port 81 for arduino to send data to browser graphs


/*************** Setup ****************************************************************************************/
void setup() {

  Serial.begin(115200); // communication with the host computer
  delay(10);
  Serial.println("\n");

  /***********  web Socket setup *************************************************/
  webSocket.begin(); // Start websocket for communication with graphs in browser
  Serial.println("WebSocket server started.");

  /***********  Flash File System Setup ******************************************/
  // SPIFFS is the SPI Flash (memory) File System https://tttapa.github.io/ESP8266/Chap11%20-%20SPIFFS.html
  if (!SPIFFS.begin()) {
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }

  /***********  Load Cell Setup *************************************************/
  loadcellSetup();

  /***********  Microphone Setup ************************************************/
 

  /*********** Wifi Setup *******************************************************/
  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");
  // Print the IP address
  Serial.println(WiFi.localIP()); // print the local server IP so you can browse to it
  server.onNotFound([]() {                              // If the client requests any URI
    if (!handleFileRead(server.uri()))                  // send it if it exists
      server.send(404, "text/plain", "404: Not Found"); // otherwise, respond with a 404 (Not Found) error
  });

  /*********** Webserver startup *****************************************************/
  server.begin();
  Serial.println("HTTP Server started");

}


/*********** Main program loop **************************************************************************************************************************/
/********************************************************************************************************************************************************/
/********************************************************************************************************************************************************/

void loop() {
  float loadValue = loadCell();
  webSocket.loop();  // constantly check for websocket events
  server.handleClient(); // handle browser requests
  //Serial.println(loadValue);
  if (millis() > (lastMillis + 250)) {
    dtostrf(loadValue, 5, 3, c);
    webSocket.broadcastTXT(c, sizeof(c));
    lastMillis = millis();
  }

}

/*** Function to convert the file extension to the MIME type **********************************************************/
String getContentType(String filename) {
  if (filename.endsWith(".html")) return "text/html";
  else if (filename.endsWith(".css")) return "text/css";
  else if (filename.endsWith(".js")) return "application/javascript";
  else if (filename.endsWith(".ico")) return "image/x-icon";
  return "text/plain";
}


/*** Function to handle browser file requests defaults to index.html ***************************************************/
bool handleFileRead(String path) { // send the right file to the client (if it exists)
  Serial.println("handleFileRead: " + path);
  if (path.endsWith("/")) path += "index.html";         // If a folder is requested, send the index file
  String contentType = getContentType(path);            // Get the MIME type
  if (SPIFFS.exists(path)) {                            // If the file exists
    File file = SPIFFS.open(path, "r");                 // Open it
    size_t sent = server.streamFile(file, contentType); // And send it to the client
    file.close();                                       // Then close the file again
    return true;
  }
  Serial.println("\tFile Not Found");
  return false;                                         // If the file doesn't exist, return false
}


/* load cell Function *****************************************************************************************/

float loadCell() {

  static boolean newDataReady = 0;
  const int serialPrintInterval = 500; //increase value to slow down serial print activity

  // check for new data/start next conversion:
  if (LoadCell_1.update()) newDataReady = true;
  LoadCell_2.update();

  //get smoothed value from data set
  if ((newDataReady)) {
    if (millis() > t + serialPrintInterval) {
      float a = LoadCell_1.getData();
      float b = LoadCell_2.getData();
      total = a-b;
      newDataReady = 0;
      t = millis();
    }
  }

  // receive command from serial terminal, send 't' to initiate tare operation:
  if (Serial.available() > 0) {
    char inByte = Serial.read();
    if (inByte == 't') {
      LoadCell_1.tareNoDelay();
      LoadCell_2.tareNoDelay();
    }
  }

  //check if last tare operation is complete
  if (LoadCell_1.getTareStatus() == true) {
    Serial.println("Tare load cell 1 complete");
  }
  if (LoadCell_2.getTareStatus() == true) {
    Serial.println("Tare load cell 2 complete");
  }
  return (-total);
}


/* load cell Setup *****************************************************************************************/
void loadcellSetup() {

  float calibrationValue_1 = 49657.00; // uncomment this if you want to set this value in the sketch
  float calibrationValue_2 = 45894.00; // uncomment this if you want to set this value in the sketch


  LoadCell_1.begin();
  LoadCell_2.begin();
  unsigned long stabilizingtime = 2000; // tare preciscion can be improved by adding a few seconds of stabilizing time
  boolean _tare = true; //set this to false if you don't want tare to be performed in the next step
  byte loadcell_1_rdy = 0;
  byte loadcell_2_rdy = 0;
  while ((loadcell_1_rdy + loadcell_2_rdy) < 2) { //run startup, stabilization and tare, both modules simultaniously
    if (!loadcell_1_rdy) loadcell_1_rdy = LoadCell_1.startMultiple(stabilizingtime, _tare);
    if (!loadcell_2_rdy) loadcell_2_rdy = LoadCell_2.startMultiple(stabilizingtime, _tare);
  }
  if (LoadCell_1.getTareTimeoutFlag()) {
    Serial.println("Timeout, check MCU>HX711 no.1 wiring and pin designations");
  }
  if (LoadCell_2.getTareTimeoutFlag()) {
    Serial.println("Timeout, check MCU>HX711 no.2 wiring and pin designations");
  }
  LoadCell_1.setCalFactor(calibrationValue_1); // user set calibration value (float)
  LoadCell_2.setCalFactor(calibrationValue_2); // user set calibration value (float)
  Serial.println("Startup is complete");

}
