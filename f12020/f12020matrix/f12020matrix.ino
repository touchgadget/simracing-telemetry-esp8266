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
 * Capture and show telemetry from the racing game F1 2020 running
 * on a PS4. Should work for PC and Xbox since both support the UDP API
 * but this has only been tested on a PS4.
 *
 * The UDP API is documented here.
 * https://forums.codemasters.com/topic/50942-f1-2020-udp-specification/
 *
 * This program has virtually no error checking so use at your own risk.
 */

#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include <WiFiManager.h>  // https://github.com/tzapu/WiFiManager
#include <NeoPixelBus.h>  // Install using IDE Library manager
#include <MD_MAX72xx.h>   // Install using the IDE Library manager

// 4 8x8 LED matrix using MAX7219 SPI interface
// Define the number of devices we have in the chain and the hardware interface
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES	4

// For NodeMCU ESP8266
#define CLK_PIN   D5  // or SCK
#define DATA_PIN  D7  // or MOSI
#define CS_PIN    D8  // or SS

// SPI hardware interface
MD_MAX72XX mx = MD_MAX72XX(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);

// 16 WS281x RGB LEDs
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

const uint16_t F1_UDP_PORT = 20777;

// All the F1 2020 structs must be packed.
#pragma pack(push)
#pragma pack(1)
struct PacketHeader
{
  uint16_t  m_packetFormat;            // 2020
  uint8_t   m_gameMajorVersion;        // Game major version - "X.00"
  uint8_t   m_gameMinorVersion;        // Game minor version - "1.XX"
  uint8_t   m_packetVersion;           // Version of this packet type, all start from 1
  uint8_t   m_packetId;                // Identifier for the packet type, see below
  uint64_t  m_sessionUID;              // Unique identifier for the session
  float     m_sessionTime;             // Session timestamp
  uint32_t  m_frameIdentifier;         // Identifier for the frame the data was retrieved on
  uint8_t   m_playerCarIndex;          // Index of player's car in the array
  uint8_t   m_secondaryPlayerCarIndex; // Index of secondary player's car in the array (splitscreen)
  // 255 if no second player
};

struct CarTelemetryData
{
  uint16_t  m_speed;                      // Speed of car in kilometres per hour
  float     m_throttle;                   // Amount of throttle applied (0.0 to 1.0)
  float     m_steer;                      // Steering (-1.0 (full lock left) to 1.0 (full lock right))
  float     m_brake;                      // Amount of brake applied (0.0 to 1.0)
  uint8_t   m_clutch;                     // Amount of clutch applied (0 to 100)
  int8_t    m_gear;                       // Gear selected (1-8, N=0, R=-1)
  uint16_t  m_engineRPM;                  // Engine RPM
  uint8_t   m_drs;                        // 0 = off, 1 = on
  uint8_t   m_revLightsPercent;           // Rev lights indicator (percentage)
  uint16_t  m_brakesTemperature[4];       // Brakes temperature (celsius)
  uint8_t   m_tyresSurfaceTemperature[4]; // Tyres surface temperature (celsius)
  uint8_t   m_tyresInnerTemperature[4];   // Tyres inner temperature (celsius)
  uint16_t  m_engineTemperature;          // Engine temperature (celsius)
  float     m_tyresPressure[4];           // Tyres pressure (PSI)
  uint8_t   m_surfaceType[4];             // Driving surface, see appendices
};

struct PacketCarTelemetryData
{
  PacketHeader     m_header;              // Header

  CarTelemetryData m_carTelemetryData[22];

  uint32_t         m_buttonStatus;        // Bit flags specifying which buttons are being pressed
                                          // currently - see appendices
  uint8_t          m_mfdPanelIndex;       // Index of MFD panel open - 255 = MFD closed
                                          // Single player, race – 0 = Car setup, 1 = Pits
                                          // 2 = Damage, 3 =  Engine, 4 = Temperatures
                                          // May vary depending on game mode
  uint8_t          m_mfdPanelIndexSecondaryPlayer;   // See above
  int8_t           m_suggestedGear;       // Suggested gear for the player (1-8)
                                          // 0 if no gear suggested
};

