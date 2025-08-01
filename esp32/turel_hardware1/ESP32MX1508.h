#ifndef ESP32MX1508_H
#define ESP32MX1508_H

#include <Arduino.h>

class MX1508 {
public:
    MX1508(uint8_t pinIN1, uint8_t pinIN2);
    void motorStop();
    void motorBrake();
    void motorGo(uint32_t pwmSpeed);
    void motorRev(uint32_t pwmSpeed);

private:
    uint8_t _pinIN1, _pinIN2;
    uint32_t _maxpwm = 255;
};

#endif