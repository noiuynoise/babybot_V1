#include <stdint.h>
#include <Arduino.h>
#include "drivetrain.h"

#define sgn(x) ((x) < 0 ? -1 : ((x) > 0 ? 1 : 0))

const uint8_t l_mot_fwd_pin = 12;
const uint8_t l_mot_fwd_channel = 0;
const uint8_t l_mot_rev_pin = 13;
const uint8_t l_mot_rev_channel = 1;
const uint8_t r_mot_fwd_pin = 14;
const uint8_t r_mot_fwd_channel = 2;
const uint8_t r_mot_rev_pin = 15;
const uint8_t r_mot_rev_channel = 3;

const int pwm_freq = 20000;
const double max_slow_rate = 3.0; //motor per second

double l_motor_requested = 0;
double l_motor_actual = 0;
double r_motor_requested = 0;
double r_motor_actual = 0;

unsigned long last_update_time;

SemaphoreHandle_t DriveMutex;

void setRobotDriveDirection(double fwd, double turn){
    if (!xSemaphoreTake(DriveMutex, 0)){
        return; //don't wait
    }
    // keep motor values between -1-1
    l_motor_requested = max(min(fwd + turn, 1.0), -1.0);
    r_motor_requested = max(min(fwd - turn, 1.0), -1.0);

    xSemaphoreGive(DriveMutex);
}

void initRobotDrive(void){
    ledcSetup(l_mot_fwd_channel, pwm_freq, 8);
    ledcSetup(l_mot_rev_channel, pwm_freq, 8);
    ledcSetup(r_mot_fwd_channel, pwm_freq, 8);
    ledcSetup(l_mot_rev_channel, pwm_freq, 8);

    ledcAttachPin(l_mot_fwd_pin, l_mot_fwd_channel);
    ledcAttachPin(l_mot_rev_pin, l_mot_rev_channel);
    ledcAttachPin(r_mot_fwd_pin, r_mot_fwd_channel);
    ledcAttachPin(r_mot_rev_pin, r_mot_rev_channel);

    DriveMutex = xSemaphoreCreateMutex();
    last_update_time = millis(); //prevents insane decelration delta on 1st call
}

void updateRobotDrive(void){

    unsigned long time_delta = millis() - last_update_time;
    last_update_time = millis();

    double max_deceleration_delta = max_slow_rate * time_delta / 1000.0f;

    double l_mot_actual_sign = sgn(l_motor_actual);
    if(l_motor_actual * l_mot_actual_sign > l_motor_requested * l_mot_actual_sign){
        l_motor_actual = l_mot_actual_sign * max(l_motor_requested * l_mot_actual_sign, (l_motor_actual * l_mot_actual_sign) - max_deceleration_delta);
    }else{
        l_motor_actual = l_motor_requested;
    }

    double r_mot_actual_sign = sgn(r_motor_actual);
    if(r_motor_actual * r_mot_actual_sign > r_motor_requested * r_mot_actual_sign){
        r_motor_actual = r_mot_actual_sign * max(r_motor_requested * r_mot_actual_sign, (r_motor_actual * r_mot_actual_sign) - max_deceleration_delta);
    }else{
        r_motor_actual = r_motor_requested;
    }

    uint8_t l_mot_duty = (uint8_t)(abs(l_motor_actual) * 255);
    uint8_t r_mot_duty = (uint8_t)(abs(r_motor_actual) * 255);

    if(l_motor_actual > 0.0){
        ledcWrite(l_mot_fwd_channel, l_mot_duty);
        ledcWrite(l_mot_rev_channel, 0);
    }else{
        ledcWrite(l_mot_rev_channel, l_mot_duty);
        ledcWrite(l_mot_fwd_channel, 0);
    }

    if(r_motor_actual > 0.0){
        ledcWrite(r_mot_fwd_channel, r_mot_duty);
        ledcWrite(r_mot_rev_channel, 0);
    }else{
        ledcWrite(r_mot_rev_channel, r_mot_duty);
        ledcWrite(r_mot_fwd_channel, 0);
    }
}