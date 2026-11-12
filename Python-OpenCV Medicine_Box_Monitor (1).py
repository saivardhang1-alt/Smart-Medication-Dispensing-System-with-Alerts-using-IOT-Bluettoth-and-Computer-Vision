import cv2
import numpy as np
import serial
import time
import requests
from datetime import datetime

global cached_reminders, config_sent
config_sent = False

# ================= CONFIG =================
EXPECTED_PASSKEY = "1234"

BLYNK_TOKEN = "gvB14qzNjm9dzWKi1dBUPusKg8Z7wcSH"
BASE_URL = "https://blynk.cloud/external/api"

REMINDER_PIN = "V0"
LOG_PIN = "V1"
COUNT_PIN = "V2"

BT_PORT = "COM5"
BT_BAUD = 9600

BLYNK_POLL_INTERVAL = 10  # seconds

# ================= BLUETOOTH =================
ser = serial.Serial(BT_PORT, BT_BAUD, timeout=1)

# ================= CAMERA =================
ip_cam_url = "http://enter:enter123@10.15.98.47:8080/video"
cap = cv2.VideoCapture(ip_cam_url)

if not cap.isOpened():
    print("IP Camera not accessible")
    exit()

cap.set(cv2.CAP_PROP_FRAME_WIDTH, 1280)
cap.set(cv2.CAP_PROP_FRAME_HEIGHT, 720)

cv2.namedWindow("Medicine Box Monitor", cv2.WINDOW_NORMAL)
cv2.resizeWindow("Medicine Box Monitor", 640, 480)

print("================================================")
print(" Python Medicine Gateway Started")
print("================================================")

# ================= STATE =================
last_blynk_poll = 0
cached_reminders = ""

last_count_frame = None
last_count_time = ""

# ================= HELPER LOG FUNCTION =================
def log(tag, message):
    ts = datetime.now().strftime("%H:%M:%S")
    print(f"[{ts}] {tag} {message}")

# ================= BLYNK FUNCTIONS =================
def blynk_get_all():
    url = f"{BASE_URL}/getAll?token={BLYNK_TOKEN}"
    try:
        r = requests.get(url, timeout=5)
        return r.json()
    except Exception as e:
        log("[BLYNK ERROR]", str(e))
        return {}

def update_blynk(pin, value):
    url = f"{BASE_URL}/update?token={BLYNK_TOKEN}&{pin}={value}"
    requests.get(url)
    log("[PY → BLYNK]", f"{pin} = {value}")

# ================= POLL & PARSE REMINDERS =================
def poll_blynk_reminders():
    global cached_reminders, config_sent

    data = blynk_get_all()
    raw = data.get("v0", "")
    if not raw:
        return

    parts = raw.split(",")
    if parts[0] != EXPECTED_PASSKEY:
        update_blynk(LOG_PIN, "INVALID_PASSKEY")
        return

    times = [t for t in parts[1:] if len(t) == 4 and t.isdigit()]
    new_reminders = ",".join(times)

    if new_reminders != cached_reminders:
        cached_reminders = new_reminders
        config_sent = False
        update_blynk(LOG_PIN, "PASSKEY_OK")
        log("[PY]", f"Reminders updated: {cached_reminders}")

# ================= TABLET DETECTION =================
def count_tablets(frame):
    gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
    blur = cv2.GaussianBlur(gray, (11,11), 0)

    circles = cv2.HoughCircles(
        blur, cv2.HOUGH_GRADIENT,
        dp=1.1, minDist=80,
        param1=60, param2=28,
        minRadius=25, maxRadius=70
    )

    count = 0
    vis = frame.copy()

    if circles is not None:
        circles = np.uint16(np.around(circles[0]))
        for (x,y,r) in circles:
            roi = gray[y-r:y+r, x-r:x+r]
            if roi.size > 0 and np.mean(roi) > 150:
                count += 1
                cv2.circle(vis, (x,y), r, (0,255,0), 2)

    cv2.putText(vis, f"Tablets: {count}",
                (20,40), cv2.FONT_HERSHEY_SIMPLEX,
                1, (0,255,0), 2)

    return count, vis

# ================= MAIN LOOP =================
while True:
    now = time.time()

    # ---- Poll Blynk ----
    if now - last_blynk_poll > BLYNK_POLL_INTERVAL:
        poll_blynk_reminders()
        last_blynk_poll = now

    # ---- Push config ----
    if cached_reminders and not config_sent:
        ser.write((f"REM:{cached_reminders}\n").encode())
        config_sent = True
        log("[PY → ESP32]", f"REM:{cached_reminders}")   #REM:2301,2302,2303,2304,2305

    # ---- Read frame ONCE ----
    ret, frame = cap.read()
    if not ret:
        continue

    # ---- Handle Bluetooth ----
    if ser.in_waiting:
        msg = ser.readline().decode().strip()
        log("[ESP32 → PY]", msg)

        if msg == "a":
            count, vis = count_tablets(frame)

            # Save snapshot
            last_count_frame = vis.copy()
            last_count_time = datetime.now().strftime("%H:%M:%S")

            win_name = f"Count @ {last_count_time}"
            cv2.namedWindow(win_name, cv2.WINDOW_NORMAL)
            cv2.resizeWindow(win_name, 640, 480)
            cv2.imshow(win_name, last_count_frame)


            ser.write(f"{count}\n".encode())
            update_blynk(COUNT_PIN, count)

        elif msg in ["TAKEN", "MISSED", "NOT_TAKEN"]:
            ts = datetime.now().strftime("%H:%M:%S")
            status_msg = f"{msg} @ {ts}"
            update_blynk(LOG_PIN, status_msg)
            log("[STATUS]", status_msg)

    # ---- Live Preview ----
    cv2.imshow("Medicine Box Monitor", frame)

    if cv2.waitKey(1) & 0xFF == ord('q'):
        break

# ================= CLEANUP =================
cap.release()
ser.close()
cv2.destroyAllWindows()
log("[SYSTEM]", "Gateway stopped")
