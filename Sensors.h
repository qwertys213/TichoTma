#pragma once
#include <Arduino.h>
#include <driver/i2s.h>
#include <math.h>
#include <Wire.h>
#include "Adafruit_Sensor.h"
#include "Adafruit_TSL2591.h"

// ---------------------------
// CONFIGURATION
// ---------------------------
#define SAMPLE_RATE 16000
#define I2S_PORT I2S_NUM_0
#define CHUNK_SIZE 512              // process in small chunks
#define DURATION_SECONDS 1          // measure per loop (1 second)
#define UNCERTAINTY 30
#define MEASURE_SECONDS 60         // total seconds to average
#define SDA_PIN 21
#define SCL_PIN 22
#define Ground 19

// ---------------------------
// TSL2591 LIGHT SENSOR GLOBALS
// ---------------------------
Adafruit_TSL2591 tsl = Adafruit_TSL2591(2591);
tsl2591Gain_t currentGain = TSL2591_GAIN_MED;
tsl2591IntegrationTime_t currentTime = TSL2591_INTEGRATIONTIME_100MS;
float lastLux = 0;

// ---------------------------
// I2S MICROPHONE CONFIGURATION
// ---------------------------
void setupI2S() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = (i2s_comm_format_t)I2S_COMM_FORMAT_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = CHUNK_SIZE,
    .use_apll = false
  };

  i2s_pin_config_t pin_config = {
      .bck_io_num = 25,   // Bit clock (BCLK)
      .ws_io_num = 14,    // Word select / LRCLK (WS)
      .data_out_num = I2S_PIN_NO_CHANGE,
      .data_in_num = 12   // DOUT -> data in pin
  };

  esp_err_t err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  if (err != ESP_OK) {
    Serial.printf("i2s_driver_install error: %d\n", err);
  }
  err = i2s_set_pin(I2S_PORT, &pin_config);
  if (err != ESP_OK) {
    Serial.printf("i2s_set_pin error: %d\n", err);
  }
}

// A-WEIGHTING FILTER COEFFICIENTS (for 48 kHz)
const double b[] = {0.25574113, -0.51148225, 0.25574113};
const double a[] = {1.0, -0.47797312, 0.17462825};
double x_hist[3] = {0.0, 0.0, 0.0};
double y_hist[3] = {0.0, 0.0, 0.0};

// Apply IIR A-weighting filter
static inline double filterSample(double x) {
  x_hist[0] = x;
  double y = b[0]*x_hist[0] + b[1]*x_hist[1] + b[2]*x_hist[2]
             - a[1]*y_hist[0] - a[2]*y_hist[1];
  x_hist[2] = x_hist[1];
  x_hist[1] = x_hist[0];
  y_hist[1] = y_hist[0];
  y_hist[0] = y;
  return y;
}

// Read and process N samples
double processAndComputeRMS(size_t totalSamples) {
  static int32_t rawChunk[CHUNK_SIZE];
  size_t samplesRemaining = totalSamples;
  double sum_sq = 0.0;
  size_t samplesProcessed = 0;

  while (samplesRemaining > 0) {
    size_t toRead = (samplesRemaining > CHUNK_SIZE) ? CHUNK_SIZE : samplesRemaining;
    size_t bytesToRead = toRead * sizeof(int32_t);
    size_t bytesRead = 0;

    esp_err_t err = i2s_read(I2S_PORT, rawChunk, bytesToRead, &bytesRead, 100 / portTICK_PERIOD_MS);
    if (err != ESP_OK) {
      Serial.printf("i2s_read error: %d (bytesRequested=%u)\n", err, (unsigned)bytesToRead);
      return -1.0;
    }
    if (bytesRead == 0) {
      Serial.println("i2s_read timed out (no data). Check wiring/SEL/power.");
      return -1.0;
    }

    size_t samplesRead = bytesRead / sizeof(int32_t);
    for (size_t i = 0; i < samplesRead; ++i) {
      double normalized = (double)rawChunk[i] / 2147483648.0; // 2^31
      double w = filterSample(normalized);
      sum_sq += (w * w);
    }

    samplesProcessed += samplesRead;
    samplesRemaining -= samplesRead;
    yield();
  }

  if (samplesProcessed == 0) return -1.0;
  double rms = sqrt(sum_sq / (double)samplesProcessed);
  return rms;
}

// Compute A-weighted dB SPL from rms
float rmsToDb(double rms) {
  if (rms <= 0.0) return 0.0f;
  double db = 20.0 * log10(rms / 0.00002); // ref 20 µPa
  return round((db + UNCERTAINTY) * 100.0) / 100.0f;
}

