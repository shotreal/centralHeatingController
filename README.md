# Project Name

## Description
This project is an Arduino-based system for managing heating, hot water, and other functionalities. It includes OTA updates, WiFi connectivity, MQTT communication, and integration with Home Assistant.

## Features
- **Legionella Program Switch**: Enables or disables the Legionella program.
- **OTA Updates**: Handles over-the-air updates using ArduinoOTA.
- **WiFi Connectivity**: Connects to WiFi and monitors signal strength.
- **MQTT Communication**: Communicates with an MQTT broker.
- **Home Assistant Integration**: Updates Home Assistant with the current status.
- **Heating Management**: Manages the heating system.
- **Hot Water Management**: Manages the hot water system.
- **Time and Day Management**: Manages the current time and day.


### Configuration
1. Update the WiFi credentials in credentials.h:
    ```cpp
    const char* ssid = "your-SSID";
    const char* password = "your-PASSWORD";
    ```
2. Configure the MQTT broker details:
    ```cpp
    const char* mqtt_server = "your-mqtt-broker-address";
    ```
