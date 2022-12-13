/*

  This is a simple MJPEG streaming webserver implemented for AI-Thinker ESP32-CAM
  and ESP-EYE modules.
  This is tested to work with VLC and Blynk video widget and can support up to 10
  simultaneously connected streaming clients.
  Simultaneous streaming is implemented with dedicated FreeRTOS tasks.

  Inspired by and based on this Instructable: $9 RTSP Video Streamer Using the ESP32-CAM Board
  (https://www.instructables.com/id/9-RTSP-Video-Streamer-Using-the-ESP32-CAM-Board/)

  Board: AI-Thinker ESP32-CAM or ESP-EYE
  Compile as:
   ESP32 Dev Module
   CPU Freq: 240
   Flash Freq: 80
   Flash mode: QIO
   Flash Size: 4Mb
   Partrition: Minimal SPIFFS
   PSRAM: Enabled
*/

// ESP32 has two cores: APPlication core and PROcess core (the one that runs ESP32 SDK stack)
#define APP_CPU 1
#define PRO_CPU 0

//Libraries
#include <Arduino.h>
#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>
#include <WiFiClient.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include "EEPROM.h"
#include <esp_bt.h>
#include <esp_wifi.h>
#include <esp_sleep.h>
#include <driver/rtc_io.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

//Project Code
//#include "canvas_htm.h"
#include "drivetrain.h"
#include "blinker.h"
#include "bms.h"

//Webpage Resources
#include "index_html_gz.h"
#include "pattern_logo.h"

#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

#define MAX_CLIENTS   2

//OV2640 cam;

WebServer server(80);

// ===== rtos task handles =========================
// Streaming is implemented with 3 tasks:
TaskHandle_t tMjpeg;   // handles client connections to the webserver
TaskHandle_t tCam;     // handles getting picture frames from the camera and storing them locally

uint8_t       noActiveClients;       // number of active clients

// frameSync semaphore is used to prevent streaming buffer as it is replaced with the next frame
SemaphoreHandle_t frameSync = NULL;

// We will try to achieve 24 FPS frame rate
const int FPS = 24;

// We will handle web client requests every 100 ms (10 Hz)
const int WSINTERVAL = 100;

QueueHandle_t cmdJsonQueue;
DynamicJsonDocument doc(512);
DynamicJsonDocument configJsonDoc(1024);

struct configData{
  char ap_ssid[40];
  char ap_pwd[40];
  char ext_ap_ssid[40];
  char ext_ap_pwd[40];
  char mdns_name[40];
};

const configData defaultConfig = {
  {.ap_ssid = "unconfigured_robot_1"},
  {.ap_pwd = "myRobotPass1234%"},
  {.ext_ap_ssid = ""},
  {.ext_ap_pwd = ""},
  {.mdns_name = "robot"}
};

configData currentConfig;

void camCB(void *pvParameters);
char* allocateMemory(char* aPtr, size_t aSize);
void handleJPGSstream(void);
void handleNotFound(void);
void streamCB(void * pvParameters);
void handleWebpage(void);
void runCmd(void);
void handlePattern(void);
void configEsp(void);

// ======== Server Connection Handler Task ==========================
void mjpegCB(void* pvParameters) {
  TickType_t xLastWakeTime;
  const TickType_t xFrequency = pdMS_TO_TICKS(WSINTERVAL);

  // Creating frame synchronization semaphore and initializing it
  frameSync = xSemaphoreCreateBinary();
  xSemaphoreGive( frameSync );

  //=== setup section  ==================

  //  Creating RTOS task for grabbing frames from the camera
  xTaskCreatePinnedToCore(
    camCB,        // callback
    "cam",        // name
    4 * 1024,       // stacj size
    NULL,         // parameters
    2,            // priority
    &tCam,        // RTOS task handle
    PRO_CPU);     // core

  //  Registering webserver handling routines
  server.on("/mjpeg/1", HTTP_GET, handleJPGSstream);
  server.on("/", HTTP_GET, handleWebpage);
  server.on("/assets/img/pattern-logo-full.png", HTTP_GET, handlePattern);
  server.on("/cmd", HTTP_POST, runCmd);
  server.on("/config", HTTP_POST, configEsp);
  server.onNotFound(handleNotFound);

  //  Starting webserver
  server.begin();

  noActiveClients = 0;

  Serial.printf("\nmjpegCB: free heap (start)  : %d\n", ESP.getFreeHeap());
  //=== loop() section  ===================
  xLastWakeTime = xTaskGetTickCount();
  for (;;) {
    server.handleClient();

    //  After every server client handling request, we let other tasks run and then pause
    taskYIELD();
    vTaskDelayUntil(&xLastWakeTime, xFrequency);
  }
}


