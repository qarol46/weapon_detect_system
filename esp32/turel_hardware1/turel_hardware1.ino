#include <WebServer.h>
#include <WiFi.h>
#include <esp32cam.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>

const char* WIFI_SSID = "Engi-Teams_2.4G";
const char* WIFI_PASS = "Neutrhino1";

WebServer server(80);

struct {
    float rel_x;
    float rel_y;
    int abs_x;
    int abs_y;
    int width;
    int height;
    float confidence;
    unsigned long lastUpdate = 0;
} objectCoords;

Servo panServo;
Servo tiltServo;

const int PAN_SERVO_PIN = 12;
const int TILT_SERVO_PIN = 13;
const int PAN_STOP = 90;
const int TILT_STOP = 90;
const int PAN_CW = 80;
const int PAN_CCW = 100;
const int TILT_CW = 80;
const int TILT_CCW = 100;
const float CENTER_THRESHOLD = 0.2;
const unsigned long OBJECT_TIMEOUT = 275; // Таймаут потери объекта (мс)

void serveJpg() {
    auto frame = esp32cam::capture();
    if (frame == nullptr) {
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
    if (server.method() != HTTP_POST) {
        server.send(405, "Method Not Allowed");
        return;
    }

    DynamicJsonDocument doc(256);
    if (deserializeJson(doc, server.arg("plain"))) {
        server.send(400, "Bad Request");
        return;
    }

    objectCoords = {
        doc["rel_x"],
        doc["rel_y"],
        doc["abs_x"],
        doc["abs_y"],
        doc["width"],
        doc["height"],
        doc["confidence"],
        millis()
    };

    Serial.printf("Обнаружен объект: X=%.2f Y=%.2f Conf=%.2f\n", 
                 objectCoords.rel_x, objectCoords.rel_y, objectCoords.confidence);
    server.send(200, "application/json", "{\"status\":\"success\"}");
}

void updateServos() {
    unsigned long currentTime = millis();
    bool objectLost = (currentTime - objectCoords.lastUpdate > OBJECT_TIMEOUT) || (objectCoords.confidence < 0.1);
    
    if (objectLost) {
        panServo.write(PAN_STOP);
        tiltServo.write(TILT_STOP);
        static unsigned long lastPrint = 0;
        if (currentTime - lastPrint > 1000) {
            Serial.println("Объект потерян, сервоприводы остановлены");
            lastPrint = currentTime;
        }
        return;
    }

    float xError = objectCoords.rel_x;
    float yError = objectCoords.rel_y;

    static int lastPanAction = PAN_STOP;
    static int lastTiltAction = TILT_STOP;
    
    // Горизонтальное управление
    if (fabs(xError) > CENTER_THRESHOLD) {
        int newAction = (xError > 0) ? PAN_CW : PAN_CCW;
        if (newAction != lastPanAction) {
            panServo.write(newAction);
            lastPanAction = newAction;
            Serial.printf("PAN: %s\n", (newAction == PAN_CCW) ? "CCW" : "CW");
        }
    } else if (lastPanAction != PAN_STOP) {
        panServo.write(PAN_STOP);
        lastPanAction = PAN_STOP;
        Serial.println("PAN: STOP");
    }

    // Вертикальное управление
    if (fabs(yError) > CENTER_THRESHOLD) {
        int newAction = (yError > 0) ? TILT_CCW : TILT_CW;
        if (newAction != lastTiltAction) {
            tiltServo.write(newAction);
            lastTiltAction = newAction;
            Serial.printf("TILT: %s\n", (newAction == TILT_CCW) ? "CCW" : "CW");
        }
    } else if (lastTiltAction != TILT_STOP) {
        tiltServo.write(TILT_STOP);
        lastTiltAction = TILT_STOP;
        Serial.println("TILT: STOP");
    }
}

void setup() {
    Serial.begin(115200);
    
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

    panServo.attach(PAN_SERVO_PIN);
    tiltServo.attach(TILT_SERVO_PIN);
    panServo.write(PAN_STOP);
    tiltServo.write(TILT_STOP);
    Serial.println("Сервоприводы инициализированы в положение STOP");

    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    
    Serial.printf("\nWiFi Connected\nIP: %s\n", WiFi.localIP().toString().c_str());
    
    server.on("/cam-hi.jpg", serveJpg);
    server.on("/coords", HTTP_POST, handleCoords);
    server.begin();
}

void loop() {
    server.handleClient();
    updateServos(); // Всегда вызываем, чтобы обрабатывать остановку при потере объекта
}