// Measure noise for MEASURE_SECONDS (60) seconds
float measureSound() {
  float db_values[MEASURE_SECONDS];
  for (int sec = 0; sec < MEASURE_SECONDS; ++sec) {
    size_t samplesThisSecond = SAMPLE_RATE * DURATION_SECONDS;
    unsigned long start = millis();

    double rms = processAndComputeRMS(samplesThisSecond);
    if (rms < 0.0) {
      Serial.printf("[sec %d] Error reading samples. FreeHeap=%u\n", sec, ESP.getFreeHeap());
      db_values[sec] = 0.0f;
    } else {
      float db = rmsToDb(rms);
      db_values[sec] = db;
    }

    unsigned long elapsed = millis() - start;
    if (elapsed < 1000) delay(1000 - elapsed);
    yield();
  }

  double sum_pow = 0.0;
  int validCount = 0;
  for (int i = 0; i < MEASURE_SECONDS; ++i) {
    if (db_values[i] > 0.0f) {
      sum_pow += pow(10.0, db_values[i] / 10.0);
      validCount++;
    }
  }
  if (validCount == 0) return 0.0f;
  double LpAeq_1min = 10.0 * log10(sum_pow / validCount);
  Serial.printf("LpAeq,1min: %.2f dB\n", LpAeq_1min);
  return round(LpAeq_1min * 100.0) / 100.0f;
}

// LIGHT SENSOR SETUP
void setupLight() {
  Serial.println("TSL2591 Dynamic Gain & Timing");
  Wire.begin(SDA_PIN, SCL_PIN); // SDA=21, SCL=22
  if (!tsl.begin()) {
    Serial.println("Could not find a valid TSL2591 sensor, check wiring!");
  }
  tsl.setGain(currentGain);
  tsl.setTiming(currentTime);
}

// ROBUST LIGHT SENSOR READING FUNCTION
const int MAX_READ_ATTEMPTS = 5;
const int SETTLING_DELAY_MS = 200;

float readLight() {
  float lux = -1.0f;
  bool validReading = false;
  int attempt = 0;

  while (!validReading && attempt < MAX_READ_ATTEMPTS) {
    attempt++;
    sensors_event_t event;
    tsl.getEvent(&event);

    if (event.light > 0 && !isnan(event.light)) {
      // --- VALID READING OBTAINED ---
      lux = event.light;
      validReading = true; // Exit the loop
      
      Serial.print("Attempt ");
      Serial.print(attempt);
      Serial.print(" - Valid Lux: ");
      Serial.println(lux, 6);

      // --- AUTO-RANGING LOGIC FOR THE *NEXT* READING ---
      if (lux < 10.0f) {
        if (currentGain != TSL2591_GAIN_MAX || currentTime != TSL2591_INTEGRATIONTIME_600MS) {
          currentGain = TSL2591_GAIN_MAX;
          currentTime = TSL2591_INTEGRATIONTIME_600MS;
          tsl.setGain(currentGain);
          tsl.setTiming(currentTime);
          Serial.println("Auto-ranging for next read: Gain -> MAX, Integration -> 600ms");
        }
      } else if (lux < 100.0f) {
        if (currentGain != TSL2591_GAIN_HIGH || currentTime != TSL2591_INTEGRATIONTIME_300MS) {
          currentGain = TSL2591_GAIN_HIGH;
          currentTime = TSL2591_INTEGRATIONTIME_300MS;
          tsl.setGain(currentGain);
          tsl.setTiming(currentTime);
          Serial.println("Auto-ranging for next read: Gain -> HIGH, Integration -> 300ms");
        }
      } else if (lux < 1000.0f) {
        if (currentGain != TSL2591_GAIN_MED || currentTime != TSL2591_INTEGRATIONTIME_200MS) {
          currentGain = TSL2591_GAIN_MED;
          currentTime = TSL2591_INTEGRATIONTIME_200MS;
          tsl.setGain(currentGain);
          tsl.setTiming(currentTime);
          Serial.println("Auto-ranging for next read: Gain -> MED, Integration -> 200ms");
        }
      } else { // lux >= 1000.0f
        if (currentGain != TSL2591_GAIN_LOW || currentTime != TSL2591_INTEGRATIONTIME_100MS) {
          currentGain = TSL2591_GAIN_LOW;
          currentTime = TSL2591_INTEGRATIONTIME_100MS;
          tsl.setGain(currentGain);
          tsl.setTiming(currentTime);
          Serial.println("Auto-ranging for next read: Gain -> LOW, Integration -> 100ms");
        }
      }
    } else {
      // --- INVALID READING (OVERLOAD OR ERROR) ---
      Serial.print("Attempt ");
      Serial.print(attempt);
      Serial.print(" - Invalid reading (");
      Serial.print(event.light);
      Serial.println(" Lux). Sensor may be saturated.");
      
      currentGain = TSL2591_GAIN_LOW;
      currentTime = TSL2591_INTEGRATIONTIME_100MS;
      tsl.setGain(currentGain);
      tsl.setTiming(currentTime);
      Serial.println("Action: Resetting to LOW gain, 100ms integration. Retrying...");
      
      delay(SETTLING_DELAY_MS);
    }
  } // end while loop

  if (!validReading) {
    Serial.println("FATAL: Failed to get a valid sensor reading after all attempts.");
    return -1.0f; // Return a clear error code
  }

  return lux;
}

// MAIN SENSOR SETUP
void setupSensors() {
  pinMode(Ground, OUTPUT);
  digitalWrite(Ground, LOW);
  Serial.begin(115200);
  delay(100);
  setupLight();
  setupI2S();
  Serial.println("Starting sound + light measurement...");
}
