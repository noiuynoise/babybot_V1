#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include "blinker.h"

const uint8_t neopixel_pin = 2;
const uint8_t pixel_count = 4;

bool l_blinker, r_blinker, headlight, reverse;

uint32_t neoColor(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

const uint8_t kColorBrightness = 175; // max color
const uint32_t kIdleColor = neoColor(0,0,kColorBrightness/8);
const uint32_t kBrakeColor = neoColor(kColorBrightness/2,0,0);
const uint32_t kHeadlightColor = neoColor(255,255,255);
const uint32_t kBlinkerColor = neoColor(kColorBrightness/2, kColorBrightness/2, 0);

Adafruit_NeoPixel strip(pixel_count, neopixel_pin, NEO_GRB + NEO_KHZ800);

SemaphoreHandle_t blinkerStatusMutex;

void blinkerThread(void * pvParameters);

void setLed(Lights light, uint32_t color){
    strip.setPixelColor((int) light, color);
    //Serial.printf("Setting LED:%d to r:%d, g:%d, b:%d\n", light, r, g, b);
}

void initBlinker(void){
    strip.begin();
    strip.show();
    l_blinker = false;
    r_blinker = false;
    blinkerStatusMutex = xSemaphoreCreateMutex();
    xTaskCreate(
        blinkerThread,
        "blinker",
        10*1024,
        NULL,
        1,
        NULL
    );
}

void setLights(bool l_blinker_in, bool r_blinker_in, bool headlight_in, bool reverse_in){
    if (!xSemaphoreTake(blinkerStatusMutex, 0)){
        return;
    }
    l_blinker = l_blinker_in;
    r_blinker = r_blinker_in;
    headlight = headlight_in;
    reverse = reverse_in;
    xSemaphoreGive(blinkerStatusMutex);
}

void blinkerThread(void * pvParameters){
    uint8_t l_blinker_state = 0;
    uint8_t r_blinker_state = 0;
    while(1){
        // get blinker statuses
        bool l_blinker_status, r_blinker_status,headlight_status, reverse_status;
        xSemaphoreTake(blinkerStatusMutex, portMAX_DELAY);
        l_blinker_status = l_blinker;
        r_blinker_status = r_blinker;
        headlight_status = headlight;
        reverse_status = reverse;
        xSemaphoreGive(blinkerStatusMutex);

        const uint8_t blinker_period = 4;
        
        // four possible light states:
        // idle, headlight, turn, brake
        // priority is:
        // brake
        // turn
        // headlight
        // idle

        for(int i=0; i<4; i++){
            setLed((Lights)i, kIdleColor);
        }

        if(headlight_status){
            setLed(front_left, kHeadlightColor);
            setLed(front_right, kHeadlightColor);
        }

        if(l_blinker_status){
            l_blinker_state = (l_blinker_state + 1) % blinker_period;
            if(l_blinker_state < blinker_period / 2){
                setLed(front_left, kBlinkerColor);
                setLed(back_left, kBlinkerColor);
            }
        }else{
            l_blinker_state = 0;
        }

        if(r_blinker_status){
            r_blinker_state = (r_blinker_state + 1) % blinker_period;
            if(r_blinker_state < blinker_period / 2){
                setLed(front_right, kBlinkerColor);
                setLed(back_right, kBlinkerColor);
            }
        }else{
            r_blinker_state = 0;
        }

        if(reverse_status){
            setLed(back_left, kBrakeColor);
            setLed(back_right, kBrakeColor);
        }

        //Serial.printf("Blinkers - l_status:%d, l_state:%d, r_status:%d, r_state:%d\n", l_blinker_status, l_blinker_state, r_blinker_status, r_blinker_state);

        strip.show();
        vTaskDelay(pdMS_TO_TICKS(300));
    }
}