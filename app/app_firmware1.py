import cv2
import urllib.request
import numpy as np
import math
from ultralytics import YOLO
import concurrent.futures
import requests
import json

# Определение URL для передачи картинки с камеры и передачи пакета координат и состояния кнопок приложения
url = 'http://192.168.0.102/cam-hi.jpg'
esp32_url = 'http://192.168.0.102/alert'

# Подключение YOLO
model = YOLO("yolo-Weights/yolov8n.pt")

# Целевые классы
target_classes = {
    "pistol": ["pistol", "gun"],
    "rifle": ["rifle", "assault rifle", "machine gun"],
    "knife": ["knife", "sword", "dagger"],
    "person": ["person"]
}

button_state = False
fire_command = False
classNames = ["person", "bicycle", "car", "motorbike", "aeroplane", "bus", "train", "truck", "boat",
              "traffic light", "fire hydrant", "stop sign", "parking meter", "bench", "bird", "cat",
              "dog", "horse", "sheep", "cow", "elephant", "bear", "zebra", "giraffe", "backpack", "umbrella",
              "handbag", "tie", "suitcase", "frisbee", "skis", "snowboard", "sports ball", "kite", "baseball bat",
              "baseball glove", "skateboard", "surfboard", "tennis racket", "bottle", "wine glass", "cup",
              "fork", "knife", "spoon", "bowl", "banana", "apple", "sandwich", "orange", "broccoli",
              "carrot", "hot dog", "pizza", "donut", "cake", "chair", "sofa", "pottedplant", "bed",
              "diningtable", "toilet", "tvmonitor", "laptop", "mouse", "remote", "keyboard", "cell phone",
              "microwave", "oven", "toaster", "sink", "refrigerator", "book", "clock", "vase", "scissors",
              "teddy bear", "hair drier", "toothbrush"]

def toggle_button():
    global button_state
    button_state = not button_state
    print(f"Button state changed to: {button_state}")

def find_weapons_and_persons(results):
    weapons = []
    persons = []
    
    for r in results:
        for box in r.boxes:
            cls = int(box.cls[0])
            class_name = classNames[cls].lower()
            
            for weapon_type, weapon_names in target_classes.items():
                if weapon_type != "person" and any(name in class_name for name in weapon_names):
                    x1, y1, x2, y2 = map(int, box.xyxy[0])
                    center_x = (x1 + x2) // 2
                    center_y = (y1 + y2) // 2
                    weapons.append({
                        "type": weapon_type,
                        "bbox": [x1, y1, x2, y2],
                        "center": (center_x, center_y),
                        "confidence": float(box.conf[0])
                    })
            
            if "person" in class_name:
                x1, y1, x2, y2 = map(int, box.xyxy[0])
                center_x = (x1 + x2) // 2
                center_y = (y1 + y2) // 2
                persons.append({
                    "bbox": [x1, y1, x2, y2],
                    "center": (center_x, center_y)
                })
    
    return weapons, persons

def find_nearest_person(weapon_center, persons):
    if not persons:
        return None
    
    min_dist = float('inf')
    nearest_person = None
    
    for person in persons:
        person_center = person["center"]
        dist = math.dist(weapon_center, person_center)
        
        if dist < min_dist:
            min_dist = dist
            nearest_person = person
    
    return nearest_person

def send_alert_to_esp32(weapon_data, person_data):
    if not person_data:
        return
    
    payload = {
        "weapon_type": weapon_data["type"],
        "weapon_center": weapon_data["center"],
        "person_center": person_data["center"],
        "button_state": button_state,
        "fire_command": fire_command
    }
    
    try:
        response = requests.post(esp32_url, json=payload)
        print(f"Alert sent to ESP32. Response: {response.status_code}")
    except Exception as e:
        print(f"Failed to send alert to ESP32: {e}")

def on_mouse(event, x, y, flags, param):
    global button_state, fire_command
    if event == cv2.EVENT_LBUTTONDOWN:
        if 10 <= x <= 110 and 10 <= y <= 50:
            toggle_button()
    elif event == cv2.EVENT_LBUTTONUP:
        fire_command = False

def run1():
    global fire_command
    cv2.namedWindow("live transmission", cv2.WINDOW_AUTOSIZE)
    cv2.setMouseCallback("live transmission", on_mouse)
    
    while True:
        img_resp = urllib.request.urlopen(url)
        imgnp = np.array(bytearray(img_resp.read()), dtype=np.uint8)
        img = cv2.imdecode(imgnp, -1)
        frame_center = (img.shape[1] // 2, img.shape[0] // 2)

        results = model(img, stream=True)
        weapons, persons = find_weapons_and_persons(results)
        
        for weapon in weapons:
            x1, y1, x2, y2 = weapon["bbox"]
            cv2.rectangle(img, (x1, y1), (x2, y2), (0, 0, 255), 3)
            cv2.putText(img, weapon["type"], (x1, y1 - 10), 
                       cv2.FONT_HERSHEY_SIMPLEX, 0.9, (0, 0, 255), 2)
            
            nearest_person = find_nearest_person(weapon["center"], persons)
            if nearest_person:
                cv2.line(img, weapon["center"], nearest_person["center"], (0, 255, 0), 2)
                send_alert_to_esp32(weapon, nearest_person)
        
        for person in persons:
            x1, y1, x2, y2 = person["bbox"]
            cv2.rectangle(img, (x1, y1), (x2, y2), (255, 0, 0), 2)
        
        cv2.circle(img, frame_center, 5, (0, 255, 255), -1)
        
        button_color = (0, 255, 0) if button_state else (0, 0, 255)
        cv2.rectangle(img, (10, 10), (110, 50), button_color, -1)
        cv2.putText(img, "FIRE" if button_state else "READY", (20, 35), 
                   cv2.FONT_HERSHEY_SIMPLEX, 0.7, (255, 255, 255), 2)
        
        key = cv2.waitKey(1) & 0xFF
        fire_command = button_state and (key == ord(' ') or (cv2.getWindowProperty("live transmission", 0) >= 0 and 
                          cv2.getMouseWheelDelta() > 0))

        cv2.imshow('live transmission', img)
        if key == ord('q'):
            break

    cv2.destroyAllWindows()

if __name__ == '__main__':
    print("started")
    with concurrent.futures.ProcessPoolExecutor() as executor:
        executor.submit(run1)