struct CarStatusData
{
  uint8_t     m_tractionControl;          // 0 (off) - 2 (high)
  uint8_t     m_antiLockBrakes;           // 0 (off) - 1 (on)
  uint8_t     m_fuelMix;                  // Fuel mix - 0 = lean, 1 = standard, 2 = rich, 3 = max
  uint8_t     m_frontBrakeBias;           // Front brake bias (percentage)
  uint8_t     m_pitLimiterStatus;         // Pit limiter status - 0 = off, 1 = on
  float       m_fuelInTank;               // Current fuel mass
  float       m_fuelCapacity;             // Fuel capacity
  float       m_fuelRemainingLaps;        // Fuel remaining in terms of laps (value on MFD)
  uint16_t    m_maxRPM;                   // Cars max RPM, point of rev limiter
  uint16_t    m_idleRPM;                  // Cars idle RPM
  uint8_t     m_maxGears;                 // Maximum number of gears
  uint8_t     m_drsAllowed;               // 0 = not allowed, 1 = allowed, -1 = unknown
  uint16_t    m_drsActivationDistance;    // 0 = DRS not available, non-zero - DRS will be available
                                          // in [X] metres
  uint8_t     m_tyresWear[4];             // Tyre wear percentage
  uint8_t     m_actualTyreCompound;       // F1 Modern - 16 = C5, 17 = C4, 18 = C3, 19 = C2, 20 = C1
                                          // 7 = inter, 8 = wet
                                          // F1 Classic - 9 = dry, 10 = wet
                                          // F2 – 11 = super soft, 12 = soft, 13 = medium, 14 = hard
                                          // 15 = wet
  uint8_t     m_tyreVisualCompound;       // F1 visual (can be different from actual compound)
                                          // 16 = soft, 17 = medium, 18 = hard, 7 = inter, 8 = wet
                                          // F1 Classic – same as above
                                          // F2 – 19 = super soft, 20 = soft, 21 = medium, 22 = hard
                                          // 15 = wet
  uint8_t     m_tyresAgeLaps;             // Age in laps of the current set of tyres
  uint8_t     m_tyresDamage[4];           // Tyre damage (percentage)
  uint8_t     m_frontLeftWingDamage;      // Front left wing damage (percentage)
  uint8_t     m_frontRightWingDamage;     // Front right wing damage (percentage)
  uint8_t     m_rearWingDamage;           // Rear wing damage (percentage)
  uint8_t     m_drsFault;                 // Indicator for DRS fault, 0 = OK, 1 = fault
  uint8_t     m_engineDamage;             // Engine damage (percentage)
  uint8_t     m_gearBoxDamage;            // Gear box damage (percentage)
  int8_t      m_vehicleFiaFlags;          // -1 = invalid/unknown, 0 = none, 1 = green
                                          // 2 = blue, 3 = yellow, 4 = red
  float       m_ersStoreEnergy;           // ERS energy store in Joules
  uint8_t     m_ersDeployMode;            // ERS deployment mode, 0 = none, 1 = medium
                                          // 2 = overtake, 3 = hotlap
  float       m_ersHarvestedThisLapMGUK;  // ERS energy harvested this lap by MGU-K
  float       m_ersHarvestedThisLapMGUH;  // ERS energy harvested this lap by MGU-H
  float       m_ersDeployedThisLap;       // ERS energy deployed this lap
};

struct PacketCarStatusData
{
  PacketHeader     m_header;              // Header

  CarStatusData    m_carStatusData[22];
};
#pragma pack(pop)

// buffer for receiving data
uint8_t packetBuffer[UDP_TX_PACKET_MAX_SIZE]; //buffer to hold incoming packet,

WiFiUDP Udp;

void scrollText(const char *p, size_t delay_ms)
{
  uint8_t charWidth;
  uint8_t cBuf[8];

  mx.clear();

  while (*p != '\0')
  {
    charWidth = mx.getChar(*p++, sizeof(cBuf), cBuf);

    for (uint8_t i = 0; i <= charWidth; i++)	// allow space between characters
    {
      mx.transform(MD_MAX72XX::TSL);
      if (i < charWidth)
        mx.setColumn(0, cBuf[i]);
      delay(delay_ms);
    }
  }
}

