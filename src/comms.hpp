#pragma once
#include <cstring>

typedef unsigned char uint8_t;

enum HelmetTypes
{
    Driver,
    Passenger
};

struct DriverData
{
    HelmetTypes helmetType;
    bool helmetOn;
    float bacLevel; // only meaningful for Driver; Passenger sends 0.0f
};

// Separate state for each helmet
struct HelmetState
{
    bool received = false;
    bool helmetOn = false;
    float bacLevel = 0.0f;
};

HelmetState driverState;
HelmetState passengerState;

void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len)
{
    // Print the sender's MAC address and payload size
    Serial.printf("\n--- Incoming ESP-NOW Data ---\n");
    Serial.printf("MAC: %02X:%02X:%02X:%02X:%02X:%02X | Bytes: %d\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], len);

    if (len != sizeof(DriverData))
    {
        Serial.println("WARNING: Received packet size does not match DriverData struct!");
    }

    DriverData incoming;
    memcpy(&incoming, incomingData, sizeof(incoming));

    if (incoming.helmetType == HelmetTypes::Driver)
    {
        Serial.printf("Type: DRIVER | Helmet On: %s | BAC: %.3f\n",
                      incoming.helmetOn ? "True" : "False",
                      incoming.bacLevel);

        driverState.received = true;
        driverState.helmetOn = incoming.helmetOn;
        driverState.bacLevel = incoming.bacLevel;
    }
    else if (incoming.helmetType == HelmetTypes::Passenger)
    {
        Serial.printf("Type: PASSENGER | Helmet On: %s\n",
                      incoming.helmetOn ? "True" : "False");

        passengerState.received = true;
        passengerState.helmetOn = incoming.helmetOn;
    }
    else
    {
        Serial.println("WARNING: Unknown HelmetType received!");
    }
}