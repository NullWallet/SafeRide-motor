#pragma once

#include <Ticker.h>
#include <Adafruit_MPU6050.h>

#include <BLE2902.h>
#include <BLEDevice.h>
#include <BLEServer.h>

#include "comms.hpp"

#define RELAY 18
#define SPEAKER 26

// --- Speaker
volatile bool speakerState = true;
Ticker speakerTimer;
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

// Accelerometer
#define CRASH_THRESHOLD (2 * 9.81)

Ticker resetCrashTimer;
volatile bool crashDetected = false;
unsigned long lastCrashTime = 0;
const unsigned long CRASH_COOLDOWN_MS = 5000;

Adafruit_MPU6050 mpu;

// MAC address of your driver helmet ESP32 — replace with actual
uint8_t driverHelmetMac[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};

bool espNowPeerAdded = false;

// Bluetooth
#define SERVICE_UUID "480ffc7b-9790-4753-a322-2c054412539a"
#define CHARACTERISTIC_UUID "4a41a0dd-212a-4b1f-a7ab-09f0f564fd7c"

// ─── Commands motor → helmet ──────────────────────────────────────────────
enum MotorCommand : uint8_t
{
    CMD_START_TEST = 1,
};

struct MotorData
{
    MotorCommand command;
};


void setupESPNowSend()
{
    if (espNowPeerAdded)
        return;

    esp_now_peer_info_t peerInfo = {};
    memcpy(peerInfo.peer_addr, driverHelmetMac, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;

    if (esp_now_add_peer(&peerInfo) != ESP_OK)
        Serial.println("[ESP-NOW] Failed to add driver helmet peer");
    else
    {
        Serial.println("[ESP-NOW] Driver helmet peer added");
        espNowPeerAdded = true;
    }
}

void sendCommandToDriver(MotorCommand cmd)
{
  setupESPNowSend();
  MotorData data;
  data.command = cmd;
  esp_err_t result = esp_now_send(driverHelmetMac, (uint8_t *)&data, sizeof(data));
  Serial.printf("[ESP-NOW] Send CMD %d → %s\n", cmd,
                result == ESP_OK ? "OK" : "FAILED");
}

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

class CharacteristicCallbacks : public BLECharacteristicCallbacks
{
  void onWrite(BLECharacteristic *pChar)
  {
    std::string val = pChar->getValue();
    if (val == "START_TEST")
    {
      Serial.println("[BLE] Received START_TEST from app — forwarding to driver helmet");
      sendCommandToDriver(CMD_START_TEST);
    }
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
  pCharacteristic->setCallbacks(new CharacteristicCallbacks());
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

// TODO: Remove
//  Safe version — add to vars.hpp
void notifyBLE(const char *msg)
{
  if (!deviceConnected)
    return;
  pCharacteristic->setValue(msg);
  pCharacteristic->notify();
  delay(20); // give BLE stack time between back-to-back notifications
}

void resetCrashFlag()
{
  crashDetected = false;
  portENTER_CRITICAL(&mux);
  speakerState = false;
  portEXIT_CRITICAL(&mux);
  Serial.println("Crash flag reset. Speaker silenced.");
}

void triggerCrashAlert()
{
  crashDetected = true;
  if (deviceConnected)
  {
    pCharacteristic->setValue("CRASH");
    pCharacteristic->notify();
    delay(200);
  }
}

// --- Helmet Removal Countdown ---
bool countdownActive = false;
unsigned long countdownStartMs = 0;
int lastBeepSecond = -1; // which second was last beeped
bool countdownBeepOn = false;
unsigned long countdownBeepOnMs = 0;
bool ignitionCutByCountdown = false;

const unsigned long COUNTDOWN_TOTAL_MS = 30000UL; // 30 s total
const unsigned long COUNTDOWN_CONT_MS = 25000UL;  // continuous beep starts at 25 s
const unsigned long BEEP_PULSE_MS = 150UL;        // short beep duration (ms)