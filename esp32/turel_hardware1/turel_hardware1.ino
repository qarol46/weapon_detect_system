#include <WebServer.h>
#include <WiFi.h>
#include <esp32cam.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <ESP32MX1508.h>

#define PINA 14
#define PINB 15

const char* WIFI_SSID = "Engi-Teams_2.4G";
const char* WIFI_PASS = "Neutrhino1";

WebServer server(80);

// Структура для обмена данными между потоками
struct {
    float rel_x;
    float rel_y;
    float confidence;
    bool button_state;
    unsigned long lastUpdate;
    SemaphoreHandle_t mutex;
} sharedData;

// Сервоприводы
Servo panServo;
Servo tiltServo;

// Мотор
MX1508 motorA(PINA, PINB);


// Параметры сервоприводов
const int PAN_SERVO_PIN = 12;
const int TILT_SERVO_PIN = 13;
const int PAN_STOP = 90;
const int TILT_STOP = 90;
const int PAN_CW = 84;
const int PAN_CCW = 120;
const int TILT_CW = 50;
const int TILT_CCW = 135;
const float CENTER_THRESHOLD = 0.25;
const unsigned long OBJECT_TIMEOUT = 275;

// Параметры плавности
const float SERVO_SMOOTHING_FACTOR = 0.1;  // Коэффициент плавности (0.1-0.3)
const int SERVO_UPDATE_INTERVAL = 20;      // Интервал обновления (мс)
const int MOTOR_RUN_TIME = 800;            // Время работы мотора (мс)
const int MOTOR_COOLDOWN = 5000;           // Время остывания мотора (мс)
const int SERVO_MOVE_TIME = 150;           // Максимальное время движения серв за один цикл (мс)

// Текущие положения
float currentPanPos = PAN_STOP;
float currentTiltPos = TILT_STOP;
bool motorTriggered = false;
unsigned long lastServoMoveTime = 0;
bool servoWasMoving = false;

