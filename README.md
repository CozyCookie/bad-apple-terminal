# 🎬 Bad Apple Terminal Renderer

A real-time terminal-based renderer for *Bad Apple!!* built in C++ using FFmpeg.

> Made by CozyCookie

---

## ⚡ Features

- 🎥 Terminal video playback
- 🌫️ Cinematic grayscale shading
- 🎵 Audio sync (FFplay)
- 🧠 Frame-accurate timing
- 🎮 Motion blur effect
- 📊 Optional FPS + sync debug overlay
- 🖥️ Auto terminal scaling
- ⚡ Fast rendering pipeline

---

## 🖼️ Preview

https://www.youtube.com/watch?v=qsblEsZ4Lps

---

## 📦 Requirements

- C++17 compiler (MSVC recommended)
- FFmpeg installed + added to PATH
- FFplay included with FFmpeg

Check:
ffmpeg -version
ffplay -version

---

## 🚀 How to Run

### Clone

git clone https://github.com/CozyCookie/bad-apple-terminal.git
cd bad-apple-terminal

### Compile

cl /EHsc /std:c++17 main.cpp

### Run
main.exe

---

## 🎛️ Notes

- Place `badapple.mp4` in the same folder
- Optional `badapple.mp3` for audio
- Works best in a fullscreen terminal

---

## 📌 Credits

- Bad Apple!! by Alstroemeria Records
- FFmpeg for decoding
- Made by CozyCookie
