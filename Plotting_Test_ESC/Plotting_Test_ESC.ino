/* Links for Codes Used
Load Cells:
  https://github.com/olkal/HX711_ADC
Tachometer:
  https://create.arduino.cc/projecthub/PracticeMakesBetter/easy-peasy-tachometer-20e73a
 Motor Control:
  https://howtomechatronics.com/tutorials/arduino/arduino-brushless-motor-control-tutorial-esc-bldc/
Web Server:
  https://github.com/esp8266/Arduino/tree/master/libraries/ESP8266WebServer
Websockets:
  https://www.hackster.io/brzi/nodemcu-websockets-tutorial-3a2013
Plotting on Graphs:
  https://www.meccanismocomplesso.org/en/jqplot-hot-to-draw-real-time-data-on-a-line-chart/
Timed Action:
  https://github.com/Glumgad/TimedAction
*/

//Timing Split
#include <TimedAction.h>

// ESC Library/ declration
#include <Servo.h>
Servo ESC;

//Wifi Libraries
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
const int HX711_dout = 2; //mcu > HX711 dout pin
const int HX711_sck = 14; //mcu > HX711 sck pin
float calibrationValue; // calibration value (see example file "Calibration.ino")
static boolean newDataReady = 0;
const int serialPrintInterval = 0; //increase value to slow down serial print activity
float i = 0;
float loadValue = 0;

// Timing stuff
int lastMillis = 0;
int oldMillis = 0;

//HX711 constructor:
HX711_ADC LoadCell(HX711_dout, HX711_sck);
const int calVal_eepromAdress = 0;
unsigned long t = 0;

//ESC Variables
int buttonPin = 4;
int escPower = 0;
int escPin = 15;
int buttonState = 0;

//Variables Tachometer
float value = 0;
float rev = 0;
int rpm;
int oldtime = 0;
int deltatime;
int pinTach = 12;

//Wifi Variables
const char* ssid = "Island"; // Your local wifi network name
const char* password = "Fuckyoukyl3"; // Your local wifi password
String getContentType(String filename); // file MIME content type
bool handleFileRead(String path);       // identifier of a file known as a handle
char* c = "empty";
int count = 0;

//Starting the Websocket and Server
ESP8266WebServer server(80);    // Create a webserver listening on port 80 for browser connections
WebSocketsServer webSocket(81); // create a websocket server on port 81 for arduino to send data to browser graphs

//Interupt for the Tachometer
void ICACHE_RAM_ATTR isr() {
  rev++;
}

/***********Void Setup ***************************************************************************************************************  *************************************************/
void setup() {

  loadcellSetup();
  Serial.begin(115200); // communication with the host computer
  delay(10);
  Serial.println("\n");

  //web Socket setup
  webSocket.begin(); // Start websocket for communication with graphs in browser
  Serial.println("WebSocket server started.");

  //Flash File System Setup
  // SPIFFS is the SPI Flash (memory) File System https://tttapa.github.io/ESP8266/Chap11%20-%20SPIFFS.html
  if (!SPIFFS.begin()) {
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }

  // Wifi Setup
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

  // Webserver startup
  server.begin();
  Serial.println("HTTP Server started");

  //ESC Setup
  ESC.attach(escPin, 1000, 2000); // (pin, min pulse width, max pulse width in microseconds)

  //Tachometer Setup
  attachInterrupt(digitalPinToInterrupt(pinTach), isr, RISING); //attaching the interrupt
}

/*Starting Threads*******************************************************************************************************************************************************************/
TimedAction rpmThread = TimedAction(1000, tachMeasure);
TimedAction loadThread = TimedAction(10, loadCell);
TimedAction chartThread = TimedAction(2000, sendchartData);


/*********** Main program loop ******************************************************************************************************************************************************/
/************************************************************************************************************************************************************************************/
void loop() {
  webSocket.loop();  // constantly check for websocket events
  server.handleClient(); // handle browser requests

  buttonState = digitalRead(buttonPin);

  if (buttonState == HIGH) {
    resetChart();
    for (float p = 0; p <= 10; p++) {
      escPower = 10 * p;
      ESC.write(escPower);
      oldMillis = millis();
      while (millis() < (oldMillis + 2000)) {
        loadThread.check();
        rpmThread.check();
        chartThread.check();
      }
      serialOutput();
    }
  }
  else {
    escPower = 0;
    ESC.write(escPower);
    loadThread.check();
    rpmThread.check();
  }
}