// Current frame information
volatile uint32_t frameNumber;
volatile size_t   camSize;    // size of the current frame, byte
volatile char*    camBuf;      // pointer to the current frame


// ==== RTOS task to grab frames from the camera =========================
void camCB(void* pvParameters) {

  TickType_t xLastWakeTime;

  //  A running interval associated with currently desired frame rate
  const TickType_t xFrequency = pdMS_TO_TICKS(1000 / FPS);

  //  Pointers to the 2 frames, their respective sizes and index of the current frame
  char* fbs[2] = { NULL, NULL };
  size_t fSize[2] = { 0, 0 };
  int ifb = 0;
  frameNumber = 0;

  //=== loop() section  ===================
  xLastWakeTime = xTaskGetTickCount();

  for (;;) {

    //  Grab a frame from the camera and query its size
    camera_fb_t* fb = NULL;

    fb = esp_camera_fb_get();
    size_t s = fb->len;

    //  If frame size is more that we have previously allocated - request  125% of the current frame space
    if (s > fSize[ifb]) {
      fSize[ifb] = s + s;
      fbs[ifb] = allocateMemory(fbs[ifb], fSize[ifb]);
    }

    //  Copy current frame into local buffer
    char* b = (char *)fb->buf;
    memcpy(fbs[ifb], b, s);
    esp_camera_fb_return(fb);

    //  Let other tasks run and wait until the end of the current frame rate interval (if any time left)
    taskYIELD();
    vTaskDelayUntil(&xLastWakeTime, xFrequency);

    //  Only switch frames around if no frame is currently being streamed to a client
    //  Wait on a semaphore until client operation completes
    //    xSemaphoreTake( frameSync, portMAX_DELAY );

    //  Do not allow frame copying while switching the current frame
    xSemaphoreTake( frameSync, xFrequency );
    camBuf = fbs[ifb];
    camSize = s;
    ifb++;
    ifb &= 1;  // this should produce 1, 0, 1, 0, 1 ... sequence
    frameNumber++;
    //  Let anyone waiting for a frame know that the frame is ready
    xSemaphoreGive( frameSync );

    //  Immediately let other (streaming) tasks run
    taskYIELD();

    //  If streaming task has suspended itself (no active clients to stream to)
    //  there is no need to grab frames from the camera. We can save some juice
    //  by suspedning the tasks
    if ( noActiveClients == 0 ) {
      Serial.printf("mjpegCB: free heap           : %d\n", ESP.getFreeHeap());
      Serial.printf("mjpegCB: min free heap)      : %d\n", ESP.getMinFreeHeap());
      Serial.printf("mjpegCB: max alloc free heap : %d\n", ESP.getMaxAllocHeap());
      Serial.printf("mjpegCB: tCam stack wtrmark  : %d\n", uxTaskGetStackHighWaterMark(tCam));
      Serial.flush();
      vTaskSuspend(NULL);  // passing NULL means "suspend yourself"
    }
  }
}


// ==== Memory allocator that takes advantage of PSRAM if present =======================
char* allocateMemory(char* aPtr, size_t aSize) {

  //  Since current buffer is too smal, free it
  if (aPtr != NULL) free(aPtr);

  char* ptr = NULL;
  ptr = (char*) ps_malloc(aSize);

  // If the memory pointer is NULL, we were not able to allocate any memory, and that is a terminal condition.
  if (ptr == NULL) {
    Serial.println("Out of memory!");
    delay(5000);
    ESP.restart();
  }
  return ptr;
}


