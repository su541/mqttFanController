#include <Arduino.h>

#include <FS.h>
#include <ArduinoJson.h>

// Wifi
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>

// OTA
#include <ArduinoOTA.h>

// MQTT
#include <PubSubClient.h>

// Status
#include <Ticker.h>

// Button
#include <JC_Button.h>

// Display

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Fonts/FreeSans18pt7b.h>
#include <Adafruit_SSD1306.h>

#include "images.h"

// Values
#include <map>

// Configuration
#include "config.h"

// Definition
#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels

#define LONG_PRESS 1000
#define BUTTON_PIN D7

#define LED_UPDATE_INTERVAL 600
#define HEADER_UPDATE_INTERVAL 5000
#define ANIM_UPDATE_INTERVAL 100
#define TEXT_UPDATE_INTERVAL 500

#define TEXT_LENGTH 6

// Enums
enum HEADER
{
  NAME,
  IP
};

enum STATES
{
  OFF,
  ON
};

Button controlButton(BUTTON_PIN);
bool ignoreButton = false;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire);

Config configuration;
const char *configFilename = "/config.jsn";

// State
bool shouldSaveConfig = false;
int displayData = -1;
float remainingDisplayTime;
unsigned int lineLength = 128;

STATES fanState = OFF;
STATES displayState = ON;
HEADER headerState = NAME;

boolean scrollLeft = true;

int signalStrength = 0;
unsigned int frame = 0;
unsigned int textPos = 0;
String deviceName;
double deviceTemp = 0;

Ticker ledTicker;
Ticker wifiTicker;
Ticker animTicker;
Ticker textTicker;
Ticker displayTimeTicker;

WiFiClient espClient;
PubSubClient client(espClient);

auto mqttData = std::map<String, double>(); // myMap;
auto keys = std::vector<String>();

void updateLedStatus()
{
  int state = digitalRead(LED_BUILTIN); // get the current state of GPIO1 pin
  digitalWrite(LED_BUILTIN, !state);    // set pin to the opposite state
}

void updateAnimationData()
{
  frame == 2 ? frame = 0 : frame++;
  if(displayState==ON)
  {
      remainingDisplayTime=remainingDisplayTime - 100;
      float value = (remainingDisplayTime / (configuration.displayTime * 1000) ) * 128;
      lineLength = round(value);
  }
}

void updateHeaderInformation()
{
  headerState = headerState == NAME ? IP : NAME;
  int32_t RSSI = WiFi.RSSI();

  if (RSSI < -82)
  {
    signalStrength = 0;
  }
  else if (RSSI < -78)
  {
    signalStrength = 1;
  }
  else if (RSSI < -70)
  {
    signalStrength = 2;
  }
  else if (RSSI < -65)
  {
    signalStrength = 3;
  }
  else if (RSSI < -55)
  {
    signalStrength = 4;
  }
  else
  {
    signalStrength = 5;
  }
}

void updateDeviceInformation()
{
  String text = displayData != -1 ? String(keys.at(displayData)) : "TEMP";

  if (displayData != -1)
  {
    deviceTemp = mqttData.find(text)->second;

    if (text.length() < TEXT_LENGTH)
    {
      textPos = 0;
    }
    else
    {
      if (scrollLeft)
      {
        textPos++;
      }
      else
      {
        textPos--;
      }

      if (textPos == 0)
      {
        scrollLeft = true;
        displayData++;
        if (mqttData.size() == displayData)
        {
          displayData = 0;
        }
      }
      if (textPos == text.length() - TEXT_LENGTH)
      {
        scrollLeft = false;
      }
    }

    deviceName = text.substring(textPos, textPos + TEXT_LENGTH);
  }
}

void loadConfig()
{
  File file = SPIFFS.open(configFilename, "r");
  StaticJsonDocument<512> doc;

  DeserializationError error = deserializeJson(doc, file);
  if (error)
    Serial.println(F("Failed to read file, using default configuration"));

  strcpy(configuration.mqtt_server, doc[MQTT_SERVER] | "mqtt.cluster.fritz.box");

  strcpy(configuration.subscriptionTopic, doc[MQTT_TOPIC] | "su541cluster/+/cpu_temp");
  strcpy(configuration.mqtt_auth_user, doc[MQTT_USER] | "");
  strcpy(configuration.mqtt_auth_pass, doc[MQTT_PASS] | "");
  configuration.displayTime = doc[DISPLAY_TIME] | 60;
  configuration.fanStartTemp = doc[FAN_START_TEMP] | 50;
  configuration.fanStopTemp = doc[FAN_STOP_TEMP] | 44;

  file.close();
}

