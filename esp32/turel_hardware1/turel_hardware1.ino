#include <WebServer.h>
#include <WiFi.h>
#include <esp32cam.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <ESP32MX1508.h>

#define PINA 9
#define PINB 10

const char* WIFI_SSID = "Engi-Teams_2.4G";
const char* WIFI_PASS = "Neutrhino1";

WebServer server(80);

// Структура для безопасного обмена данными между потоками
struct {
    float rel_x;
    float rel_y;
    float confidence;
    bool button_state;  // Добавлено состояние кнопки
    unsigned long lastUpdate;
    SemaphoreHandle_t mutex;
} sharedData;

// Сервоприводы
Servo panServo;
Servo tiltServo;
const int LOCK_PIN = 14;  // Пин для управления блокировкой

// Мотор
MX1508 motorA(PINA, PINB);

// Параметры сервоприводов
const int PAN_SERVO_PIN = 12;
const int TILT_SERVO_PIN = 13;
const int PAN_STOP = 90;
const int TILT_STOP = 90;
const int PAN_CW = 84;
const int PAN_CCW = 100;
const int TILT_CW = 75;
const int TILT_CCW = 105;
const float CENTER_THRESHOLD = 0.2;
const unsigned long OBJECT_TIMEOUT = 275;

// Флаг для однократного срабатывания мотора
bool motorTriggered = false;

void motorControlTask(void *pvParameters) {
    while(1) {
        if(xSemaphoreTake(sharedData.mutex, portMAX_DELAY) == pdTRUE) {
            if(sharedData.button_state == 1 && !motorTriggered) {
                // Запускаем мотор только если button_state == 0 и мотор еще не был запущен
                motorA.motorGo(255);  // Вращаем мотор на полную скорость
                delay(800);           // Ждем 800 мс
                motorA.motorStop();   // Останавливаем мотор
                delay(5000);
                motorTriggered = true; // Устанавливаем флаг, что мотор был запущен
            } else if(sharedData.button_state == 0) {
                // Если button_state снова стал 1, сбрасываем флаг
                motorTriggered = false;
            }
            xSemaphoreGive(sharedData.mutex);
        }
        vTaskDelay(100 / portTICK_PERIOD_MS); // Проверяем состояние каждые 100 мс
    }
}

void servoTask(void *pvParameters) {
    while(1) {
        if(xSemaphoreTake(sharedData.mutex, portMAX_DELAY) == pdTRUE) {
            unsigned long currentTime = millis();
            bool objectLost = (currentTime - sharedData.lastUpdate > OBJECT_TIMEOUT) || 
                             (sharedData.confidence < 0.1);
            
            // Управление блокировкой
            digitalWrite(LOCK_PIN, sharedData.button_state ? HIGH : LOW);
            
            if(objectLost) {
                panServo.write(PAN_STOP);
                tiltServo.write(TILT_STOP);
            } else {
                float xError = sharedData.rel_x;
                float yError = sharedData.rel_y;
                
                if(fabs(xError) > CENTER_THRESHOLD) {
                    panServo.write((xError > 0) ? PAN_CW : PAN_CCW);
                } else {
                    panServo.write(PAN_STOP);
                }
                
                if(fabs(yError) > CENTER_THRESHOLD) {
                    tiltServo.write((yError > 0) ? TILT_CCW : TILT_CW);
                } else {
                    tiltServo.write(TILT_STOP);
                }
            }
            xSemaphoreGive(sharedData.mutex);
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
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

    // Инициализация сервоприводов и пина блокировки
    panServo.attach(PAN_SERVO_PIN);
    tiltServo.attach(TILT_SERVO_PIN);
    pinMode(LOCK_PIN, OUTPUT);
    digitalWrite(LOCK_PIN, LOW);
    panServo.write(PAN_STOP);
    tiltServo.write(TILT_STOP);
    Serial.println("Сервоприводы и блокировка инициализированы");

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
    xTaskCreatePinnedToCore(
        servoTask,
        "ServoTask",
        4096,
        NULL,
        1,
        NULL,
        0
    );

    xTaskCreatePinnedToCore(
        motorControlTask,
        "MotorControlTask",
        4096,
        NULL,
        1,
        NULL,
        0
    );

    Serial.println("Система запущена. Потоки сервоприводов и мотора активны.");
}

void loop() {
    server.handleClient();
    delay(1);
}