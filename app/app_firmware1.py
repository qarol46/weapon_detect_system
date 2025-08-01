import cv2
import urllib.request
import numpy as np
import requests
import time
from ultralytics import YOLO

# Настройки
CAM_URL = 'http://192.168.0.201/cam-hi.jpg'
COORDS_URL = 'http://192.168.0.201/coords'
MODEL_PATH = "yolo-Weights/yolov8n.pt"
TARGET_CLASS = "person"
MIN_CONFIDENCE = 0.35
SEND_INTERVAL = 0.03  # Интервал отправки (секунды)
OBJECT_TIMEOUT = 0.5  # Таймаут потери объекта (секунды)

class ObjectTracker:
    def __init__(self):
        self.model = YOLO(MODEL_PATH)
        self.last_sent = 0
        self.last_frame = None
        self.class_id = self.get_class_id()
        self.last_detection_time = 0
        self.communication_errors = 0

    def get_class_id(self):
        """Получаем ID класса 'person' из модели"""
        if hasattr(self.model, 'names'):
            for id, name in self.model.names.items():
                if name == TARGET_CLASS:
                    return id
        return 0  # Fallback для старых версий

    def process_frame(self, img):
        self.last_frame = img
        results = self.model.predict(img, verbose=False)
        best_box = None
        max_conf = 0.0
        
        for result in results:
            for box in result.boxes:
                if box.cls.item() == self.class_id:
                    conf = box.conf.item()
                    if conf > max_conf and conf >= MIN_CONFIDENCE:
                        max_conf = conf
                        best_box = box
        
        if best_box is not None:
            x1, y1, x2, y2 = map(int, best_box.xyxy[0].tolist())
            self.send_coordinates(x1, y1, x2, y2, max_conf)
            self.draw_objects(img, x1, y1, x2, y2, max_conf)
            self.last_detection_time = time.time()
        else:
            if time.time() - self.last_detection_time > OBJECT_TIMEOUT:
                self.send_stop_signal()
        
        return img

    def send_coordinates(self, x1, y1, x2, y2, conf):
        if time.time() - self.last_sent < SEND_INTERVAL:
            return

        height, width = self.last_frame.shape[:2]
        center_x, center_y = width // 2, height // 2
        obj_x, obj_y = (x1 + x2) // 2, (y1 + y2) // 2
        
        rel_x = (obj_x - center_x) / center_x
        rel_y = (center_y - obj_y) / center_y
        
        rel_x = max(-1.0, min(1.0, rel_x))
        rel_y = max(-1.0, min(1.0, rel_y))
        
        payload = {
            "rel_x": float(rel_x),
            "rel_y": float(rel_y),
            "abs_x": obj_x,
            "abs_y": obj_y,
            "width": x2 - x1,
            "height": y2 - y1,
            "confidence": float(conf)
        }
        
        try:
            # Вывод отправляемых данных
            print("-------------------")
            print(f"SEND X {rel_x:.2f} Y {rel_y:.2f} Conf: {conf:.2f}")
            
            # Отправка и получение эхо-ответа
            resp = requests.post(COORDS_URL, json=payload, timeout=0.3)
            if resp.status_code == 200:
                self.last_sent = time.time()
                echo_data = resp.json()
                if "echo" in echo_data:
                    echo = echo_data["echo"]
                    print(f"RECV X {echo['rel_x']:.2f} Y {echo['rel_y']:.2f} Conf: {echo['confidence']:.2f}")
                else:
                    print("RECV: No echo data received")
            else:
                print(f"RECV: Error {resp.status_code}")
            
            print("-------------------")
            self.communication_errors = 0
            
        except Exception as e:
            self.communication_errors += 1
            if self.communication_errors % 5 == 0:  # Выводим не каждую ошибку
                print(f"Communication error ({self.communication_errors}): {str(e)[:50]}...")

    def send_stop_signal(self):
        """Отправляет сигнал остановки сервоприводам"""
        if time.time() - self.last_sent < SEND_INTERVAL:
            return
        
        payload = {
            "rel_x": 0.0,
            "rel_y": 0.0,
            "abs_x": 0,
            "abs_y": 0,
            "width": 0,
            "height": 0,
            "confidence": 0.0
        }
        
        try:
            print("-------------------")
            print("SEND STOP SIGNAL (X 0.00 Y 0.00)")
            
            resp = requests.post(COORDS_URL, json=payload, timeout=0.3)
            if resp.status_code == 200:
                self.last_sent = time.time()
                echo_data = resp.json()
                if "echo" in echo_data:
                    echo = echo_data["echo"]
                    print(f"RECV X {echo['rel_x']:.2f} Y {echo['rel_y']:.2f}")
                else:
                    print("RECV: No echo data received")
            else:
                print(f"RECV: Error {resp.status_code}")
            
            print("-------------------")
            self.communication_errors = 0
            
        except Exception as e:
            self.communication_errors += 1
            if self.communication_errors % 5 == 0:
                print(f"STOP signal error ({self.communication_errors}): {str(e)[:50]}...")

    def draw_objects(self, img, x1, y1, x2, y2, conf):
        """Отрисовка bounding box и дополнительной информации"""
        # Рисуем прямоугольник вокруг объекта
        cv2.rectangle(img, (x1, y1), (x2, y2), (0, 255, 0), 2)
        
        # Добавляем подпись с уверенностью
        label = f"{TARGET_CLASS} {conf:.2f}"
        cv2.putText(img, label, (x1, y1 - 10), 
                   cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)
        
        # Рисуем центр объекта
        center_x, center_y = (x1 + x2) // 2, (y1 + y2) // 2
        cv2.circle(img, (center_x, center_y), 5, (0, 0, 255), -1)
        
        # Рисуем линию от центра кадра к объекту
        h, w = img.shape[:2]
        cv2.line(img, (w//2, h//2), (center_x, center_y), (255, 0, 0), 2)

def main():
    tracker = ObjectTracker()
    cv2.namedWindow("Object Tracking", cv2.WINDOW_NORMAL)
    
    print("\n===== Object Tracking System =====\n")
    print(f"Target: {TARGET_CLASS} (ID: {tracker.class_id})")
    print(f"Camera: {CAM_URL}")
    print(f"Min confidence: {MIN_CONFIDENCE}")
    print(f"Send interval: {SEND_INTERVAL}s")
    print("\nStarting tracking...\n")
    
    while True:
        try:
            # Получаем кадр с камеры
            resp = urllib.request.urlopen(CAM_URL, timeout=2)
            img_np = np.array(bytearray(resp.read()), dtype=np.uint8)
            img = cv2.imdecode(img_np, -1)
            
            if img is None:
                print("Warning: Empty frame received")
                time.sleep(0.1)
                continue
                
            # Обрабатываем кадр
            processed_img = tracker.process_frame(img)
            
            # Отображаем результат
            cv2.imshow("Object Tracking", processed_img)
            
            # Выход по клавише 'q'
            if cv2.waitKey(1) & 0xFF == ord('q'):
                break
                
        except KeyboardInterrupt:
            break
        except Exception as e:
            print(f"Camera error: {str(e)[:50]}...")
            time.sleep(1)

    cv2.destroyAllWindows()
    print("\n===== System stopped =====\n")

if __name__ == "__main__":
    main()