/* Sending Data with Socket ****************************************************************************************************************************************************/
void sendchartData() {
  String mystr = String(i) + "," + String(rpm) + "," + String(escPower);
  int str_len = mystr.length() + 1;
  char char_array[str_len];
  mystr.toCharArray(char_array, str_len);
  webSocket.broadcastTXT(char_array, sizeof(char_array));
}

/* Reseting Chart **************************************************************************************************************************************************************/
void resetChart() {
  String resetStr = "r";
  int str_leng = resetStr.length() + 1;
  char char_arrayRst[str_leng];
  resetStr.toCharArray(char_arrayRst, str_leng);
  webSocket.broadcastTXT(char_arrayRst, sizeof(char_arrayRst));
}

/*** Function to convert the file extension to the MIME type *******************************************************************************************************************/
String getContentType(String filename) {
  if (filename.endsWith(".html")) return "text/html";
  else if (filename.endsWith(".css")) return "text/css";
  else if (filename.endsWith(".js")) return "application/javascript";
  else if (filename.endsWith(".ico")) return "image/x-icon";
  return "text/plain";
}


/*** Function to handle browser file requests defaults to index.html ***********************************************************************************************************/
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


/* Load cell measurements ******************************************************************************************************************************************************/
void loadCell() {
  //Load Cell loop
  // check for new data/start next conversion:
  if (LoadCell.update()) newDataReady = true;

  // get smoothed value from the dataset:
  if (newDataReady) {
    if (millis() > t + serialPrintInterval) {
      i = LoadCell.getData();
      //Serial.print("Load_cell output val: ");
      //Serial.println(i);
      newDataReady = 0;
      t = millis();
    }
  }
  // receive command from serial terminal, send 't' to initiate tare operation:
  if (Serial.available() > 0) {
    char inByte = Serial.read();
    if (inByte == 't') LoadCell.tareNoDelay();
  }

  // check if last tare operation is complete:
  if (LoadCell.getTareStatus() == true) {
    Serial.println("Tare complete");
  }
}

/* Load cell Setup ************************************************************************************************************************************************************/
void loadcellSetup() {
  LoadCell.begin();
#if defined(ESP8266)|| defined(ESP32)
  EEPROM.begin(512); // uncomment this if you use ESP8266/ESP32 and want to fetch the calibration value from eeprom
#endif
  EEPROM.get(calVal_eepromAdress, calibrationValue); // uncomment this if you want to fetch the calibration value from eeprom

  unsigned long stabilizingtime = 2000; // preciscion right after power-up can be improved by adding a few seconds of stabilizing time
  boolean _tare = true; //set this to false if you don't want tare to be performed in the next step
  LoadCell.start(stabilizingtime, _tare);
  if (LoadCell.getTareTimeoutFlag()) {
    Serial.println("Timeout, check MCU>HX711 wiring and pin designations");
    while (1);
  }
  else {
    LoadCell.setCalFactor(calibrationValue); // set calibration value (float)
    Serial.println("Startup is complete");
  }
}

/* Serial Print Data ************************************************************************************************************************************************************/
void serialOutput() {
  Serial.print(rpm); Serial.print(", ");
  Serial.print(i); Serial.print(", ");
  Serial.println(escPower);
}

/* Tachometer  ******************************************************************************************************************************************************************/
void tachMeasure() {
  if (millis() - oldtime > 1000) {
    detachInterrupt(digitalPinToInterrupt(pinTach));           //detaches the interrupt
    deltatime = millis() - oldtime;    //finds the time
    rpm = (rev / deltatime) * 60000;   //calculates rpm
    oldtime = millis();           //saves the current time

    rev = 0;
    attachInterrupt(digitalPinToInterrupt(pinTach), isr, RISING);
  }
}
