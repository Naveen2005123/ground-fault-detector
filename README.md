# ⚡ Ground Fault Detection System — Live Dashboard
ESP32 + ADS1115 + ACS712 → Firebase Realtime Database → real-time web dashboard.

## Setup
1. Firebase Console → Realtime Database → Rules → paste `database.rules.json` → Publish.
2. Arduino IDE: open `firmware/ground_fault_esp32/ground_fault_esp32.ino` (libraries: Adafruit ADS1X15, SSD1306, GFX). Edit `secrets.h` with your WiFi.
3. Website: push these files to GitHub → Settings → Pages (branch `main`, root) or import into Vercel.

`firmware/ground_fault_esp32/secrets.h` is git-ignored so your WiFi password is not uploaded; use `secrets.example.h` as the template.
Data path: `/groundfault/live` and `/groundfault/events`. Thresholds: Warning ≥ 0.30 A, Alert ≥ 0.50 A (demo values).
