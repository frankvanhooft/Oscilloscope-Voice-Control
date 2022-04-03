//
// Oscilloscope Voice Control
//
// ESP32, Elechouse VR3 Voice Recognition Module, SSD1306 I2C OLED
//
// Requires the VR3 to be trained. For this application the first 3 records of the VR3 are trained
// with STOP, SINGLE, and RUN respectively. When one of those words is recognized by the VR3, 
// the ESP32 sends an appropriate command to the Rigol MSO5000 oscilloscope via WiFi. Status
// information is displayed on the OLED.
//
// (c) Frank Van Hooft 2022
// License: Do as you wish, give credit where credit is due. No restrictions.
//

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "VoiceRecognitionV3.h"
#include <WiFi.h>

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define SCREEN_ROTATION 2 // OLED display rotation, index 0,1,2,3


// These #defines used for positioning text on the OLED
#define OLED_YPOS_TITLE  0
#define OLED_YPOS_WIFI   18
#define OLED_YPOS_SCOPE  28
#define OLED_YPOS_VR3    38
#define OLED_YPOS_WORD   52

// Network related constants
const char* wifi_ssid     = "MySSID";           // change this
const char* wifi_password = "MyWifiPassword";   // change this
const char* scope_ip      = "192.168.1.2";      // change this
const uint16_t scope_port = 5555;               // don't change this without good reason

// Messages to send to the Rigol MSO5000 scope
const char* scope_stop_msg   = ":STOP\n";
const char* scope_run_msg    = ":RUN\n";
const char* scope_single_msg = ":SINGle\n";

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Declaration for the network socket that will connect to the scope
WiFiClient scope;

// Declaration for the Elechouse VR3
VR myVR(2,3);    // the (2,3) are ignored for ESP32 - kept only for easier backwards compatibility

// The following arrays used for the Elechouse VR3 module
uint8_t vr3_check_recog_cmd[]  = {0x01};
uint8_t vr3_clear_recog_cmd[]  = {0x31};
uint8_t vr3_load_records_cmd[] = {0x30, 0x00, 0x01, 0x02};
uint8_t vr3_load_response[]    = {0xAA, 0x09, 0x30, 0x03, 0x00, 0x00, 0x01, 0x00, 0x02, 0x00, 0x0A};
uint8_t vr3_stop_msg[]   = {0xAA, 0x0B, 0x0D, 0x00, 0xFF, 0x00, 0x00, 0x04, 0x53, 0x54, 0x4F, 0x50, 0x0A};
uint8_t vr3_single_msg[] = {0xAA, 0x0D, 0x0D, 0x00, 0xFF, 0x01, 0x01, 0x06, 0x53, 0x49, 0x4E, 0x47, 0x4C, 0x45, 0x0A};
uint8_t vr3_run_msg[]    = {0xAA, 0x0A, 0x0D, 0x00, 0xFF, 0x02, 0x02, 0x03, 0x72, 0x75, 0x6E, 0x0A};
uint8_t vr3_buf[50];


void setup(void) 
{
  // USB debug serial port setup
  Serial.begin(115200);
  Serial.println(F("Scope Control on the serial monitor!"));

  start_oled();
  start_wifi();
  scope_connect();
  start_vr3_running();
}


void loop(void) 
{
  int ret_len;

  // Check for received voice messages from the VR3. If there's a voice message, check what it is, 
  // display word on OLED and send command to scope as appropriate. 
  ret_len = myVR.receive_pkt(vr3_buf, 50);
  if (ret_len > 0) {
    display.setCursor(0, OLED_YPOS_WORD);
    
    if (byte_array_cmp(vr3_buf, vr3_stop_msg, ret_len, sizeof(vr3_stop_msg))) {
      display.println("Heard: STOP  ");
      display.display(); 
      scope.print(scope_stop_msg);
    }
    else if (byte_array_cmp(vr3_buf, vr3_single_msg, ret_len, sizeof(vr3_single_msg))) {
      display.println("Heard: SINGLE");
      display.display(); 
      scope.print(scope_single_msg);
    }
    else if (byte_array_cmp(vr3_buf, vr3_run_msg, ret_len, sizeof(vr3_run_msg))) {
      display.println("Heard: RUN   ");
      display.display(); 
      scope.print(scope_run_msg);
    }
    else {
      display.println("Unknown word");
      display.display(); 
    }
  }
}