// ==== STREAMING ======================================================
const char HEADER[] = "HTTP/1.1 200 OK\r\n" \
                      "Access-Control-Allow-Origin: *\r\n" \
                      "Content-Type: multipart/x-mixed-replace; boundary=123456789000000000000987654321\r\n";
const char BOUNDARY[] = "\r\n--123456789000000000000987654321\r\n";
const char CTNTTYPE[] = "Content-Type: image/jpeg\r\nContent-Length: ";
const int hdrLen = strlen(HEADER);
const int bdrLen = strlen(BOUNDARY);
const int cntLen = strlen(CTNTTYPE);


struct streamInfo {
  uint32_t        frame;
  WiFiClient      client;
  TaskHandle_t    task;
  char*           buffer;
  size_t          len;
};

// ==== Handle connection request from clients ===============================
void handleJPGSstream(void)
{
  if ( noActiveClients >= MAX_CLIENTS ) return;
  Serial.printf("handleJPGSstream start: free heap  : %d\n", ESP.getFreeHeap());

  streamInfo* info = new streamInfo;

  info->frame = frameNumber - 1;
  info->client = server.client();
  info->buffer = NULL;
  info->len = 0;

  //  Creating task to push the stream to all connected clients
  int rc = xTaskCreatePinnedToCore(
             streamCB,
             "strmCB",
             3 * 1024,
             (void*) info,
             2,
             &info->task,
             APP_CPU);
  if ( rc != pdPASS ) {
    Serial.printf("handleJPGSstream: error creating RTOS task. rc = %d\n", rc);
    Serial.printf("handleJPGSstream: free heap  : %d\n", ESP.getFreeHeap());
    //    Serial.printf("stk high wm: %d\n", uxTaskGetStackHighWaterMark(tSend));
    delete info;
  }

  noActiveClients++;

  // Wake up streaming tasks, if they were previously suspended:
  if ( eTaskGetState( tCam ) == eSuspended ) vTaskResume( tCam );
}


// ==== Actually stream content to all connected clients ========================
void streamCB(void * pvParameters) {
  char buf[16];
  TickType_t xLastWakeTime;
  TickType_t xFrequency;

  streamInfo* info = (streamInfo*) pvParameters;

  if ( info == NULL ) {
    Serial.println("streamCB: a NULL pointer passed");
  }
  //  Immediately send this client a header
  info->client.write(HEADER, hdrLen);
  info->client.write(BOUNDARY, bdrLen);
  taskYIELD();

  xLastWakeTime = xTaskGetTickCount();
  xFrequency = pdMS_TO_TICKS(1000 / FPS);

  for (;;) {
    //  Only bother to send anything if there is someone watching
    if ( info->client.connected() ) {

      if ( info->frame != frameNumber) {
        xSemaphoreTake( frameSync, portMAX_DELAY );
        if ( info->buffer == NULL ) {
          info->buffer = allocateMemory (info->buffer, camSize);
          info->len = camSize;
        }
        else {
          if ( camSize > info->len ) {
            info->buffer = allocateMemory (info->buffer, camSize);
            info->len = camSize;
          }
        }
        memcpy(info->buffer, (const void*) camBuf, info->len);
        xSemaphoreGive( frameSync );
        taskYIELD();

        info->frame = frameNumber;
        info->client.write(CTNTTYPE, cntLen);
        sprintf(buf, "%d\r\n\r\n", info->len);
        info->client.write(buf, strlen(buf));
        info->client.write((char*) info->buffer, (size_t)info->len);
        info->client.write(BOUNDARY, bdrLen);
        info->client.flush();
      }
    }
    else {
      //  client disconnected - clean up.
      noActiveClients--;
      Serial.printf("streamCB: Stream Task stack wtrmark  : %d\n", uxTaskGetStackHighWaterMark(info->task));
      Serial.flush();
      info->client.flush();
      info->client.stop();
      if ( info->buffer ) {
        free( info->buffer );
        info->buffer = NULL;
      }
      delete info;
      info = NULL;
      vTaskDelete(NULL);
    }
    //  Let other tasks run after serving every client
    taskYIELD();
    vTaskDelayUntil(&xLastWakeTime, xFrequency);
  }
}


