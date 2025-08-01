#include "ESP32MX1508.h"

MX1508::MX1508(uint8_t pinIN1, uint8_t pinIN2) {
    _pinIN1 = pinIN1;
    _pinIN2 = pinIN2;

    pinMode(_pinIN1, OUTPUT);
    pinMode(_pinIN2, OUTPUT);

    // ������������� ���������� ��� ��� ������� ���� (ESP32 Core 3.2.1+)
    analogWriteResolution(_pinIN1, 8);  // 8 ��� (0-255)
    analogWriteResolution(_pinIN2, 8);
}

void MX1508::motorStop() {
    analogWrite(_pinIN1, 0);
    analogWrite(_pinIN2, 0);
}

void MX1508::motorBrake() {
    digitalWrite(_pinIN1, HIGH);
    digitalWrite(_pinIN2, HIGH);
}

void MX1508::motorGo(uint32_t pwmSpeed) {
    analogWrite(_pinIN1, pwmSpeed);
    analogWrite(_pinIN2, 0);
}

void MX1508::motorRev(uint32_t pwmSpeed) {
    analogWrite(_pinIN1, 0);
    analogWrite(_pinIN2, pwmSpeed);
}