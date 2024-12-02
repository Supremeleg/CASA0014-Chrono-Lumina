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
const float fastMotionThreshold = 0.3; // Lower threshold for fast motion
const float slowMotionThreshold = 0.5; // Lower threshold for slow motion

// Sliding average filter
const int filterSize = 5;
float accXFilter[filterSize] = {0}, accYFilter[filterSize] = {0}, accZFilter[filterSize] = {0};
int filterIndex = 0;

// Interval for printing "no motion" messages
unsigned long lastNoMovementTime = 0;
const unsigned long noMovementInterval = 1000; // Check for no motion every 1 second

// Cooldown time
unsigned long lastMovementTime = 0;
const unsigned long movementCooldown = 500; // Reduce cooldown to 0.5 seconds

//------------------------------------------------------------------------------------------------------------------------------------------------
// Function declarations
void reconnectMQTT();
void setupWiFi();
void readAccelData();
void applyFilter();
void calibrateOffsets();
void controlLight(int lightID, int R, int G, int B, int W);
void handleMotion();

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

    // Convert to g values and apply filter
    applyFilter();
    accXg = (accXFilter[filterIndex] - offsetX) / 16384.0;
    accYg = (accYFilter[filterIndex] - offsetY) / 16384.0;
    accZg = (accZFilter[filterIndex] - offsetZ) / 16384.0;

    // Handle motion detection and control light
    handleMotion();

    delay(100); // Reduce delay for faster response
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

// Sliding average filter
void applyFilter() {
    accXFilter[filterIndex] = accX;
    accYFilter[filterIndex] = accY;
    accZFilter[filterIndex] = accZ;

    filterIndex = (filterIndex + 1) % filterSize;

    float sumX = 0, sumY = 0, sumZ = 0;
    for (int i = 0; i < filterSize; i++) {
        sumX += accXFilter[i];
        sumY += accYFilter[i];
        sumZ += accZFilter[i];
    }

    accXFilter[filterIndex] = sumX / filterSize;
    accYFilter[filterIndex] = sumY / filterSize;
    accZFilter[filterIndex] = sumZ / filterSize;
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

//------------------------------------------------------------------------------------------------------------------------------------------------
// Control a single light (all pixels of the light)
void controlLight(int lightID, int R, int G, int B, int W) {
    sprintf(mqtt_topic_demo, "student/CASA0014/light/%d/pixel/", lightID);
    for (int pixelID = 0; pixelID < 12; pixelID++) {
        char mqtt_message[100];
        sprintf(mqtt_message, "{\"pixelid\": %d, \"R\": %d, \"G\": %d, \"B\": %d, \"W\": %d}", pixelID, R, G, B, W);
        client.publish(mqtt_topic_demo, mqtt_message, false);
    }
}

// Handle motion detection logic
void handleMotion() {
    unsigned long currentTime = millis();

    // Determine motion direction
    float absX = fabs(accXg);
    float absY = fabs(accYg);
    float absZ = fabs(accZg);

    if (absX > fastMotionThreshold && absX > absY && absX > absZ) {
        Serial.println("X direction motion detected, light set to red");
        controlLight(2, 255, 0, 0, 0); // Red light
    } else if (absY > fastMotionThreshold && absY > absX && absY > absZ) {
        Serial.println("Y direction motion detected, light set to green");
        controlLight(2, 0, 255, 0, 0); // Green light
    } else if (absZ > fastMotionThreshold && absZ > absX && absZ > absY) {
        Serial.println("Z direction motion detected, light set to blue");
        controlLight(2, 0, 0, 255, 0); // Blue light
    } else {
        if (currentTime - lastNoMovementTime > noMovementInterval) {
            lastNoMovementTime = currentTime;
            Serial.println("No motion detected, light set to white");
            controlLight(2, 0, 0, 0, 50); // White light
        }
    }
}
