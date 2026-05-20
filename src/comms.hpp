#pragma once
#include "vars.hpp"

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
    float bacLevel;
};

struct PassengerData
{
    HelmetTypes helmetType;
    bool helmetOn;
};

DriverData driverData;
PassengerData passengerData;

void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len)
{
    if (len < 1)
        return; // prevent out of bounds read

    uint8_t helmetType = incomingData[0] & 0x01; // Extract helmet type (bit 0)
    if (helmetType == HelmetTypes::Driver)
    {
        memcpy(&driverData, incomingData, sizeof(driverData));
        Serial.print("Received Driver Data - Helmet On: ");
        Serial.println((bool)driverData.helmetOn);
    }
    else if (helmetType == HelmetTypes::Passenger)
    {
        memcpy(&passengerData, incomingData, sizeof(passengerData));
        Serial.printf("Received Passenger Data - Helmet On: %s", (passengerData.helmetOn ? "True" : "False"));
        if (passengerData.helmetOn)
        {
            triggerCrashAlert();
            resetCrashTimer.once_ms(3000, resetCrashFlag);
        }
    }
    else
    {
        return; // Invalid helmet type
    }
}