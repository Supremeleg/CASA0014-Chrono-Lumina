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
const float fastMotionThreshold = 0.4; // Threshold for fast motion
const float slowMotionThreshold = 0.5; // Threshold for slow motion

// Sliding average filter
const int filterSize = 5;
float accXFilter[filterSize] = {0}, accYFilter[filterSize] = {0}, accZFilter[filterSize] = {0};
int filterIndex = 0;

// Interval for printing "no motion" messages
unsigned long lastNoMovementTime = 0;
unsigned long lastNoMovementPrintTime = 0; // Reduce printing frequency for "no motion"
const unsigned long noMovementInterval = 2000; // Check for no motion every 2 seconds
const unsigned long noMovementPrintInterval = 5000; // Print "no motion" every 5 seconds

// Cooldown time
unsigned long lastMovementTime = 0;
const unsigned long movementCooldown = 2000; // Only process motion every 5 seconds

// Wave animation status
bool isWaveRunning = false;

// Record the last motion direction and magnitude
String lastDirection = "";
String primaryDirection = ""; // Current main motion direction

//------------------------------------------------------------------------------------------------------------------------------------------------
// Function declarations
void reconnectMQTT();
void setupWiFi();
void readAccelData();
void applyFilter();
void calibrateOffsets();
void controlAllLights(bool turnOn, bool weakWhite = false);
void controlWaveLights(String direction, String colorScheme);
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

    // Determine main motion direction
    float absX = fabs(accXg);
    float absY = fabs(accYg);
    float absZ = fabs(accZg);

    if (absX > fastMotionThreshold && absX > absY && absX > absZ) {
        primaryDirection = accXg > 0 ? "X" : "-X";
    } else if (absY > fastMotionThreshold && absY > absX && absY > absZ) {
        primaryDirection = accYg > 0 ? "Y" : "-Y";
    } else if (absZ > fastMotionThreshold && absZ > absX && absZ > absY) {
        primaryDirection = accZg > 0 ? "Z" : "-Z";
    } else {
        primaryDirection = "";
    }

    handleMotion();

    delay(200); // Delay for better data observation
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
// Control all lights
void controlAllLights(bool turnOn, bool weakWhite) {
    for (int i = 1; i <= 52; i++) {
        sprintf(mqtt_topic_demo, "student/CASA0014/light/%d/pixel/", i);
        for (int j = 0; j < 12; j++) {
            char mqtt_message[100];
            if (turnOn) {
                if (weakWhite) {
                    sprintf(mqtt_message, "{\"pixelid\": %d, \"R\": 0, \"G\": 0, \"B\": 0, \"W\": 50}", j);
                }
            } else {
                sprintf(mqtt_message, "{\"pixelid\": %d, \"R\": 0, \"G\": 0, \"B\": 0, \"W\": 0}", j);
            }
            client.publish(mqtt_topic_demo, mqtt_message, false);
        }
    }
}

// Control wave light effect
void controlWaveLights(String direction, String colorScheme) {
    if (direction == "horizontal") {
        for (int col = 1; col <= 14; col++) {
            for (int i = 1; i <= 52; i++) {
                sprintf(mqtt_topic_demo, "student/CASA0014/light/%d/pixel/", i);
                for (int j = 0; j < 12; j++) {
                    char mqtt_message[100];
                    if (colorScheme == "blue-purple") {
                        sprintf(mqtt_message, "{\"pixelid\": %d, \"R\": 0, \"G\": 0, \"B\": 255, \"W\": 50}", j);
                    }
                    client.publish(mqtt_topic_demo, mqtt_message, false);
                }
            }
            delay(2000); // Keep each column lit for 2 seconds
        }
    } else if (direction == "vertical") {
        for (int row = 1; row <= 10; row++) {
            for (int i = 1; i <= 52; i++) {
                sprintf(mqtt_topic_demo, "student/CASA0014/light/%d/pixel/", i);
                for (int j = 0; j < 12; j++) {
                    char mqtt_message[100];
                    if (colorScheme == "yellow-green") {
                        sprintf(mqtt_message, "{\"pixelid\": %d, \"R\": 255, \"G\": 255, \"B\": 0, \"W\": 50}", j);
                    }
                    client.publish(mqtt_topic_demo, mqtt_message, false);
                }
            }
            delay(2000); // Keep each row lit for 2 seconds
        }
    }
}

// Handle motion detection logic
void handleMotion() {
    unsigned long currentTime = millis();

    if (isWaveRunning) {
        return; // If wave effect is running, ignore new motion
    }

    if (!primaryDirection.isEmpty() && primaryDirection.startsWith("X")) {
        isWaveRunning = true;
        Serial.println("Detected fast motion in X direction, triggering horizontal wave");
        controlWaveLights("horizontal", "blue-purple");
        isWaveRunning = false;
    } else if (!primaryDirection.isEmpty() && primaryDirection.startsWith("Y")) {
        isWaveRunning = true;
        Serial.println("Detected fast motion in Y direction, triggering vertical wave");
        controlWaveLights("vertical", "yellow-green");
        isWaveRunning = false;
    } else if (primaryDirection.isEmpty()) {
        if (currentTime - lastNoMovementTime > noMovementInterval) {
            lastNoMovementTime = currentTime;

            // Print "no motion detected" message (reduce frequency)
            if (currentTime - lastNoMovementPrintTime > noMovementPrintInterval) {
                Serial.println("No motion detected, all lights set to weak white");
                lastNoMovementPrintTime = currentTime;
            }

            controlAllLights(true, true); // Set lights to weak white
        }
    }
}

