# RMG-Cutting-ERP-Logger-System
To modernize the Ready-Made Garment (RMG) cutting department (and/or next swing department) by replacing manual pen-and-paper tracking with a real-time, RFID-based IoT solution that integrates directly with the factory's ERP.

## Key Features

* **End-to-End Workflow Digitization:** Tracks knit bundles through every physical stage of the cutting lifecycle: laying, cutting, numbering, bundling, card panel checking, reject replacement, counting, and final swing line input.

* **RFID/NFC Bundle Tagging:** Assigns a unique, scannable RFID/NFC tag to each individual knit bundle, transforming raw materials into instantly traceable digital assets.

* **Custom IoT Edge Devices:** Purpose-built floor scanners engineered using an ESP32 microcontroller, a PN532 module for RFID/NFC reading, and a TFT touch display for intuitive operator interaction.

* **Precision Timekeeping:** Integrates an RTC DS3231 module to ensure highly accurate, offline-capable timestamping of all process movements.

* **MicroPython Firmware:** The device's operating logic and hardware integrations are powered by efficient, lightweight MicroPython.

* **Real-Time ERPNext API Integration:** Bypasses the end-of-day data entry bottleneck by pushing instant tag location and process updates directly to ERPNext the moment a bundle is scanned.

* **Automated Quality Control Tracking:** Streamlines the reject and replacement process by logging QC data dynamically on the floor rather than relying on delayed manual counts.
