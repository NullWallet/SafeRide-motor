#pragma once
#include <cstring>
#include <vars.hpp>

typedef unsigned char uint8_t;

enum HelmetTypes { Driver, Passenger };

struct DriverData {
    HelmetTypes helmetType;
    bool helmetOn;
    bool isSober;
};

struct HelmetState {
    bool received = false;
    bool helmetOn = false;
    bool isSober = false;
    bool everOn = false;
    unsigned long lastUpdateMs = 0;
};

HelmetState driverState;
HelmetState passengerState;

// Dedicated test result packet from driver helmet → motor
struct TestResultData { bool isSober; };

void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len)
{
    // ── DriverData heartbeat ──────────────────────────────────────────────
    if (len == sizeof(DriverData))
    {
        DriverData incoming;
        memcpy(&incoming, incomingData, sizeof(incoming));

        if (incoming.helmetType == HelmetTypes::Driver)
        {
            driverState.received = true;
            driverState.helmetOn = incoming.helmetOn;
            driverState.isSober  = incoming.isSober;
            driverState.lastUpdateMs = millis();
            if (incoming.helmetOn)
                driverState.everOn = true;

            notifyBLE(incoming.helmetOn ? "HELMET:DRIVER:1" : "HELMET:DRIVER:0");
            notifyBLE(incoming.isSober  ? "SOBER:1"         : "SOBER:0");
        }
        else if (incoming.helmetType == HelmetTypes::Passenger)
        {
            passengerState.received = true;
            passengerState.helmetOn = incoming.helmetOn;
            passengerState.lastUpdateMs = millis();
            if (incoming.helmetOn)
                passengerState.everOn = true;

            notifyBLE(incoming.helmetOn ? "HELMET:PASSENGER:1" : "HELMET:PASSENGER:0");
        }
        return;
    }

    // ── TestResultData — sent only when a test finishes ───────────────────
    if (len == sizeof(TestResultData))
    {
        TestResultData result;
        memcpy(&result, incomingData, sizeof(result));
        Serial.printf("[ESP-NOW] Test result received: %s\n",
                      result.isSober ? "SOBER" : "DRUNK");

        // Notify app with a dedicated message (distinct from heartbeat SOBER:)
        notifyBLE(result.isSober ? "TEST_RESULT:SOBER" : "TEST_RESULT:DRUNK");
        return;
    }

    Serial.printf("WARNING: Unknown packet size %d\n", len);
}