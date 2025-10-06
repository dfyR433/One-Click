# One Click

> **ESP32 Wi-Fi File Downloader Firmware**

<p align="center">
  <img src="assets/one_click_logo.png" alt="One Click Logo" width="200"/>
</p>

[![Platform](https://img.shields.io/badge/platform-ESP32-blue?style=for-the-badge\&logo=espressif)]()
[![License](https://img.shields.io/badge/license-MIT-green?style=for-the-badge)]()
[![Build](https://img.shields.io/badge/build-passing-success?style=for-the-badge)]()

---

## Overview

**One Click** is a compact ESP32 firmware that enables instant file downloads over Wi-Fi.
Power it up, connect to the access point, and grab your files directly from the browser — no drivers, cables, or setup tools.

---

## Key Features

* **Single-click file download** over Wi‑Fi
* **Standalone Access Point mode** — no router needed
* **SPIFFS support**
* **Optional access password**
* **Built with ESP-IDF for stability and speed**
* **Works on all major operating systems**

---

## Hardware Requirements

* ESP32-WROOM / LOLIN32 / ESP32-S2 / NodeMCU-32 or compatible
* Power supply: 5 V via USB or VIN

---

## Software Requirements

* ESP-IDF v5.5

---

## All OS Support

**One Click** works on every major operating system without additional drivers:

* **Windows** — Chrome, Edge, Firefox, Opera, etc.
* **macOS** — Safari, Chrome, Firefox, etc.
* **Linux** — All major distributions (Ubuntu, Fedora, Debian, Arch, etc.)
* **Android** — Chrome, Firefox, Samsung Internet, etc.
* **iOS** — Safari, Chrome, Firefox, etc.
* **Smart TVs / IoT devices** — Any device with a web browser.

> No special apps, drivers, or installations are required — just connect to the ESP32 access point and use a browser.

---

## Installation

### Clone the repository

```bash
git clone https://github.com/yourusername/one-click.git
cd one-click
```

### Build and flash

```bash
idf.py set-target esp32
idf.py build
idf.py -p COMx flash monitor
```

*(Replace `COMx` with your device’s COM port.)*

---

## Configuration

Edit `main/config.h` to customize your firmware:

```c
#define WIFI_SSID       "OneClickAP"
#define WIFI_PASS       "12345678"      // Leave blank for open network
#define MAX_CONN        4
#define FILE_PATH       "/spiffs/file.zip"
```

---

## Usage

1. Flash the firmware to your ESP32.
2. Power it on — it will create a Wi‑Fi access point.
3. Connect to the AP using a device.
4. Click the file download link to get your file.

---

## SPIFFS Upload

Place your file in the `spiffs` folder, then build the SPIFFS image:

```bash
idf.py spiffs_create
idf.py -p COMx flash
```

---

## Security

* Optionally protect your AP with a password.
* Only serves files stored in SPIFFS.

---

## Contributing

Contributions are welcome! Please open issues or pull requests for improvements or bug fixes.

---

## License

**MIT License** — free to use, modify, and distribute. See [LICENSE](LICENSE) for details.
