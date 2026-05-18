import cv2
import os

VIDEO_PATH = "badapple.mp4"
OUTPUT_DIR = "frames"

WIDTH = 120
HEIGHT = 40

os.makedirs(OUTPUT_DIR, exist_ok=True)

cap = cv2.VideoCapture(VIDEO_PATH)
if not cap.isOpened():
    raise FileNotFoundError(f"Could not open {VIDEO_PATH}")

fps = cap.get(cv2.CAP_PROP_FPS)
if not fps or fps <= 0:
    fps = 30.0

frame_count = 0

while True:
    ret, frame = cap.read()
    if not ret:
        break

    frame = cv2.resize(frame, (WIDTH, HEIGHT), interpolation=cv2.INTER_AREA)
    gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)

    # 1 = white pixel, 0 = black pixel
    _, bw = cv2.threshold(gray, 127, 1, cv2.THRESH_BINARY)

    filename = f"{OUTPUT_DIR}/frame_{frame_count:04d}.txt"
    with open(filename, "w", encoding="utf-8") as f:
        for row in bw:
            f.write("".join("1" if px else "0" for px in row) + "\n")

    frame_count += 1

cap.release()

with open(f"{OUTPUT_DIR}/meta.txt", "w", encoding="utf-8") as f:
    f.write(f"fps={fps}\n")
    f.write(f"frames={frame_count}\n")

print(f"Done. Saved {frame_count} frames at {fps:.3f} FPS.")