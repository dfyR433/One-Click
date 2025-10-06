<p align="center">
  <img src="assets/logo/one_click_logo.png" alt="One Click Logo" width="200"/>
</p>

<h1 align="center">One Click</h1>
<p align="center"><b>ESP32 Wi-Fi File Downloader Firmware</b></p>
<p align="center">Download. Fast. Effortless.</p>
# One Click
> **ESP32 Wi-Fi File Downloader Firmware**

[![Platform](https://img.shields.io/badge/platform-ESP32-blue?style=for-the-badge&logo=espressif)]()
[![License](https://img.shields.io/badge/license-MIT-green?style=for-the-badge)]()
[![Build](https://img.shields.io/badge/build-passing-success?style=for-the-badge)]()

---

## Overview
**One Click** is a compact ESP32 firmware that enables instant file downloads over Wi-Fi.  
Power it up, connect to the access point, and grab your files directly from the browser — no drivers, cables, or setup tools.

---

## ✨ Key Features
- 🚀 **Single-click file download** over Wi-Fi  
- 📡 **Standalone Access Point mode** — no router needed  
- 💾 **SPIFFS**  
- 🔒 **Optional access password**  
- ⚙️ **Built with ESP-IDF for stability and speed**  

---

## 🧩 Hardware & Software
### **Hardware**
- ESP32-WROOM / LOLIN32 / NodeMCU-32 or compatible  
- Power supply: 5 V via USB or VIN  

### **Software**
- ESP-IDF v5.5

---

## ⚙️ Installation
### **ESP-IDF**
```bash
git clone https://github.com/yourusername/one-click.git
cd one-click
idf.py set-target esp32
idf.py build
idf.py -p COMx flash monitor