void motorControlTask(void *pvParameters) {
    while(1) {
        if(xSemaphoreTake(sharedData.mutex, portMAX_DELAY) == pdTRUE) {
            if(sharedData.button_state && !motorTriggered) {
                motorA.motorGo(255);            // Pass the speed to the motor: 0-255 for 8 bit resolution
                delay(500);
                servoWrite(180);
                motorA.motorGo(255);
                delay(1100);
                motorA.motorStop();             // Soft Stop    -no argument
                delay(5000);
                servoWrite(0);
                motorTriggered = true;
            } 
            else if(!sharedData.button_state) {
                motorTriggered = false;
            }
            xSemaphoreGive(sharedData.mutex);
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

void smoothServoMove(Servo &servo, float &currentPos, int targetPos) {
    // Плавное изменение положения с ограничением времени
    unsigned long startTime = millis();
    while(millis() - startTime < SERVO_MOVE_TIME) {
        float delta = targetPos - currentPos;
        currentPos += delta * SERVO_SMOOTHING_FACTOR;
        servo.write(round(currentPos));
        delay(SERVO_UPDATE_INTERVAL);
    }
}

void servoTask(void *pvParameters) {
    TickType_t xLastWakeTime = xTaskGetTickCount();
    
    while(1) {
        if(xSemaphoreTake(sharedData.mutex, portMAX_DELAY) == pdTRUE) {
            unsigned long currentTime = millis();
            bool objectLost = (currentTime - sharedData.lastUpdate > OBJECT_TIMEOUT) || 
                            (sharedData.confidence < 0.1);
            
            if(objectLost || servoWasMoving) {
                // Если объект потерян или сервы уже двигались в предыдущем цикле - останавливаем
                currentPanPos = PAN_STOP;
                currentTiltPos = TILT_STOP;
                panServo.write(PAN_STOP);
                tiltServo.write(TILT_STOP);
                servoWasMoving = false;
            } 
            else {
                float xError = sharedData.rel_x;
                float yError = sharedData.rel_y;
                
                int targetPan = PAN_STOP;
                int targetTilt = TILT_STOP;
                
                if(fabs(xError) > CENTER_THRESHOLD) {
                    targetPan = (xError > 0) ? PAN_CW : PAN_CCW;
                }
                
                if(fabs(yError) > CENTER_THRESHOLD) {
                    targetTilt = (yError > 0) ? TILT_CCW : TILT_CW;
                }
                
                if(targetPan != PAN_STOP || targetTilt != TILT_STOP) {
                    smoothServoMove(panServo, currentPanPos, targetPan);
                    smoothServoMove(tiltServo, currentTiltPos, targetTilt);
                    servoWasMoving = true;
                }
            }
            xSemaphoreGive(sharedData.mutex);
        }
        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(SERVO_UPDATE_INTERVAL));
    }
}

void servoWrite(int angle) {
  int pulseWidth = map(angle, 0, 180, 1000, 2000);  // 1-2 мс
  digitalWrite(2, HIGH);
  delayMicroseconds(pulseWidth);
  digitalWrite(2, LOW);
  delayMicroseconds(20000 - pulseWidth);  // Общий период 20 мс
}

void serveJpg() {
    auto frame = esp32cam::capture();
    if(frame == nullptr) {
        Serial.println("CAPTURE FAIL");
        server.send(503, "", "");
        return;
    }
    
    server.setContentLength(frame->size());
    server.send(200, "image/jpeg");
    WiFiClient client = server.client();
    frame->writeTo(client);
}

void handleCoords() {
    if(server.method() != HTTP_POST) {
        server.send(405, "Method Not Allowed");
        return;
    }

    DynamicJsonDocument doc(256);
    if(deserializeJson(doc, server.arg("plain"))) {
        server.send(400, "Bad Request");
        return;
    }

    if(xSemaphoreTake(sharedData.mutex, portMAX_DELAY) == pdTRUE) {
        sharedData.rel_x = doc["rel_x"];
        sharedData.rel_y = doc["rel_y"];
        sharedData.confidence = doc["confidence"];
        sharedData.button_state = doc["button_state"];
        sharedData.lastUpdate = millis();
        xSemaphoreGive(sharedData.mutex);
    }

    char echo_response[150];
    snprintf(echo_response, sizeof(echo_response), 
            "{\"echo\": {\"rel_x\": %.2f, \"rel_y\": %.2f, \"confidence\": %.2f, \"button_state\": %d}}", 
            sharedData.rel_x, sharedData.rel_y, sharedData.confidence, sharedData.button_state);
    
    server.send(200, "application/json", echo_response);
}

void setup() {
    Serial.begin(115200);

    pinMode(2, OUTPUT);
    analogWriteResolution(2, 8);
    servoWrite(0);
    // Инициализация мьютекса
    sharedData.mutex = xSemaphoreCreateMutex();
    
    // Настройка камеры
    {
        using namespace esp32cam;
        Config cfg;
        cfg.setPins(pins::AiThinker);
        cfg.setResolution(esp32cam::Resolution::find(640, 480));
        cfg.setBufferCount(2);
        cfg.setJpeg(80);

        bool ok = Camera.begin(cfg);
        Serial.println(ok ? "CAMERA OK" : "CAMERA FAIL");
    }

    // Инициализация сервоприводов
    panServo.attach(PAN_SERVO_PIN);
    tiltServo.attach(TILT_SERVO_PIN);
    currentPanPos = PAN_STOP;
    currentTiltPos = TILT_STOP;
    panServo.write(PAN_STOP);
    tiltServo.write(TILT_STOP);

    // Подключение к WiFi
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while(WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.printf("\nWiFi Connected\nIP: %s\n", WiFi.localIP().toString().c_str());
    
    // Роуты
    server.on("/cam-hi.jpg", serveJpg);
    server.on("/coords", HTTP_POST, handleCoords);
    server.begin();

    // Создаем потоки
    xTaskCreatePinnedToCore(servoTask, "ServoTask", 4096, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(motorControlTask, "MotorControlTask", 4096, NULL, 1, NULL, 0);

    Serial.println("Система запущена. Потоки активны.");
}

void loop() {
    server.handleClient();
    delay(1);
}