// ==== Handle invalid URL requests ============================================
void handleNotFound(void)
{
  String message = "Server is running!\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  server.send(200, "text / plain", message);
}

void handleWebpage(void)
{
  server.sendHeader(F("Content-Encoding"), F("gzip"));
  server.send_P(200, "text / html", (const char *)index_html, index_html_len);
}

void handlePattern(void){
  server.sendHeader(F("Content-Encoding"), F("gzip"));
  server.send_P(200, "image / png", (const char *)pattern_logo, pattern_logo_len);
}

void execCmd(void *pvParameters){
  char buffer[120];
  pinMode(4, OUTPUT);
  while(1){
    if(xQueueReceive(cmdJsonQueue, buffer, 0) == pdTRUE){
      if(uxQueueMessagesWaiting(cmdJsonQueue) > 0){
        continue;
      }
      //Serial.print(buffer);
      deserializeJson(doc, buffer);
      double fwd = doc["fwd"];
      double turn = doc["turn"];
      bool l_turn = doc["l_turn"];
      bool r_turn = doc["r_turn"];
      bool headlight = doc["headlight"];
      bool horn = doc["horn"];
      setRobotDriveDirection(fwd, turn);
      //temporary horn is light
      //ledcWrite(4, 100 * horn);
      digitalWrite(4, horn);
      setLights(l_turn, r_turn, headlight, fwd < 0);
      //Serial.printf("recieved data. fwd: %f, turn: %f, l_turn: %d, r_turn: %d, horn: %d, headlight:%d\n", fwd, turn, l_turn, r_turn, horn, headlight);
    }
    updateRobotDrive();
    vTaskDelay(100);
  }
}

void runCmd(void){
  String body = server.arg("plain");
  xQueueSend(cmdJsonQueue, body.c_str(), 0);
}

bool setIfPresent(DynamicJsonDocument &doc, const String &key, char *data_ptr){
  if(doc.containsKey(key)){
    String value = doc[key];
    if(value.length() < 40){
      memcpy(data_ptr, value.c_str(), value.length() + 1); //copy null char too
      return true;
    }
  }
  return false;
}

void configEsp(void){
  String body = server.arg("plain");
  deserializeJson(configJsonDoc, body);
  uint8_t updated = 0;
  updated += setIfPresent(configJsonDoc, "ap_ssid", currentConfig.ap_ssid);
  updated += setIfPresent(configJsonDoc, "ap_pwd", currentConfig.ap_pwd);
  updated += setIfPresent(configJsonDoc, "ext_ap_ssid", currentConfig.ext_ap_ssid);
  updated += setIfPresent(configJsonDoc, "ext_ap_pwd", currentConfig.ext_ap_pwd);
  updated += setIfPresent(configJsonDoc, "mdns_name", currentConfig.mdns_name);
  Serial.printf("updating ESP32. %d parameters\n", updated);
  if(updated){
    EEPROM.put(0, currentConfig);
    EEPROM.commit();
    Serial.println("saved");
  }
  if(configJsonDoc.containsKey("reset")){
    if(configJsonDoc["reset"]){
      ESP.restart();
    }
  }
}

