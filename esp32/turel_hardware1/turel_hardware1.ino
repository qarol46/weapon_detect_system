#include <WebServer.h>
#include <WiFi.h>
#include <esp32cam.h>
#include <ArduinoJson.h>

const char* WIFI_SSID = "TP-LINK_041C";
const char* WIFI_PASS = "1122332211";

WebServer server(80);

// Подключение серв и драйвера моторов
const int panPin = 12;     // Горизонтальная серва
const int tiltPin = 13;    // Вертикаьлная серва
const int motorPin = 14;   // Управляющий пин драйверов мотора
const int motorDuration = 500; // Длительность выстрела(время работы двигателей)

// Servo control
float panAngle = 90.0;
float tiltAngle = 90.0;
float panSpeed = 0.5;      // Скорость поворота горизонтальной сервы
float tiltSpeed = 0.3;     // Скорость поворота вертикальной сервы
unsigned long lastUpdate = 0;
const long updateInterval = 20; // Скорость обновления, мс

bool isFiring = false;
unsigned long fireStartTime = 0;

// Разрешение камеры
static auto hiRes = esp32cam::Resolution::find(800, 600);

void setupPins() {
  pinMode(panPin, OUTPUT);
  pinMode(tiltPin, OUTPUT);
  pinMode(motorPin, OUTPUT);
  digitalWrite(motorPin, LOW);
  //Инициализация сервоприводов(скорее всего нафиг ненужная фигня) - просто выставим их в нужно положение изначально, которое будет считаться (90, 90) по двум углам
  analogWrite(panPin, map(90, 0, 180, 0, 255));
  analogWrite(tiltPin, map(90, 0, 180, 0, 255));
}

void updateServos() {
  unsigned long currentTime = millis();
  if (currentTime - lastUpdate < updateInterval) return;
  lastUpdate = currentTime;

  panAngle = constrain(panAngle, 0, 180);
  tiltAngle = constrain(tiltAngle, 0, 90);
  
  analogWrite(panPin, map(int(panAngle), 0, 180, 0, 255));
  analogWrite(tiltPin, map(int(tiltAngle), 0, 180, 0, 255));

  if (isFiring && (currentTime - fireStartTime > motorDuration)) {
    digitalWrite(motorPin, LOW);
    isFiring = false;
    Serial.println("Motor stopped");
  }
}

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

void handleJpgHi() {
  if (!esp32cam::Camera.changeResolution(hiRes)) {
    Serial.println("SET-HI-RES FAIL");
  }
  serveJpg();
}

void handleAlert() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Method Not Allowed");
    return;
  }
  
  String body = server.arg("plain");
  DynamicJsonDocument doc(256);
  DeserializationError error = deserializeJson(doc, body);
  
  if (error) {
    server.send(400, "text/plain", "Bad Request");
    return;
  }
  
  bool button_state = doc["button_state"];
  bool fire_command = doc["fire_command"];

  if (button_state && fire_command && !isFiring) {
    digitalWrite(motorPin, HIGH);
    isFiring = true;
    fireStartTime = millis();
    Serial.println("FIRE COMMAND! Activating motor...");
  }

  if (!button_state) {
    panAngle = 90;
    tiltAngle = 90;
    server.send(200, "application/json", "{\"status\":\"disabled\"}");
    return;
  }

  int person_x = doc["person_center"][0];
  int person_y = doc["person_center"][1];
  int center_x = 400; // Половина от 800(ширины изображения)
  int center_y = 300; // Половина от 600(высоты изображения)
  int deadzone = 50;

  if (abs(person_x - center_x) > deadzone) {
    if (person_x > center_x) {
      panAngle += panSpeed;
    } else {
      panAngle -= panSpeed;
    }
  }

  if (abs(person_y - center_y) > deadzone) {
    if (person_y > center_y) {
      tiltAngle -= tiltSpeed;
    } else {
      tiltAngle += tiltSpeed;
    }
  }

  server.send(200, "application/json", "{\"status\":\"received\"}");
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  
  setupPins();
  {
    using namespace esp32cam;
    Config cfg;
    cfg.setPins(pins::AiThinker);
    cfg.setResolution(hiRes);
    cfg.setBufferCount(2);
    cfg.setJpeg(80);
 
    bool ok = Camera.begin(cfg);
    Serial.println(ok ? "CAMERA OK" : "CAMERA FAIL");
  }
  
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
  
  Serial.print("http://");
  Serial.println(WiFi.localIP());
  Serial.println("  /cam-hi.jpg");
  Serial.println("  /alert (POST)");
  
  server.on("/cam-hi.jpg", handleJpgHi);
  server.on("/alert", HTTP_POST, handleAlert);
  
  server.begin();
}

void loop() {
  server.handleClient();
  updateServos();
}