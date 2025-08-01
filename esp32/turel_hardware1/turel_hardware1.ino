#include <WebServer.h>
#include <WiFi.h>
#include <esp32cam.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

const char* WIFI_SSID = "Engi-Teams_2.4G";
const char* WIFI_PASS = "Neutrhino1";

WebServer server(80);

// Структура для безопасного обмена данными между потоками
struct {
    float rel_x;
    float rel_y;
    float confidence;
    unsigned long lastUpdate;
    SemaphoreHandle_t mutex;
} sharedData;

// Сервоприводы
Servo panServo;
Servo tiltServo;

// Параметры сервоприводов
const int PAN_SERVO_PIN = 12;
const int TILT_SERVO_PIN = 13;
const int PAN_STOP = 90;
const int TILT_STOP = 90;
const int PAN_CW = 84;
const int PAN_CCW = 100;
const int TILT_CW = 80;
const int TILT_CCW = 100;
const float CENTER_THRESHOLD = 0.2;
const unsigned long OBJECT_TIMEOUT = 275;

// Функция для потока сервоприводов
void servoTask(void *pvParameters) {
    while(1) {
        // Блокируем мьютекс для чтения общих данных
        if(xSemaphoreTake(sharedData.mutex, portMAX_DELAY) == pdTRUE) {
            unsigned long currentTime = millis();
            bool objectLost = (currentTime - sharedData.lastUpdate > OBJECT_TIMEOUT) || 
                             (sharedData.confidence < 0.1);
            
            if(objectLost) {
                panServo.write(PAN_STOP);
                tiltServo.write(TILT_STOP);
            } else {
                float xError = sharedData.rel_x;
                float yError = sharedData.rel_y;
                
                // Горизонтальное управление
                if(fabs(xError) > CENTER_THRESHOLD) {
                    panServo.write((xError > 0) ? PAN_CW : PAN_CCW);
                } else {
                    panServo.write(PAN_STOP);
                }
                
                // Вертикальное управление
                if(fabs(yError) > CENTER_THRESHOLD) {
                    tiltServo.write((yError > 0) ? TILT_CCW : TILT_CW);
                } else {
                    tiltServo.write(TILT_STOP);
                }
            }
            xSemaphoreGive(sharedData.mutex);
        }
        vTaskDelay(10 / portTICK_PERIOD_MS); // 10ms задержка
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

    // Блокируем мьютекс для записи общих данных
    if(xSemaphoreTake(sharedData.mutex, portMAX_DELAY) == pdTRUE) {
        sharedData.rel_x = doc["rel_x"];
        sharedData.rel_y = doc["rel_y"];
        sharedData.confidence = doc["confidence"];
        sharedData.lastUpdate = millis();
        xSemaphoreGive(sharedData.mutex);
    }

    // Формируем эхо-ответ
    char echo_response[128];
    snprintf(echo_response, sizeof(echo_response), 
             "{\"echo\": {\"rel_x\": %.2f, \"rel_y\": %.2f, \"confidence\": %.2f}}", 
             sharedData.rel_x, sharedData.rel_y, sharedData.confidence);
    
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

    // Инициализация сервоприводов
    panServo.attach(PAN_SERVO_PIN);
    tiltServo.attach(TILT_SERVO_PIN);
    panServo.write(PAN_STOP);
    tiltServo.write(TILT_STOP);
    Serial.println("Сервоприводы инициализированы");

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

    // Создаем отдельный поток для сервоприводов
    xTaskCreatePinnedToCore(
        servoTask,    // Функция задачи
        "ServoTask", // Имя задачи
        4096,        // Размер стека
        NULL,        // Параметры
        1,           // Приоритет
        NULL,        // Дескриптор задачи
        0            // Ядро (0 или 1)
    );

    Serial.println("Система запущена. Поток сервоприводов активен.");
}

void loop() {
    server.handleClient();
    // Основной цикл теперь занимается только веб-сервером
    delay(1);
}