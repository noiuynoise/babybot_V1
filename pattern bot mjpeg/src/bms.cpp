#include <Arduino.h>
#include "bms.h"

const uint8_t kBmsPin = 16;

void bmsThread(void * pvParameters);

TaskHandle_t BmsTask;

void initBms(void){
    pinMode(kBmsPin, INPUT); //input = no pull
    xTaskCreate(
        bmsThread,
        "BMS Task",
        1024,
        NULL,
        1,
        &BmsTask
    );
}

void bmsThread(void * pvParameters){
    while(1){
        pinMode(kBmsPin, OUTPUT);
        digitalWrite(kBmsPin, LOW);
        vTaskDelay(pdMS_TO_TICKS(500));
        pinMode(kBmsPin, INPUT);
        vTaskDelay(pdMS_TO_TICKS(100000));
    }
}

void shutdown(void){
    if(BmsTask){
        vTaskDelete(BmsTask);
    }
}