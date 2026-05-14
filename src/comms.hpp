#pragma once
#include <cstring>

typedef unsigned char uint8_t;

typedef struct data
{
    bool helmetOn;
    float bacLevel;
} data;

data helmetData;

void OnDataRecv(const uint8_t *mac, const uint8_t *incomingData, int len)
{
    memcpy(&helmetData, incomingData, sizeof(helmetData));
}