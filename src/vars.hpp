#pragma once

#include <Ticker.h>
#include <Adafruit_MPU6050.h>

#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>

#define RELAY 18
#define SPEAKER 26

// --- Speaker
volatile bool speakerState = true;
Ticker speakerTimer;
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

// Accelerometer
#define CRASH_THRESHOLD (3 * 9.8)

Ticker resetCrashTimer;
volatile bool crashDetected = false;
unsigned long lastCrashTime = 0;
const unsigned long CRASH_COOLDOWN_MS = 5000;

Adafruit_MPU6050 mpu;

// Bluetooth
#define SERVICE_UUID "480ffc7b-9790-4753-a322-2c054412539a"
#define CHARACTERISTIC_UUID "4a41a0dd-212a-4b1f-a7ab-09f0f564fd7c"

BLECharacteristic *pCharacteristic;
bool deviceConnected = false;

class ServerCallbacks : public BLEServerCallbacks
{
  void onConnect(BLEServer *s)
  {
    deviceConnected = true;
  }
  void onDisconnect(BLEServer *s)
  {
    deviceConnected = false;
    // Restart advertising so the app can reconnect
    BLEDevice::getAdvertising()->start();
    Serial.println("Client disconnected — advertising restarted");
  }
};

void setupBLE()
{
  BLEDevice::init("ESP-MOTOR");
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  BLEService *pService = pServer->createService(SERVICE_UUID);
  pCharacteristic = pService->createCharacteristic(
      CHARACTERISTIC_UUID,
      BLECharacteristic::PROPERTY_READ |
          BLECharacteristic::PROPERTY_NOTIFY |
          BLECharacteristic::PROPERTY_WRITE);

  pCharacteristic->addDescriptor(new BLE2902());
  pService->start();

  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->start();
}

// Relay
bool ignitionDisabled = false;

void setRelay(bool state)
{
  ignitionDisabled = state;
  digitalWrite(RELAY, state ? HIGH : LOW); // LOW to disable, HIGH to enable
  Serial.printf("Relay %s\n", !state ? "ENGAGED (Ignition Enabled)" : "DISENGAGED (Ignition Disabled)");
}

void alert() {
    if (!crashDetected) return;
    digitalWrite(SPEAKER, speakerState ? HIGH : LOW);
    speakerState = !speakerState;
}

void resetCrashFlag() {
    crashDetected = false;
    portENTER_CRITICAL(&mux);
    speakerState = false;
    digitalWrite(SPEAKER, LOW);
    portEXIT_CRITICAL(&mux);
    Serial.println("Crash flag reset. Speaker silenced.");
}

void triggerCrashAlert() {
    crashDetected = true;
    if (deviceConnected) {
        pCharacteristic->setValue("CRASH");
        pCharacteristic->notify();
        delay(200);
    }
}