//
// Starts OLED, by initializing it and putting a message on the screen.
// Hangs forever if it's unable to start, which should never happen, but at least cues the user
// that perhaps something is wrong.
void start_oled(void)
{
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3D for 128x64 OLED
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }
  
  delay(2000);            // OLED needs wakeup time, but so does everything else
  display.setRotation(SCREEN_ROTATION);
  display.clearDisplay();

  display.setTextSize(1);
  display.setTextColor(WHITE, BLACK);

  // Display title text
  display.setCursor(0, OLED_YPOS_TITLE);
  display.println(" Scope Voice Control");
  display.display(); 
}


//
// Starts the Wifi by connecting to the access point. Note this will try forever - no timeout,
// because this product is useless unless it connects to WiFi. Displays status messages on the OLED.
void start_wifi(void) {
  WiFi.mode(WIFI_STA);

  display.setCursor(0, OLED_YPOS_WIFI);
  display.println("WiFi Connecting...");
  display.display(); 
  
  WiFi.begin(wifi_ssid, wifi_password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
  }
  Serial.println(WiFi.localIP());

  display.setCursor(0, OLED_YPOS_WIFI);
  display.println("WiFi Connected    ");
  display.display(); 
}


//
// Call this after the WiFi and OLED have been started. Connects to the scope. Tries forever - no timeout,
// because this product is useless unless it connects to the scope. Displays status messages on the OLED.
void scope_connect(void)
{
  display.setCursor(0, OLED_YPOS_SCOPE);
  display.println("Connecting 2 scope...");
  display.display(); 
  
  while (!scope.connect(scope_ip, scope_port)) 
    delay (1000);

  display.setCursor(0, OLED_YPOS_SCOPE);
  display.println("Scope connected      ");
  display.display();   
}


//
// Starts the VR3 module running, by loading the records into the recognizer and reporting the
// result on the OLED.
void start_vr3_running(void)
{
  myVR.begin(9600);     // 9600 baud serial port between ESP32 and VR3

  // Check the recognizer and clear the recognizer. Get the resulting responses but ignore them, we don't
  // care, it's only to get the VR3 started in a known clear state
  myVR.receive_pkt(vr3_buf, 50);    // start with a read in case there's any junk data in the receive fifo
  myVR.send_pkt(vr3_check_recog_cmd, sizeof(vr3_check_recog_cmd));
  myVR.receive_pkt(vr3_buf, 50);
  myVR.send_pkt(vr3_clear_recog_cmd, sizeof(vr3_clear_recog_cmd));
  myVR.receive_pkt(vr3_buf, 50);
  
  // Now tell the VR3 recognizer to load the word recognition records
  // Check the response to ensure the VR3 did load the recognizer correctly.
  myVR.send_pkt(vr3_load_records_cmd, sizeof(vr3_load_records_cmd));
  display.setCursor(0, OLED_YPOS_VR3);
  
  if (check_for_vr3_load_response(50))
    display.println("Listening...");
  else
    display.println("VR3 Not Started");
    
  display.display();   
}


//
// Returns true if the VR3 module returns the expected reponse to the "load records"
// command within the specified milliseconds timeout.
boolean check_for_vr3_load_response(int timeout)
{
  int ret_len;
  
  ret_len = myVR.receive_pkt(vr3_buf, timeout);
  if (ret_len <= 0)
    return false;

  if (byte_array_cmp(vr3_buf, vr3_load_response, ret_len, sizeof(vr3_load_response)))
    return true;
   
  return false;
}


//
// Compares two arrays, returns true if they're identical
// Copied from https://forum.arduino.cc/t/comparing-two-arrays/5211
boolean byte_array_cmp(uint8_t *a, uint8_t *b, int len_a, int len_b)
{
      int n;

      // if their lengths are different, return false
      if (len_a != len_b) return false;

      // test each element to be the same. if not, return false
      for (n=0;n<len_a;n++) if (a[n]!=b[n]) return false;

      //ok, if we have not returned yet, they are equal :)
      return true;
}
