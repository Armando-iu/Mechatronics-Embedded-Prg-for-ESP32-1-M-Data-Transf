Mechatronics 25: Embedded-Prg-for-ESP32-1-M-Data-Transf

## Jist of the assignment:

Use multiple ESP-32 controllers to send data to a single base station using both ESP-NOW and Wifi (Where the base station is the tcp server). Whenever any information sent using esp-now cause's base station to light up the yellow LED (as seen below) and Green LED for the wifi.
- This is mostly for debugging, as I was lazy to check on the serial monitor when its constantly getting updated
- it is possible to receive both requests from the wifi and esp-now for the data

### Design constraints and considerations
- I assumed that all the memory and processing power bandwidth is used for the wireless transmission
- So far the program has only been proven to work with 3 ESP32 Wrooms
  - Even though it can theoratically support 20 devices
- Channel of wifi has to be the same as channel for ESP-NOW. Thats why the channel is set after the wifi is set

<br>

## Hardware Requirements
- Minimum 3 ESP32 Wroom
- Router/Hotspot
(Sort of optional)
- 1 resistor under 2k
- 2 LED

<br>

## Software Requirements
- ESP-IDF
- IDE (VS Code with PlatformIO)

<br>

## PlatformIO configs:
- Programming Framework: ESP-IDF
- Board: Espressif ESP32 Dev Module

<br>
## Images of how stuff looked like

Base stations
![IMG_1557](https://github.com/user-attachments/assets/b2c6ad88-971a-4e95-a2dc-bc81d7171ea8)

When any esp-now data has been received/receive callback is triggered:
![IMG_1555](https://github.com/user-attachments/assets/20e5b9fb-d135-4a05-9dfd-f4f93f474cae)

When any request has been to the server/request callback is triggered:
![IMG_1556](https://github.com/user-attachments/assets/b5472122-d3b1-45f0-82ad-c17c0da91be0)
