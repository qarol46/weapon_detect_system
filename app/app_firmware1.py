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
SEND_INTERVAL = 0.03
OBJECT_TIMEOUT = 0.5

class ObjectTracker:
    def __init__(self):
        self.model = YOLO(MODEL_PATH)
        self.last_sent = 0
        self.last_frame = None
        self.class_id = self.get_class_id()
        self.last_detection_time = 0
        self.communication_errors = 0
        self.button_state = False
        self.button_rect = (20, 20, 200, 50)

    def get_class_id(self):
        if hasattr(self.model, 'names'):
            for id, name in self.model.names.items():
                if name == TARGET_CLASS:
                    return id
        return 0

    def process_frame(self, img):
        # Поворачиваем изображение на 180 градусов
        rotated_img = cv2.rotate(img, cv2.ROTATE_180)
        self.last_frame = rotated_img
        self.draw_interface(rotated_img)
        
        results = self.model.predict(rotated_img, verbose=False)
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
            self.send_data(x1, y1, x2, y2, max_conf)
            self.draw_objects(rotated_img, x1, y1, x2, y2, max_conf)
            self.last_detection_time = time.time()
        else:
            if time.time() - self.last_detection_time > OBJECT_TIMEOUT:
                self.send_data(0, 0, 0, 0, 0.0)
        
        return rotated_img

    def draw_interface(self, img):
        button_color = (0, 255, 0) if self.button_state else (0, 0, 255)
        cv2.rectangle(img, 
                     (self.button_rect[0], self.button_rect[1]),
                     (self.button_rect[0] + self.button_rect[2], self.button_rect[1] + self.button_rect[3]),
                     button_color, -1)
        button_text = "Done" if self.button_state else "Danger"
        cv2.putText(img, button_text, 
                   (self.button_rect[0] + 10, self.button_rect[1] + 30), 
                   cv2.FONT_HERSHEY_SIMPLEX, 0.7, (255, 255, 255), 2)

    def handle_click(self, x, y):
        bx, by, bw, bh = self.button_rect
        if bx <= x <= bx + bw and by <= y <= by + bh:
            self.button_state = not self.button_state
            print(f"Button state changed to: {self.button_state}")
            return True
        return False

    def send_data(self, x1, y1, x2, y2, conf):
        if time.time() - self.last_sent < SEND_INTERVAL:
            return

        height, width = self.last_frame.shape[:2]
        center_x, center_y = width // 2, height // 2
        obj_x, obj_y = (x1 + x2) // 2, (y1 + y2) // 2
        
        rel_x = (obj_x - center_x) / center_x if x1 != 0 else 0.0
        rel_y = (center_y - obj_y) / center_y if y1 != 0 else 0.0
        
        rel_x = max(-1.0, min(1.0, rel_x))
        rel_y = max(-1.0, min(1.0, rel_y))
        
        payload = {
            "rel_x": float(rel_x),
            "rel_y": float(rel_y),
            "abs_x": obj_x,
            "abs_y": obj_y,
            "width": x2 - x1,
            "height": y2 - y1,
            "confidence": float(conf),
            "button_state": int(self.button_state)
        }
        
        try:
            print("-------------------")
            status = 1 if self.button_state else 0
            print(f"SEND X {rel_x:.2f} Y {rel_y:.2f} Conf: {conf:.2f} State: {status}")
            
            resp = requests.post(COORDS_URL, json=payload, timeout=0.3)
            if resp.status_code == 200:
                self.last_sent = time.time()
                echo_data = resp.json()
                if "echo" in echo_data:
                    echo = echo_data["echo"]
                    print(f"RECV X {echo['rel_x']:.2f} Y {echo['rel_y']:.2f} State: {echo.get('button_state', 'N/A')}")
            else:
                print(f"RECV: Error {resp.status_code}")
            
            print("-------------------")
            self.communication_errors = 0
            
        except Exception as e:
            self.communication_errors += 1
            if self.communication_errors % 5 == 0:
                print(f"Communication error ({self.communication_errors}): {str(e)[:50]}...")

    def draw_objects(self, img, x1, y1, x2, y2, conf):
        cv2.rectangle(img, (x1, y1), (x2, y2), (0, 255, 0), 2)
        label = f"{TARGET_CLASS} {conf:.2f}"
        cv2.putText(img, label, (x1, y1 - 10), 
                   cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)
        center_x, center_y = (x1 + x2) // 2, (y1 + y2) // 2
        cv2.circle(img, (center_x, center_y), 5, (0, 0, 255), -1)
        h, w = img.shape[:2]
        cv2.line(img, (w//2, h//2), (center_x, center_y), (255, 0, 0), 2)

def mouse_callback(event, x, y, flags, param):
    if event == cv2.EVENT_LBUTTONDOWN:
        tracker.handle_click(x, y)

def main():
    global tracker
    tracker = ObjectTracker()
    cv2.namedWindow("Object Tracking", cv2.WINDOW_NORMAL)
    cv2.setMouseCallback("Object Tracking", mouse_callback)
    
    print("\n===== Object Tracking System =====\n")
    print(f"Target: {TARGET_CLASS} (ID: {tracker.class_id})")
    print(f"Camera: {CAM_URL}")
    print(f"Min confidence: {MIN_CONFIDENCE}")
    print("Click the button to toggle weapon lock state")
    print("\nStarting tracking...\n")
    
    while True:
        try:
            resp = urllib.request.urlopen(CAM_URL, timeout=2)
            img_np = np.array(bytearray(resp.read()), dtype=np.uint8)
            img = cv2.imdecode(img_np, -1)
            
            if img is None:
                print("Warning: Empty frame received")
                time.sleep(0.1)
                continue
                
            processed_img = tracker.process_frame(img)
            cv2.imshow("Object Tracking", processed_img)
            
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