void saveConfig()
{
  SPIFFS.remove(configFilename);

  // Open file for writing
  File file = SPIFFS.open(configFilename, "w");
  if (!file)
  {
    Serial.println(F("Failed to create file"));
    return;
  }

  StaticJsonDocument<512> doc;

  doc[MQTT_SERVER] = configuration.mqtt_server;
  doc[MQTT_TOPIC] = configuration.subscriptionTopic;
  doc[MQTT_USER] = configuration.mqtt_auth_user;
  doc[MQTT_PASS] = configuration.mqtt_auth_pass;
  doc[DISPLAY_TIME] = configuration.displayTime;
  doc[FAN_START_TEMP] = configuration.fanStartTemp;
  doc[FAN_STOP_TEMP] = configuration.fanStopTemp;

  if (serializeJson(doc, file) == 0)
  {
    Serial.println(F("Failed to write to file"));
  }

  // Close the file
  file.close();
}

// Callbacks
void configModeCallback(WiFiManager *myWiFiManager)
{
  Serial.println(WiFi.softAPIP());
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(10, 8);
  display.print("Configuration-Mode");
  display.setCursor(0, 23);
  display.print("SSID: " + myWiFiManager->getConfigPortalSSID());
  display.setCursor(0, 35);
  display.print("IP:   192.168.1.1");
  display.display();
  ledTicker.attach_ms(200, updateLedStatus);
}

void mqttCallback(char *topic, byte *payload, unsigned int length)
{
  char value[length + 1];

  unsigned int i;
  for (i = 0; i < length; i++)
  {
    value[i] = (char)payload[i];
  }
  value[i] = 0;

  mqttData[String(topic)] = atof(value);
  keys.push_back(String(topic));
  if (displayData == -1)
    displayData++;

  double currentMax = 0;
  for (auto it = mqttData.cbegin(); it != mqttData.cend(); ++it)
  {
    if (it->second > currentMax)
    {
      currentMax = it->second;
    }
  }

  if (currentMax > configuration.fanStartTemp)
  {
    fanState = ON;
  }
  else
  {
    fanState = OFF;
  }
}

void saveConfigCallback()
{
  shouldSaveConfig = true;
}

// OverTheAir - Update Initialization
void initOTA()
{
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
      type = "sketch";
    else // U_SPIFFS
      type = "filesystem";

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR)
      Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR)
      Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR)
      Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR)
      Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR)
      Serial.println("End Failed");
  });
  ArduinoOTA.begin();
}

void updateDisplayState()
{
  displayState = OFF;
}

void updateDisplayWifi()
{
  for (int b = 0; b <= signalStrength; b++)
  {
    display.fillRect(119 + (b * 2), 10 - (b * 2), 1, 2 + (b * 2), SSD1306_WHITE);
  }

  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  display.setCursor(0, 3);

  if (headerState == NAME)
  {
    display.print("FanControl");
  }
  else
  {
    display.print(WiFi.localIP());
  }
}

