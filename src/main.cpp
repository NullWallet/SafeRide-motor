#include <esp_now.h>
#include <WiFi.h>
#include <Adafruit_MPU6050.h>

#include <comms.hpp>
#include <vars.hpp>

void setup()
{
  Serial.begin(115200);

  pinMode(SPEAKER, OUTPUT);
  pinMode(RELAY, OUTPUT);

  speakerTimer.attach_ms(500, alert);

  if (!mpu.begin())
  {
    Serial.println("Failed to find MPU6050 chip. Check your Wiring!");
    while (1)
    {
      delay(10);
    }
  }
  mpu.setAccelerometerRange(MPU6050_RANGE_16_G);
  Serial.println("MPU6050 Found!");

  WiFi.mode(WIFI_MODE_STA);

  if(esp_now_init() != ESP_OK) 
  {
    Serial.println("Error initializing ESP-NOW");
    return;
  }
  Serial.println("initialized ESP-NOW");

  esp_now_register_recv_cb(OnDataRecv);

  setupBLE();
}

void loop()
{
#pragma region crash detection
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  float x = a.acceleration.x;
  float y = a.acceleration.y;
  float z = a.acceleration.z;

  float magnitude = sqrt((x * x) + (y * y) + (z * z));

  if (magnitude > CRASH_THRESHOLD)
  {
    if (millis() - lastCrashTime > CRASH_COOLDOWN_MS)
    {
      Serial.println("Crash Detected!");
      Serial.print("Impact Force (m/s^2): ");
      Serial.println(magnitude);

      triggerCrashAlert();

      resetCrashTimer.once_ms(3000, resetCrashFlag);

      lastCrashTime = millis();
    }
  }
  delay(10);
#pragma endregion
}