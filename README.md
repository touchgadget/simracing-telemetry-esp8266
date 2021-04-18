# Car Racing Simulation UDP Telemetry

![NodeMCU ESP8266 with 16 RGB LEDs](./images/ESP8266_RGB_LEDs.jpg)

Popular car racing simulators send car telemetry via UDP packets. This allows
third party programs and devices to access real-time information such as engine
speed, RPM, temperature, etc.

The repo has some examples using an ESP8266 to drive LEDs based on RPM. Two
games are supported: "Project Cars 2" and "F1 2020".

## Dependencies

### NodeMCU 1.0 Pin Map

https://github.com/nodemcu/nodemcu-devkit-v1.0#pin-map

### WiFi Manager Library

Use your phone or tablet to enter your SSID and password. No need to change
the source code.

https://github.com/tzapu/WiFiManager

### NeoPixelBus Library

This library has ESP8266 specific optimizations WS2812 LEDs.

https://github.com/Makuna/NeoPixelBus

### The Project Cars 2 API

https://www.projectcarsgame.com/two/project-cars-2-api/

### F1 2020 API

https://forums.codemasters.com/topic/50942-f1-2020-udp-specification/

## Hardware

![Two 8 RGB sticks soldered end-to-end](./images/LED_16_stick_back.jpg)

Parts

Quantity    |Description
------------|-----------
1           |NodeMCU 1.0 ESP8266 board
2           |8 RGB LED stick
3           |Wires
n/a         |Solder

Connection Table

NodeMCU     |WS2812     |Description
------------|-----------|-----------
G           |GND        |Ground (Blue wire)
3V3         |VCC        |3.3 Volt Power (Orange wire)
RX          |IN         |Arduino GPIO#3 (Yellow wire)

Connecting the WS2812 LEDs this way may not always work because the LEDs are
designed for 5 Volt power and logic levels. But 3.3 Volt works on most of the
time. And this eliminates the need for logic level converters. If you want
maximum brightness, use 5 Volts and logic level converters. The number
of LEDs is limited by the ESP8266 board 3.3V voltage regulator.

The two 8 RGB sticks are soldered together to form one 16 RGB stick. Flexible
strips and rings can be used instead.

## Project Cars 2 LEDs Example

pcars2/pcars2leds/pcars2leds.ino

Drive 16 WS2812 RGB LEDs based on engine RPM.

## F1 2020 LEDs Example

f12020/f12020leds/f12020leds.ino

Drive 16 WS2812 RGB LEDs based on rev lights percent.

