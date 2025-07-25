#include <ESP32Servo.h>

const int servoPin = 13; // GPIO13 для сигнала сервопривода
Servo continuousServo;

void setup() {
  Serial.begin(115200);
  
  // Инициализация сервопривода
  continuousServo.attach(servoPin);
  
  // Остановка сервопривода (90 для большинства моделей)
  continuousServo.write(90);
  Serial.println("Сервопривод остановлен (90)");
}

void loop() {
  // Пример управления
  
  // Вращение по часовой (меньше 90)
  Serial.println("Вращение по часовой (45)");
  continuousServo.write(60);
  delay(2000);
  
  // Остановка
  Serial.println("Остановка (90)");
  continuousServo.write(90);
  delay(1000);
  
  // Вращение против часовой (больше 90)
  Serial.println("Вращение против часовой (135)");
  continuousServo.write(120);
  delay(2000);
  
  // Остановка
  Serial.println("Остановка (90)");
  continuousServo.write(90);
  delay(1000);
}