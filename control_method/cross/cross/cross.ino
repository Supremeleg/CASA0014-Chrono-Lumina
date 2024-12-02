#include <Wire.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include "arduino_secrets.h"

//------------------------------------------------------------------------------------------------------------------------------------------------
// WiFi and MQTT settings
const char* ssid          = SECRET_SSID;
const char* password      = SECRET_PASS;
const char* mqtt_username = SECRET_MQTTUSER;
const char* mqtt_password = SECRET_MQTTPASS;
const char* mqtt_server   = "mqtt.cetools.org";
const int mqtt_port       = 1884;

WiFiClient espClient;
PubSubClient client(espClient);

char mqtt_topic_demo[50];

const uint8_t MPU_ADDR = 0x68; // MPU6050 I2C address
int16_t accX, accY, accZ; // Raw acceleration data
float offsetX = 0, offsetY = 0, offsetZ = 0; // Calibration offset
float accXg, accYg, accZg; // Converted acceleration data
const float fastMotionThreshold = 0.3; // Threshold for fast motion

unsigned long lastMotionTime = 0;
const unsigned long motionDuration = 3000; // Duration to keep lights on

//------------------------------------------------------------------------------------------------------------------------------------------------
// Function declarations
void reconnectMQTT();
void setupWiFi();
void readAccelData();
void calibrateOffsets();
void controlLight(int lightID, int R, int G, int B, int W);
void clearAllLights();
void handleMotion(String direction);

//------------------------------------------------------------------------------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(1000);

    setupWiFi();
    client.setServer(mqtt_server, mqtt_port);

    // Initialize MPU6050
    Wire.begin();
    Wire.beginTransmission(MPU_ADDR);
    Wire.write(0x6B); // Power management register
    Wire.write(0);    // Wake up MPU6050
    Wire.endTransmission(true);

    // Initial calibration
    calibrateOffsets();
    Serial.println("Setup complete");
}

//------------------------------------------------------------------------------------------------------------------------------------------------
void loop() {
    if (!client.connected()) {
        reconnectMQTT();
    }
    if (WiFi.status() != WL_CONNECTED) {
        setupWiFi();
    }
    client.loop();

    // Read acceleration data
    readAccelData();

    // Convert to g values
    accXg = (accX - offsetX) / 16384.0;
    accYg = (accY - offsetY) / 16384.0;
    accZg = (accZ - offsetZ) / 16384.0;

    // Detect motion direction
    float absX = fabs(accXg);
    float absY = fabs(accYg);
    if (absX > fastMotionThreshold && absX > absY) {
        handleMotion(accXg > 0 ? "X" : "-X");
    } else if (absY > fastMotionThreshold && absY > absX) {
        handleMotion(accYg > 0 ? "Y" : "-Y");
    }
}

//------------------------------------------------------------------------------------------------------------------------------------------------
// WiFi connection function
void setupWiFi() {
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("WiFi connected");
    Serial.println("IP Address: ");
    Serial.println(WiFi.localIP());
}

// MQTT connection function
void reconnectMQTT() {
    while (!client.connected()) {
        Serial.print("Attempting MQTT connection...");
        String clientId = "ESP32Client-" + String(random(0xffff), HEX);
        if (client.connect(clientId.c_str(), mqtt_username, mqtt_password)) {
            Serial.println("MQTT connected");
        } else {
            Serial.print("failed, rc=");
            Serial.print(client.state());
            Serial.println(" try again in 5 seconds");
            delay(5000);
        }
    }
}

// Read acceleration data
void readAccelData() {
    Wire.beginTransmission(MPU_ADDR);
    Wire.write(0x3B); // Starting address for acceleration data
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)MPU_ADDR, (size_t)6, (bool)true); // Request 6 bytes of data

    accX = (Wire.read() << 8 | Wire.read());
    accY = (Wire.read() << 8 | Wire.read());
    accZ = (Wire.read() << 8 | Wire.read());
}

// Calibration function
void calibrateOffsets() {
    const int sampleCount = 100;
    long sumX = 0, sumY = 0, sumZ = 0;

    for (int i = 0; i < sampleCount; i++) {
        readAccelData();
        sumX += accX;
        sumY += accY;
        sumZ += accZ;
        delay(10);
    }

    offsetX = sumX / sampleCount;
    offsetY = sumY / sampleCount;
    offsetZ = sumZ / sampleCount;

    Serial.println("Calibration complete!");
}

// Control a single light
void controlLight(int lightID, int R, int G, int B, int W) {
    sprintf(mqtt_topic_demo, "student/CASA0014/light/%d/pixel/", lightID);
    for (int pixelID = 0; pixelID < 12; pixelID++) {
        char mqtt_message[100];
        sprintf(mqtt_message, "{\"pixelid\": %d, \"R\": %d, \"G\": %d, \"B\": %d, \"W\": %d}", pixelID, R, G, B, W);
        client.publish(mqtt_topic_demo, mqtt_message, false);
    }
}

// Clear all lights
void clearAllLights() {
    for (int i = 1; i <= 52; i++) {
        controlLight(i, 0, 0, 0, 0);
    }
}

// Handle motion detection and control lights
void handleMotion(String direction) {
    clearAllLights(); // Clear previous lights

    if (direction == "X") {
        Serial.println("X motion detected: Blue to Purple");
        int lights[] = {2, 7, 15, 23, 33, 42, 44};
        for (int i = 0; i < 7; i++) {
            controlLight(lights[i], 0, 0, 255 - i * 30, 50 + i * 20); // Blue to Purple gradient
            delay(200); // Delay between lights
        }
    } else if (direction == "-X") {
        Serial.println("-X motion detected: Yellow to Green");
        int lights[] = {2, 7, 15, 23, 33, 42, 44};
        for (int i = 0; i < 7; i++) {
            controlLight(lights[i], 255 - i * 30, 255, 0, 0); // Yellow to Green gradient
            delay(200); // Delay between lights
        }
    } else if (direction == "Y") {
        Serial.println("Y motion detected: Green to Pink");
        int lights[] = {26, 27, 28, 29, 30};
        for (int i = 0; i < 5; i++) {
            controlLight(lights[i], 0, 255 - i * 50, 255 - i * 30, 0); // Green to Pink gradient
            delay(300); // Delay between lights
        }
    } else if (direction == "-Y") {
        Serial.println("-Y motion detected: Yellow to Purple");
        int lights[] = {26, 27, 28, 29, 30};
        for (int i = 0; i < 5; i++) {
            controlLight(lights[i], 255 - i * 30, 255 - i * 50, i * 50, 0); // Yellow to Purple gradient
            delay(300); // Delay between lights
        }
    }

    delay(motionDuration); // Keep lights on for the specified duration
    clearAllLights(); // Clear lights after duration
}

