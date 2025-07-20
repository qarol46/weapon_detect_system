import sys
import cv2
import numpy as np
from PyQt5.QtWidgets import (QApplication, QMainWindow, QLabel, 
                           QVBoxLayout, QWidget, QPushButton)
from PyQt5.QtGui import QImage, QPixmap
from PyQt5.QtCore import Qt, QObject, pyqtSignal, QThread
import socket
import struct
import time

class VideoServer(QObject):
    frame_received = pyqtSignal(np.ndarray)
    status_changed = pyqtSignal(str)
    
    def __init__(self):
        super().__init__()
        self.running = False
        self.server_socket = None
        
    def start_server(self, port=8765):
        self.running = True
        try:
            self.server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
            self.server_socket.bind(('0.0.0.0', port))
            self.server_socket.listen(1)
            self.server_socket.settimeout(1)
            self.status_changed.emit(f"Сервер запущен. Ожидание ESP32 на порту {port}...")
            
            while self.running:
                conn = None
                try:
                    conn, addr = self.server_socket.accept()
                    conn.settimeout(5.0)  # Таймаут для операций
                    self.status_changed.emit(f"ESP32 подключена: {addr[0]}")
                    
                    while self.running:
                        try:
                            # Получаем размер кадра
                            header = conn.recv(4)
                            if not header:
                                break
                                
                            if len(header) != 4:
                                break
                                
                            frame_size = struct.unpack('>I', header)[0]
                            
                            # Получаем данные кадра
                            frame_data = bytearray()
                            start_time = time.time()
                            
                            while len(frame_data) < frame_size:
                                chunk = conn.recv(min(4096, frame_size - len(frame_data)))
                                if not chunk:
                                    if time.time() - start_time > 10.0:
                                        break
                                    continue
                                frame_data.extend(chunk)
                            
                            if len(frame_data) == frame_size:
                                frame = cv2.imdecode(
                                    np.frombuffer(frame_data, np.uint8),
                                    cv2.IMREAD_COLOR
                                )
                                if frame is not None:
                                    self.frame_received.emit(frame)
                            
                        except socket.timeout:
                            self.status_changed.emit("Таймаут получения данных")
                            break
                        except Exception as e:
                            self.status_changed.emit(f"Ошибка: {str(e)}")
                            break
                            
                except socket.timeout:
                    continue
                except Exception as e:
                    self.status_changed.emit(f"Ошибка соединения: {str(e)}")
                finally:
                    if conn:
                        conn.close()
                    self.status_changed.emit("Ожидание переподключения ESP32...")
                        
        except Exception as e:
            self.status_changed.emit(f"Фатальная ошибка: {str(e)}")
        finally:
            if self.server_socket:
                self.server_socket.close()
            self.status_changed.emit("Сервер остановлен")
            
    def stop_server(self):
        self.running = False
        if self.server_socket:
            try:
                temp_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                temp_sock.connect(('127.0.0.1', 8765))
                temp_sock.close()
            except:
                pass

class WeaponDetectionApp(QMainWindow):
    def __init__(self):
        super().__init__()
        self.initUI()
        self.start_server()

    def initUI(self):
        self.setWindowTitle("ESP32-CAM Video Stream")
        self.setGeometry(100, 100, 800, 600)
        
        self.video_label = QLabel(self)
        self.video_label.setAlignment(Qt.AlignCenter)
        
        self.status_label = QLabel("Статус: Запуск сервера...")
        
        layout = QVBoxLayout()
        layout.addWidget(self.video_label)
        layout.addWidget(self.status_label)
        
        container = QWidget()
        container.setLayout(layout)
        self.setCentralWidget(container)

    def start_server(self):
        self.server = VideoServer()
        self.server_thread = QThread()
        self.server.moveToThread(self.server_thread)
        
        self.server.frame_received.connect(self.display_frame)
        self.server.status_changed.connect(self.status_label.setText)
        
        self.server_thread.started.connect(self.server.start_server)
        self.server_thread.start()

    def display_frame(self, frame):
        try:
            rgb_image = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
            h, w, ch = rgb_image.shape
            bytes_per_line = ch * w
            qt_image = QImage(rgb_image.data, w, h, bytes_per_line, QImage.Format_RGB888)
            self.video_label.setPixmap(QPixmap.fromImage(qt_image).scaled(
                self.video_label.size(), Qt.KeepAspectRatio))
        except Exception as e:
            print(f"Ошибка отображения: {e}")

    def closeEvent(self, event):
        self.server.stop_server()
        self.server_thread.quit()
        self.server_thread.wait()
        event.accept()

if __name__ == "__main__":
    app = QApplication(sys.argv)
    window = WeaponDetectionApp()
    window.show()
    sys.exit(app.exec_())