void updateDisplayForMQTT()
{
  int topics = mqttData.size();
  display.setCursor(5, 22);
  display.fillRect(3, 20, 22, 12, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.print("DEV");
  display.setTextColor(SSD1306_WHITE);
  display.setFont(&FreeSans18pt7b);
  display.setCursor(3, 60);
  display.print(topics);

  display.setFont();
  display.setCursor(37, 22);
  display.fillRect(35, 20, 36, 12, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);

  display.setTextWrap(false);

  display.print(deviceName);
  display.setTextColor(SSD1306_WHITE);
  display.setFont(&FreeSans18pt7b);
  display.setCursor(35, 60);

  display.print(String((int)round(deviceTemp)));

  display.setFont();
}

void updateDisplayFanAnimation()
{
  if (fanState == ON)
  {
    if (frame == 0)
    {
      display.drawBitmap(80, 15, FAN_1, 48, 48, SSD1306_WHITE);
    }
    if (frame == 1)
    {
      display.drawBitmap(80, 15, FAN_2, 48, 48, SSD1306_WHITE);
    }
    if (frame == 2)
    {
      display.drawBitmap(80, 15, FAN_3, 48, 48, SSD1306_WHITE);
    }
  }
  else
  {
    display.drawBitmap(80, 15, FAN_1, 48, 48, SSD1306_WHITE);
  }
  display.drawLine(0,63,lineLength,63,SSD1306_WHITE);
}

void setup()
{
  Serial.begin(115200);
  WiFiManager wifiManager;
  wifiManager.setDebugOutput(false);

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();

  controlButton.begin();
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  if (digitalRead(BUTTON_PIN) == LOW)
  {
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);

    display.setCursor(10, 9);
    display.print("RESET in 10 SECONDS");
    display.display();
    delay(10000);
    wifiManager.resetSettings();
    delay(500);
    ESP.reset();
  }

  display.drawBitmap(0, 0, SU541_LOGO, 126, 64, SSD1306_WHITE);
  display.display();
  delay(2000); // Pause for 2 seconds

  while (!SPIFFS.begin())
  {
    Serial.println(F("Failed to initialize SD library"));
    delay(1000);
  }

  pinMode(LED_BUILTIN, OUTPUT);

  ledTicker.attach_ms(LED_UPDATE_INTERVAL, updateLedStatus);

  loadConfig();

  WiFiManagerParameter custom_mqtt_server("server", "mqtt server", configuration.mqtt_server, 40);
  WiFiManagerParameter custom_mqtt_topic("topic", "topic", configuration.subscriptionTopic, 100, "readonly");
  WiFiManagerParameter custom_mqtt_auth_user("user", "username", configuration.mqtt_auth_user, 20);
  WiFiManagerParameter custom_mqtt_auth_pass("pass", "password", configuration.mqtt_auth_pass, 20);
  WiFiManagerParameter custom_display_time("displayTime", "Fan Start Temp", "60", 2);
  WiFiManagerParameter custom_fan_start("fanStart", "Fan Start Temp", "5", 5);
  WiFiManagerParameter custom_fan_stop("fanStop", "Fan Stop Temp", "44", 5);

  wifiManager.setAPStaticIPConfig(IPAddress(192, 168, 1, 1), IPAddress(192, 168, 1, 1), IPAddress(255, 255, 255, 0));

  wifiManager.setAPCallback(configModeCallback);
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_topic);
  wifiManager.addParameter(&custom_mqtt_auth_user);
  wifiManager.addParameter(&custom_mqtt_auth_pass);
  wifiManager.addParameter(&custom_display_time);
  wifiManager.addParameter(&custom_fan_start);
  wifiManager.addParameter(&custom_fan_stop);

  if (!wifiManager.autoConnect("su541FanControl"))
  {
    ESP.reset();
    delay(1000);
  }

  if (shouldSaveConfig)
  {
    strcpy(configuration.mqtt_server, custom_mqtt_server.getValue());
    strcpy(configuration.mqtt_auth_user, custom_mqtt_auth_user.getValue());
    strcpy(configuration.mqtt_auth_pass, custom_mqtt_auth_pass.getValue());
    configuration.displayTime = atoi(custom_display_time.getValue()) | 60;
    configuration.fanStartTemp = atof(custom_fan_start.getValue());
    configuration.fanStopTemp = atof(custom_fan_stop.getValue());
    saveConfig();
  }

  ledTicker.detach();

  client.setServer(configuration.mqtt_server, 1883);
  client.setCallback(mqttCallback);

  if (client.connect("fancontroller", configuration.mqtt_auth_user, configuration.mqtt_auth_pass))
  {
    client.subscribe(configuration.subscriptionTopic);
  }

  // LED OFF
  digitalWrite(LED_BUILTIN, HIGH);

  initOTA();

  // Init Timers
  wifiTicker.attach_ms(HEADER_UPDATE_INTERVAL, updateHeaderInformation);
  animTicker.attach_ms(ANIM_UPDATE_INTERVAL, updateAnimationData);
  textTicker.attach_ms(TEXT_UPDATE_INTERVAL, updateDeviceInformation);
  displayTimeTicker.once_ms(configuration.displayTime * 1000, updateDisplayState);
  remainingDisplayTime = configuration.displayTime * 1000;
}

void loop()
{
  display.clearDisplay();
  client.loop();
  ArduinoOTA.handle();

  if (displayState == ON)
  {
    updateDisplayFanAnimation();
    updateDisplayForMQTT();
    updateDisplayWifi();
  }

  display.display();
  controlButton.read();

  if (controlButton.pressedFor(LONG_PRESS))
  {
    Serial.println("LONG_PRESS");
    Serial.println(millis());
  }
  else if (controlButton.wasReleased())
  {
    if (displayState == OFF)
    {
      remainingDisplayTime = configuration.displayTime * 1000;
      displayState = ON;
      displayTimeTicker.once_ms(configuration.displayTime * 1000, updateDisplayState);
    }
    else
    {
      displayState = OFF;
      displayTimeTicker.detach();
    }
  }
}