// ==== SETUP method ==================================================================
void setup()
{
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); //disable brownout detector

  // ledcSetup(4, 1000, 8);
  // ledcAttachPin(4, 4);
  // ledcWrite(4, 0);

  // Setup Serial connection:
  Serial.begin(115200);
  delay(1000); // wait for a second to let Serial connect
  Serial.printf("setup: free heap  : %d\n", ESP.getFreeHeap());

  // get configuration from EEPROM
  EEPROM.begin(sizeof(configData));
  EEPROM.get(0, currentConfig);
  bool is_configured = false;
  for(int i=0; i<sizeof(configData); i++){
    if(*((uint8_t*)&currentConfig + i) != 0){
      is_configured = true;
      Serial.println("Device is already configured, using configuration");
      break;
    }
  }
  if(!is_configured){
    currentConfig = defaultConfig;
    Serial.println("EEPROM not configured, saving default config");
    EEPROM.put(0, currentConfig);
    EEPROM.commit();
  }
  // Configure the camera

  static camera_config_t camera_config = {
    .pin_pwdn       = PWDN_GPIO_NUM,
    .pin_reset      = RESET_GPIO_NUM,
    .pin_xclk       = XCLK_GPIO_NUM,
    .pin_sscb_sda   = SIOD_GPIO_NUM,
    .pin_sscb_scl   = SIOC_GPIO_NUM,
    .pin_d7         = Y9_GPIO_NUM,
    .pin_d6         = Y8_GPIO_NUM,
    .pin_d5         = Y7_GPIO_NUM,
    .pin_d4         = Y6_GPIO_NUM,
    .pin_d3         = Y5_GPIO_NUM,
    .pin_d2         = Y4_GPIO_NUM,
    .pin_d1         = Y3_GPIO_NUM,
    .pin_d0         = Y2_GPIO_NUM,
    .pin_vsync      = VSYNC_GPIO_NUM,
    .pin_href       = HREF_GPIO_NUM,
    .pin_pclk       = PCLK_GPIO_NUM,

    .xclk_freq_hz   = 20000000,
    .ledc_timer     = LEDC_TIMER_0,
    .ledc_channel   = LEDC_CHANNEL_0,
    .pixel_format   = PIXFORMAT_JPEG,
    /*
        FRAMESIZE_96X96,    // 96x96
        FRAMESIZE_QQVGA,    // 160x120
        FRAMESIZE_QCIF,     // 176x144
        FRAMESIZE_HQVGA,    // 240x176
        FRAMESIZE_240X240,  // 240x240
        FRAMESIZE_QVGA,     // 320x240
        FRAMESIZE_CIF,      // 400x296
        FRAMESIZE_HVGA,     // 480x320
        FRAMESIZE_VGA,      // 640x480
        FRAMESIZE_SVGA,     // 800x600
        FRAMESIZE_XGA,      // 1024x768
        FRAMESIZE_HD,       // 1280x720
        FRAMESIZE_SXGA,     // 1280x1024
        FRAMESIZE_UXGA,     // 1600x1200
    */
    .frame_size     = FRAMESIZE_XGA,
    .jpeg_quality   = 16,
    .fb_count       = 2
  };

  if (esp_camera_init(&camera_config) != ESP_OK) {
    Serial.println("Error initializing the camera");
    delay(10000);
    ESP.restart();
  }

  cmdJsonQueue = xQueueCreate(3, 120);
  initRobotDrive();
  initBlinker();
  //initBms();

  //setBlinker(true, true);

  sensor_t* s = esp_camera_sensor_get();
  s->set_vflip(s, false);

  //  Configure and connect to WiFi
  IPAddress ip;

  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(currentConfig.ext_ap_ssid, currentConfig.ext_ap_pwd);
  Serial.print("Connecting to WiFi");
  for(int i=0; i<30; i++){
    if (WiFi.status() == WL_CONNECTED){
      break;
    }
    delay(1000);
    Serial.print(F("."));
  }
  ip = WiFi.localIP();
  Serial.println("");
  Serial.println(F("WiFi connected"));
  Serial.println("");
  WiFi.softAP(currentConfig.ap_ssid, currentConfig.ap_pwd);
  IPAddress myIP = WiFi.softAPIP();
  Serial.print("\nAP IP address: ");
  Serial.println(myIP);
  Serial.println("");
  Serial.print("Stream Link: http://");
  Serial.print(currentConfig.mdns_name);
  Serial.println(".local/mjpeg/1");

  // Start mDNS server
  MDNS.begin(currentConfig.mdns_name);
  MDNS.addService("http", "tcp", 80);

  // Start mainstreaming RTOS task
  xTaskCreatePinnedToCore(
    mjpegCB,
    "mjpeg",
    6 * 1024,
    NULL,
    2,
    &tMjpeg,
    APP_CPU);
  
  xTaskCreatePinnedToCore(
    execCmd,
    "cmd exec",
    10 * 1024,
    NULL,
    1,
    NULL,
    APP_CPU);
  
  Serial.printf("setup complete: free heap  : %d\n", ESP.getFreeHeap());
}

void loop() {
  // this seems to be necessary to let IDLE task run and do GC
  vTaskDelete(NULL);
}