// Draw speed on right side of LED matrix
void draw_speed(uint16_t speed)
{
  uint8_t charWidth;
  uint8_t cBuf[8];
  char speed_cstr[8];

  int speed_len = snprintf(speed_cstr, sizeof(speed_cstr), "%d", speed);
  int cols = 0;
  while (speed_len > 0) {
    speed_len--;
    charWidth = mx.getChar(speed_cstr[speed_len], sizeof(cBuf), cBuf);
    cols += charWidth;
    mx.setBuffer(cols, charWidth, cBuf);
    cols++; // Add inter-character spacing
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

// Draw gear on left side of LED matrix
void draw_gear(const char *p)
{
  uint8_t charWidth;
  int col = 31;
  while (*p != '\0')
  {
    charWidth = mx.setChar(col, *p++);
    col -= charWidth + 1;
  }
}

void setup() {
  WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP
  Serial.begin(115200);
  // Init 16 RGB LEDs
  strip.Begin();
  strip.Show();
  // Init 4 8x8 LED matrix
  mx.begin();

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
  res = wm.autoConnect("F1 2020 LEDs");
  // res = wm.autoConnect("AutoConnectAP","password"); // password protected ap

  if(!res) {
    Serial.println("Failed to connect");
    ESP.restart();
  }
  else {
    //if you get here you have connected to the WiFi
    Serial.print("Connected! IP address: ");
    Serial.println(WiFi.localIP());
    Serial.printf("UDP server on port %d\n", F1_UDP_PORT);
    Udp.begin(F1_UDP_PORT);
    // Display my IP address.
    scrollText(WiFi.localIP().toString().c_str(), 50);
  }
}

// This determines the LED colors.
const RgbColor RPM_COLORS[16] = {
  green, green, green, green, green,
  red, red, red, red, red,
  blue, blue, blue, blue, blue, white
};

void draw_rev(int rev_light_percent)
{
  static int last_stop = -1;    // Update LEDs only when needed.
  int stop = (((rev_light_percent * PixelCount * 10) / 100) + 5) / 10;
  if (stop != last_stop) {
    last_stop = stop;
    for (int i = 0; i < PixelCount; i++) {
      if (i < stop) {
        strip.SetPixelColor(i, RPM_COLORS[i]);
      }
      else {
        strip.SetPixelColor(i, black);
      }
    }
    strip.Show();
  }
}

void loop() {
  // if there's data available, read a packet
  int packetSize = Udp.parsePacket();
  if (packetSize > 0) {
    // read the packet into packetBufffer
    int packet_length = Udp.read(packetBuffer, sizeof(packetBuffer));
    if (packet_length >= sizeof(PacketHeader)) {
      struct PacketHeader *header = (struct PacketHeader *)&packetBuffer;
      if (header->m_packetFormat == 2020) {
        uint8_t myCar = header->m_playerCarIndex;
        switch (header->m_packetId) {
          case 6:
            {
              struct PacketCarTelemetryData *p;
              p = (struct PacketCarTelemetryData *)packetBuffer;
              struct CarTelemetryData *telemetry = &(p->m_carTelemetryData[myCar]);
#if 0
              Serial.printf("Speed = %u RPM %u Gear %d Rev %% %u\n", \
                  telemetry->m_speed, telemetry->m_engineRPM, \
                  telemetry->m_gear, telemetry->m_revLightsPercent);
#endif
              draw_rev(telemetry->m_revLightsPercent);

              uint16_t speed_kph = telemetry->m_speed;
              static uint16_t last_speed_kph = 10000;
              uint8_t currentGear = telemetry->m_gear & 0x0F;
              static uint8_t last_gear = 255;
              if ((speed_kph != last_speed_kph) ||
                  (currentGear != last_gear)) {
                mx.clear();
                draw_speed(speed_kph);
                draw_gear(GEAR_NAMES[currentGear]);
                last_speed_kph = speed_kph;
                last_gear = currentGear;
              }
            }
            break;
          case 7:
            {
              struct PacketCarStatusData *p;
              p = (struct PacketCarStatusData *)packetBuffer;
              struct CarStatusData *status = &(p->m_carStatusData[myCar]);
#if 0
              Serial.printf("Fuel Mix = %u Front Brake Bias %u\n", \
                  status->m_fuelMix, status->m_frontBrakeBias);
#endif
            }
            break;
          default:
            break;
        }
      }
    }
  }
}
