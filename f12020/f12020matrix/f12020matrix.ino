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

#define DEBUG_PRINT (0)

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WebSocketsServer.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <WiFiManager.h>  // https://github.com/tzapu/WiFiManager
#include <NeoPixelBus.h>  // Install using IDE Library manager
#include <MD_MAX72xx.h>   // Install using IDE Library manager
#include <ArduinoJson.h>
#include "telemetry_html.h"

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

ESP8266WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

/* F1 2020 UDP API ****************************************************/
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

struct CarSetupData
{
  uint8_t   m_frontWing;                // Front wing aero
  uint8_t   m_rearWing;                 // Rear wing aero
  uint8_t   m_onThrottle;               // Differential adjustment on throttle (percentage)
  uint8_t   m_offThrottle;              // Differential adjustment off throttle (percentage)
  float     m_frontCamber;              // Front camber angle (suspension geometry)
  float     m_rearCamber;               // Rear camber angle (suspension geometry)
  float     m_frontToe;                 // Front toe angle (suspension geometry)
  float     m_rearToe;                  // Rear toe angle (suspension geometry)
  uint8_t   m_frontSuspension;          // Front suspension
  uint8_t   m_rearSuspension;           // Rear suspension
  uint8_t   m_frontAntiRollBar;         // Front anti-roll bar
  uint8_t   m_rearAntiRollBar;          // Front anti-roll bar
  uint8_t   m_frontSuspensionHeight;    // Front ride height
  uint8_t   m_rearSuspensionHeight;     // Rear ride height
  uint8_t   m_brakePressure;            // Brake pressure (percentage)
  uint8_t   m_brakeBias;                // Brake bias (percentage)
  float     m_rearLeftTyrePressure;     // Rear left tyre pressure (PSI)
  float     m_rearRightTyrePressure;    // Rear right tyre pressure (PSI)
  float     m_frontLeftTyrePressure;    // Front left tyre pressure (PSI)
  float     m_frontRightTyrePressure;   // Front right tyre pressure (PSI)
  uint8_t   m_ballast;                  // Ballast
  float     m_fuelLoad;                 // Fuel load
};

struct PacketCarSetupData
{
  PacketHeader    m_header;            // Header

  CarSetupData    m_carSetups[22];
};

struct CarMotionData
{
    float         m_worldPositionX;           // World space X position
    float         m_worldPositionY;           // World space Y position
    float         m_worldPositionZ;           // World space Z position
    float         m_worldVelocityX;           // Velocity in world space X
    float         m_worldVelocityY;           // Velocity in world space Y
    float         m_worldVelocityZ;           // Velocity in world space Z
    int16_t       m_worldForwardDirX;         // World space forward X direction (normalised)
    int16_t       m_worldForwardDirY;         // World space forward Y direction (normalised)
    int16_t       m_worldForwardDirZ;         // World space forward Z direction (normalised)
    int16_t       m_worldRightDirX;           // World space right X direction (normalised)
    int16_t       m_worldRightDirY;           // World space right Y direction (normalised)
    int16_t       m_worldRightDirZ;           // World space right Z direction (normalised)
    float         m_gForceLateral;            // Lateral G-Force component
    float         m_gForceLongitudinal;       // Longitudinal G-Force component
    float         m_gForceVertical;           // Vertical G-Force component
    float         m_yaw;                      // Yaw angle in radians
    float         m_pitch;                    // Pitch angle in radians
    float         m_roll;                     // Roll angle in radians
};

struct PacketMotionData
{
    PacketHeader    m_header;               	// Header

    CarMotionData   m_carMotionData[22];    	// Data for all cars on track

