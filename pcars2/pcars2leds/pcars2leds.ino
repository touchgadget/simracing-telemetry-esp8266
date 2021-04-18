/*
MIT License

Copyright (c) 2021 touchgadgetdev@gmail.com

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
 */
/*
 * Capture and show telemetry from the racing game Project Cars 2 running
 * on a PS4. Should work for PC and Xbox since both support the UDP API
 * but this has only been tested on a PS4.
 *
 * On the PS4, set UDP Frequency to 1 and UDP Protocol Version to 2.
 *
 * The UDP API is documented here.
 * https://www.projectcarsgame.com/two/project-cars-2-api/
 * This program depends on the Project Cars 2 Patch 5 API.
 * The file SMS_UDP_Definitions.hpp is from above link.
 *
 * This program has virtually no error checking so use at your own risk.
 */

#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <WiFiManager.h>  // https://github.com/tzapu/WiFiManager
#include <NeoPixelBus.h>  // Install using IDE Library manager

const uint16_t PixelCount = 16;
const uint8_t PixelPin = 3;  // make sure to set this to the correct pin, ignored for Esp8266
#define colorSaturation 255

//NeoPixelBus<NeoGrbFeature, Neo800KbpsMethod> strip(PixelCount, PixelPin);
// for esp8266 omit the pin. This uses NodeMCU pin RX (GPIO03) and DMA.
NeoPixelBus<NeoGrbFeature, Neo800KbpsMethod> strip(PixelCount);

RgbColor red(colorSaturation, 0, 0);
RgbColor green(0, colorSaturation, 0);
RgbColor blue(0, 0, colorSaturation);
RgbColor white(colorSaturation);
RgbColor black(0);

// All the structs must be packed.
// The hpp file is from https://www.projectcarsgame.com/two/project-cars-2-api/
// Patch 5 API.
#pragma pack(push)
#pragma pack(1)
#include "SMS_UDP_Definitions.hpp"
#pragma pack(pop)

// buffer for receiving data
uint8_t packetBuffer[SMS_UDP_MAX_PACKETSIZE]; //buffer to hold incoming packet,

WiFiUDP Udp;

void setup() {
  WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP
  Serial.begin(115200);
  strip.Begin();
  strip.Show();

  //WiFiManager, Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wm;

  //reset settings - wipe credentials for testing
  //wm.resetSettings();

  // Automatically connect using saved credentials,
  // if connection fails, it starts an access point with the specified name ( "AutoConnectAP"),
  // if empty will auto generate SSID, if password is blank it will be anonymous AP (wm.autoConnect())
  // then goes into a blocking loop awaiting configuration and will return success result

  bool res;
  // res = wm.autoConnect(); // auto generated AP name from chipid
  res = wm.autoConnect("Project Cars 2 LEDs");
  // res = wm.autoConnect("AutoConnectAP","password"); // password protected ap

  if(!res) {
    Serial.println("Failed to connect");
    ESP.restart();
  } 
  else {
    //if you get here you have connected to the WiFi    
    Serial.print("Connected! IP address: ");
    Serial.println(WiFi.localIP());
    Serial.printf("UDP server on port %d\n", SMS_UDP_PORT);
    Udp.begin(SMS_UDP_PORT);
  }
}

const char * const GEAR_NAMES[16] = {
  "N",  // Neutral
  "1",
  "2",
  "3",
  "4",
  "5",
  "6",
  "7",
  "8",
  "9",
  "10",
  "11",
  "12",
  "13",
  "14",
  "R",  // Reverse
};

// This determines the LED colors.
const RgbColor RPM_COLORS[16] = {
  green, green, green, green, green,
  red, red, red, red, red,
  blue, blue, blue, blue, blue, white 
};

void show_rpm(int rpm, int max_rpm)
{
  int stop = (((rpm * ((PixelCount - 1) * 10)) / max_rpm) + 5) / 10;
  for (int i = 0; i < PixelCount; i++) {
    if (i <= stop) {
      strip.SetPixelColor(i, RPM_COLORS[i]);
    }
    else {
      strip.SetPixelColor(i, black);
    }
  }
  strip.Show();
}

void loop() {
  // if there's data available, read a packet
  int packetSize = Udp.parsePacket();
  if (packetSize > 0) {
    // read the packet into packetBufffer
    int n = Udp.read(packetBuffer, sizeof(packetBuffer));
    if (n >= sizeof(PacketBase)) {
      struct PacketBase *header = (struct PacketBase *)&packetBuffer;
      switch (header->mPacketType) {
        case eCarPhysics:
          {
            struct sTelemetryData *carPhysics = (struct sTelemetryData *)&packetBuffer;
#if 0
            Serial.printf("sFuelCapacity %u liters\n", carPhysics->sFuelCapacity);
            Serial.printf("sFuelLevel %f\n", carPhysics->sFuelLevel);
            float fuel_liters = carPhysics->sFuelLevel * (float)carPhysics->sFuelCapacity;
            Serial.printf("Fuel %f liters\n", fuel_liters);
            float fuel_gallons = fuel_liters * 0.2641729f;
            Serial.printf("Fuel %f gallons\n", fuel_gallons);

            Serial.printf("sSpeed %f meters/s\n", carPhysics->sSpeed);
            float speed_kph = carPhysics->sSpeed * (3600.0f/1000.0f);
            Serial.printf("sSpeed %f KPH\n", speed_kph);
            float speed_mph = speed_kph * 0.62137f;
            Serial.printf("sSpeed %f MPH\n", speed_mph);

            uint8_t currentGear = carPhysics->sGearNumGears & 0x0F;
            Serial.printf("sGear %u\n", currentGear);
            Serial.printf("Gear %s\n", GEAR_NAMES[currentGear]);
            Serial.printf("sNumGears %u\n", carPhysics->sGearNumGears >> 4);
#endif
            //Serial.printf("sRpm %u sMaxRpm %u\n", carPhysics->sRpm, carPhysics->sMaxRpm);
            show_rpm(carPhysics->sRpm, carPhysics->sMaxRpm);
          }
          break;
        case eRaceDefinition:
          {
            struct sRaceData *raceData = (struct sRaceData *)&packetBuffer;
#if 0
            Serial.printf("sWorldFastestLapTime %f secs\n", raceData->sWorldFastestLapTime);
            Serial.printf("sTrackLength %f meters\n", raceData->sTrackLength);
#endif
          }
          break;
        case eParticipants:
          {
            struct sParticipantsData *participants = (struct sParticipantsData *)&packetBuffer;
          }
          break;
        case eTimings:
          {
            struct sTimingsData *timings = (struct sTimingsData *)&packetBuffer;
          }
          break;
        case eGameState:
          {
            struct sGameStateData *gameState = (struct sGameStateData *)&packetBuffer;
          }
          break;
        case eWeatherState: // not sent at the moment, information can be found in the game state packet
          break;
        case eVehicleNames: //not sent at the moment
          break;
        case eTimeStats:
          {
            struct sTimeStatsData *timeStats = (struct sTimeStatsData *)&packetBuffer;
          }
          break;
        case eParticipantVehicleNames:
          {
            struct sParticipantVehicleNamesData *participantVehicleNames = (struct sParticipantVehicleNamesData *)&packetBuffer;
          }
          break;
        default:
          break;
      }
    }
  }
}
