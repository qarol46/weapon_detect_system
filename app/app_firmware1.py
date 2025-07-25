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
SEND_INTERVAL = 0.05  # Интервал отправки (секунды)

class ObjectTracker:
    def __init__(self):
        self.model = YOLO(MODEL_PATH)
        self.last_sent = 0
        self.last_frame = None
        self.class_id = self.get_class_id()

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
        
        return img

    def send_coordinates(self, x1, y1, x2, y2, conf):
        if time.time() - self.last_sent < SEND_INTERVAL:
            return

        height, width = self.last_frame.shape[:2]
        center_x, center_y = width // 2, height // 2
        obj_x, obj_y = (x1 + x2) // 2, (y1 + y2) // 2
        
        # Вычисляем относительные координаты (-1..1)
        rel_x = (obj_x - center_x) / center_x
        rel_y = (center_y - obj_y) / center_y  # Инвертируем ось Y
        
        # Ограничиваем значения
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
            resp = requests.post(COORDS_URL, json=payload, timeout=0.3)
            if resp.status_code == 200:
                self.last_sent = time.time()
                print(f"Sent: X={rel_x:.2f}, Y={rel_y:.2f}")
        except Exception as e:
            print(f"Send error: {e}")

    def draw_objects(self, img, x1, y1, x2, y2, conf):
        # Рисуем bounding box
        cv2.rectangle(img, (x1, y1), (x2, y2), (0, 255, 0), 2)
        
        # Подпись с координатами
        label = f"{TARGET_CLASS} {conf:.2f}"
        cv2.putText(img, label, (x1, y1 - 10), 
                   cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)
        
        # Центр объекта
        center_x, center_y = (x1 + x2) // 2, (y1 + y2) // 2
        cv2.circle(img, (center_x, center_y), 5, (0, 0, 255), -1)
        
        # Линия от центра кадра
        h, w = img.shape[:2]
        cv2.line(img, (w//2, h//2), (center_x, center_y), (255, 0, 0), 2)

def main():
    tracker = ObjectTracker()
    cv2.namedWindow("Object Tracking", cv2.WINDOW_NORMAL)
    
    print("Starting tracking system...")
    print(f"Target: {TARGET_CLASS} (ID: {tracker.class_id})")
    print(f"Camera: {CAM_URL}")
    print(f"Min confidence: {MIN_CONFIDENCE}")
    
    while True:
        try:
            resp = urllib.request.urlopen(CAM_URL, timeout=2)
            img_np = np.array(bytearray(resp.read()), dtype=np.uint8)
            img = cv2.imdecode(img_np, -1)
            
            if img is None:
                print("Empty frame")
                continue
                
            processed_img = tracker.process_frame(img)
            cv2.imshow("Object Tracking", processed_img)
            
            if cv2.waitKey(1) & 0xFF == ord('q'):
                break
                
        except KeyboardInterrupt:
            break
        except Exception as e:
            print(f"Error: {e}")
            time.sleep(1)

    cv2.destroyAllWindows()
    print("System stopped")

if __name__ == "__main__":
    main()