    // Extra player car ONLY data
    float         m_suspensionPosition[4];      // Note: All wheel arrays have the following order:
    float         m_suspensionVelocity[4];      // RL, RR, FL, FR
    float         m_suspensionAcceleration[4];	// RL, RR, FL, FR
    float         m_wheelSpeed[4];           	// Speed of each wheel
    float         m_wheelSlip[4];               // Slip ratio for each wheel
    float         m_localVelocityX;         	// Velocity in local space
    float         m_localVelocityY;         	// Velocity in local space
    float         m_localVelocityZ;         	// Velocity in local space
    float         m_angularVelocityX;		    // Angular velocity x-component
    float         m_angularVelocityY;           // Angular velocity y-component
    float         m_angularVelocityZ;           // Angular velocity z-component
    float         m_angularAccelerationX;       // Angular velocity x-component
    float         m_angularAccelerationY;	    // Angular velocity y-component
    float         m_angularAccelerationZ;       // Angular velocity z-component
    float         m_frontWheelsAngle;           // Current front wheels angle in radians
};
#pragma pack(pop)

// buffer for receiving data
uint8_t packetBuffer[4096]; //buffer to hold incoming packet,

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

// Fuel mix - 0 = lean, 1 = standard, 2 = rich, 3 = max
const char * const FUEL_MIX_NAMES[4] = {
  "Lean",
  "Standard",
  "Rich",
  "Max",
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

/* web and web socket server ***************************************/

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length)
{
  if (DEBUG_PRINT) Serial.printf("webSocketEvent(%d, %d, ...)\r\n", num, type);
  switch(type) {
    case WStype_DISCONNECTED:
      if (DEBUG_PRINT) Serial.printf("[%u] Disconnected!\r\n", num);
      break;
    case WStype_CONNECTED:
        if (DEBUG_PRINT) {
          IPAddress ip = webSocket.remoteIP(num);
          Serial.printf("[%u] Connected from %d.%d.%d.%d url: %s\r\n", num, ip[0], ip[1], ip[2], ip[3], payload);
        }
        // Send the last values.
        send_car_telemetry(NULL);
        send_car_status(NULL);
        send_car_setup(NULL);
        send_car_motion(NULL);
      break;
    case WStype_TEXT:
      {
        if (DEBUG_PRINT) Serial.printf("[%u] get Text: %s\r\n", num, payload);

#if 0
        // Maybe: Parse touch/mouse events for button presses/releases???
        StaticJsonDocument<64> doc;

        DeserializationError error = deserializeJson(doc, payload, length);

        if (error) {
          if (DEBUG_PRINT) {
            Serial.print(F("deserializeJson() failed: "));
            Serial.println(error.f_str());
          }
          return;
        }
#endif
      }
      break;
    case WStype_BIN:
      if (DEBUG_PRINT) {
        Serial.printf("[%u] get binary length: %u\r\n", num, length);
        hexdump(payload, length);
        // echo data back to browser
        webSocket.sendBIN(num, payload, length);
      }
      break;
    default:
      if (DEBUG_PRINT) Serial.printf("Invalid WStype [%d]\r\n", type);
      break;
  }
}

void handleRoot(void){
  server.send(200, "text/html", TELEMETRY_HTML);
}

void handleNotFound(void) {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET)?"GET":"POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i=0; i < server.args(); i++)
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  server.send(404, "text/plain", message);
}

void web_server_setup(void) {
  // Start web socket server
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  // Setup the root web page.
  server.on("/", handleRoot);
  // Set up an error page.
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("HTTP server started");
}

