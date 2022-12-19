#include <Arduino.h>
#include "EEPROM.h"

struct configData{
  char ap_ssid[40];
  char ap_pwd[40];
  char ext_ap_ssid[40];
  char ext_ap_pwd[40];
  char mdns_name[40];
};

const configData defaultConfig = {
  {.ap_ssid = "babybot2"},
  {.ap_pwd = "myRobotPass1234%"},
  {.ext_ap_ssid = "SSID"},
  {.ext_ap_pwd = "PASSWORD"},
  {.mdns_name = "babybot"}
};

void setup() {
  // put your setup code here, to run once:
  EEPROM.begin(sizeof(configData));
  EEPROM.put(0, defaultConfig);
  EEPROM.commit();
  Serial.begin(115200);
  Serial.println("complete!");
}

void loop() {
  // put your main code here, to run repeatedly:
}
