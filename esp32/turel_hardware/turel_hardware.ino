#include "esp_camera.h"
#include <WiFi.h>
#include <WiFiClient.h>

// Настройки WiFi
const char* ssid = "MGTS_GPON_DA64";
const char* password = "U3TmHkXY";
const char* server_ip = "192.168.1.8"; // IP ноутбука
const int server_port = 8765;

WiFiClient client;

// Конфигурация камеры (AI Thinker)
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

void setup() {
  Serial.begin(115200);
  
  // Инициализация камеры
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_QVGA; // Уменьшенное разрешение для стабильности
  config.jpeg_quality = 12;           // Среднее качество
  config.fb_count = 1;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Ошибка камеры: 0x%x", err);
    return;
  }

  // Подключение к WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi подключен");
}

void reconnect() {
  Serial.println("Попытка переподключения...");
  if (client.connect(server_ip, server_port)) {
    Serial.println("Успешно переподключены");
  } else {
    Serial.println("Ошибка переподключения");
  }
}

void loop() {
  if (!client.connected()) {
    reconnect();
    delay(1000);
    return;
  }

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    Serial.println("Ошибка захвата кадра");
    return;
  }

  // Отправка размера кадра
  uint32_t img_size = fb->len;
  size_t sent = client.write((const char*)&img_size, 4);
  
  if (sent != 4) {
    Serial.println("Ошибка отправки размера кадра");
    client.stop();
    esp_camera_fb_return(fb);
    return;
  }

  // Отправка данных кадра по частям
  size_t offset = 0;
  while (offset < fb->len) {
    size_t chunk_size = (fb->len - offset) > 1024 ? 1024 : (fb->len - offset);
    sent = client.write(fb->buf + offset, chunk_size);
    
    if (sent != chunk_size) {
      Serial.println("Ошибка отправки данных");
      client.stop();
      break;
    }
    offset += sent;
    delay(10); // Небольшая пауза между пакетами
  }

  esp_camera_fb_return(fb);
  delay(100); // Увеличенная задержка между кадрами
}