void web_server_loop(void) {
  webSocket.loop();
  server.handleClient();  // Handle any web activity
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
    // Display my IP address.
    scrollText(WiFi.localIP().toString().c_str(), 50);
    Udp.begin(F1_UDP_PORT);
    web_server_setup();
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

void send_car_status(struct CarStatusData *status) {
  static struct CarStatusData last_status;
  char json[256+1];
  int len;
  static const char json_format[] =
    R"====({"type":2,"front_brake_bias":%u,"fuel_mix":"%s","fuel_remaining_laps":%.2f})====";
  static const char json_format_ers[] =
    R"====({"type":3,"ers_store_energy":%.2f,"ers_deploy_mode":%u,"ers_deployed_this_lap":%.2f})====";
  if (status == NULL) {
    len = snprintf(json, sizeof(json), json_format, last_status.m_frontBrakeBias, \
      FUEL_MIX_NAMES[last_status.m_fuelMix & 0x3], last_status.m_fuelRemainingLaps);
      // Broadcast sends to all connected clients.
    if (len > 0) webSocket.broadcastTXT(json, len);
    len = snprintf(json, sizeof(json), json_format_ers, last_status.m_ersStoreEnergy, \
      last_status.m_ersDeployMode, last_status.m_ersDeployedThisLap);
    if (len > 0) webSocket.broadcastTXT(json, len);
  }
  else {
    if (memcmp(&last_status, status, sizeof(last_status))) {
      len = snprintf(json, sizeof(json), json_format, status->m_frontBrakeBias, \
          FUEL_MIX_NAMES[status->m_fuelMix & 0x03], status->m_fuelRemainingLaps);
      if (len > 0) webSocket.broadcastTXT(json, len);
      len = snprintf(json, sizeof(json), json_format_ers, status->m_ersStoreEnergy, \
        status->m_ersDeployMode, status->m_ersDeployedThisLap);
      if (len > 0) webSocket.broadcastTXT(json, len);
      memcpy(&last_status, status, sizeof(last_status));
    }
  }
}

void send_car_telemetry(struct CarTelemetryData *telemetry) {
  static struct CarTelemetryData last_telemetry;
  char json[256+1];
  int len;
  static const char json_format[] =
    R"====({"type":1,"speed":%u,"gear":"%s","rpm":%u,"drs":%u,"revLights":%u,"engineTemp":%u})====";
  static const char json_format_temp[] =
    R"====({"type":4,"brakes":"%u,%u,%u,%u","tyres_surface":"%u,%u,%u,%u","tyres_inner":"%u,%u,%u,%u"})====";

  if (telemetry == NULL) {
    telemetry = &last_telemetry;
    len = snprintf(json, sizeof(json), json_format,
        telemetry->m_speed, GEAR_NAMES[telemetry->m_gear & 0x0F],
        telemetry->m_engineRPM, telemetry->m_drs,
        telemetry->m_revLightsPercent, telemetry->m_engineTemperature);
    if (len > 0) webSocket.broadcastTXT(json, len);
    len = snprintf(json, sizeof(json), json_format_temp,
        telemetry->m_brakesTemperature[0], telemetry->m_brakesTemperature[1],
        telemetry->m_brakesTemperature[2], telemetry->m_brakesTemperature[3],
        telemetry->m_tyresSurfaceTemperature[0], telemetry->m_tyresSurfaceTemperature[1],
        telemetry->m_tyresSurfaceTemperature[2], telemetry->m_tyresSurfaceTemperature[3],
        telemetry->m_tyresInnerTemperature[0], telemetry->m_tyresInnerTemperature[1],
        telemetry->m_tyresInnerTemperature[2], telemetry->m_tyresInnerTemperature[3]);
    if (len > 0) webSocket.broadcastTXT(json, len);
  }
  else {
    if (memcmp(&last_telemetry, telemetry, sizeof(last_telemetry))) {
      uint16_t speed_kph = telemetry->m_speed;
      uint8_t currentGear = telemetry->m_gear & 0x0F;
      mx.clear();
      draw_speed(speed_kph);
      draw_gear(GEAR_NAMES[currentGear]);
      len = snprintf(json, sizeof(json), json_format,
          speed_kph, GEAR_NAMES[currentGear], telemetry->m_engineRPM,
          telemetry->m_drs, telemetry->m_revLightsPercent,
          telemetry->m_engineTemperature);
      // Broadcast sends to all connected clients.
      if (len > 0) webSocket.broadcastTXT(json, len);
      len = snprintf(json, sizeof(json), json_format_temp,
          telemetry->m_brakesTemperature[0], telemetry->m_brakesTemperature[1],
          telemetry->m_brakesTemperature[2], telemetry->m_brakesTemperature[3],
          telemetry->m_tyresSurfaceTemperature[0], telemetry->m_tyresSurfaceTemperature[1],
          telemetry->m_tyresSurfaceTemperature[2], telemetry->m_tyresSurfaceTemperature[3],
          telemetry->m_tyresInnerTemperature[0], telemetry->m_tyresInnerTemperature[1],
          telemetry->m_tyresInnerTemperature[2], telemetry->m_tyresInnerTemperature[3]);
      if (len > 0) webSocket.broadcastTXT(json, len);
      memcpy(&last_telemetry, telemetry, sizeof(last_telemetry));
    }
  }
}

void send_car_setup(struct CarSetupData *carsetup) {
  static struct CarSetupData last_carsetup;
  char json[256+1];
  int len;
  static const char json_format[] =
    R"====({"type":5,"diff_on_throttle":%u,"diff_off_throttle":"%u"})====";

  if (carsetup == NULL) {
    carsetup = &last_carsetup;
    len = snprintf(json, sizeof(json), json_format,
        carsetup->m_onThrottle, carsetup->m_offThrottle);
    if (len > 0) webSocket.broadcastTXT(json, len);
  }
  else {
    if (memcmp(&last_carsetup, carsetup, sizeof(last_carsetup))) {
      len = snprintf(json, sizeof(json), json_format,
        carsetup->m_onThrottle, carsetup->m_offThrottle);
      // Broadcast sends to all connected clients.
      if (len > 0) webSocket.broadcastTXT(json, len);
      memcpy(&last_carsetup, carsetup, sizeof(last_carsetup));
    }
  }
}

void send_car_motion(struct CarMotionData *carmotion) {
  static struct CarMotionData last_carmotion;
  char json[256+1];
  int len;
  static const char json_format[] =
    R"====({"type":6,"orientation":"%.2f,%.2f,%.2f"})====";

  if (carmotion == NULL) {
    carmotion = &last_carmotion;
    len = snprintf(json, sizeof(json), json_format,
        carmotion->m_roll, carmotion->m_pitch, carmotion->m_yaw);
    if (len > 0) webSocket.broadcastTXT(json, len);
  }
  else {
    if (memcmp(&last_carmotion, carmotion, sizeof(last_carmotion))) {
      len = snprintf(json, sizeof(json), json_format,
          carmotion->m_roll, carmotion->m_pitch, carmotion->m_yaw);
      // Broadcast sends to all connected clients.
      if (len > 0) webSocket.broadcastTXT(json, len);
      memcpy(&last_carmotion, carmotion, sizeof(last_carmotion));
    }
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
          case 0:
            {
              struct PacketMotionData *p;
              p = (struct PacketMotionData *)packetBuffer;
              struct CarMotionData *carmotion = &(p->m_carMotionData[myCar]);
#if DEBUG_PRINT
              Serial.printf("Roll = %f, Pitch = %f, Yaw = %f\n", \
                  carmotion->m_roll, carmotion->m_pitch, carmotion->m_yaw);
#endif
              send_car_motion(carmotion);
            }
            break;
          case 5:
            {
              struct PacketCarSetupData *p;
              p = (struct PacketCarSetupData *)packetBuffer;
              struct CarSetupData *carsetup = &(p->m_carSetups[myCar]);
#if DEBUG_PRINT
              Serial.printf("Diff on throttle = %u Diff off throttle %u\n", \
                  carsetup->m_onThrottle, carsetup->m_offThrottle);
#endif
              send_car_setup(carsetup);
            }
            break;
          case 6:
            {
              struct PacketCarTelemetryData *p;
              p = (struct PacketCarTelemetryData *)packetBuffer;
              struct CarTelemetryData *telemetry = &(p->m_carTelemetryData[myCar]);
#if DEBUG_PRINT
              Serial.printf("Speed = %u RPM %u Gear %d Rev %% %u\n", \
                  telemetry->m_speed, telemetry->m_engineRPM, \
                  telemetry->m_gear, telemetry->m_revLightsPercent);
#endif
              draw_rev(telemetry->m_revLightsPercent);

              send_car_telemetry(telemetry);
            }
            break;
          case 7:
            {
              struct PacketCarStatusData *p;
              p = (struct PacketCarStatusData *)packetBuffer;
              struct CarStatusData *status = &(p->m_carStatusData[myCar]);
#if DEBUG_PRINT
              Serial.printf("Fuel Mix = %u Front Brake Bias %u\n", \
                  status->m_fuelMix, status->m_frontBrakeBias);
#endif
              send_car_status(status);
            }
            break;
          default:
            break;
        }
      }
    }
  }

  web_server_loop();
}
