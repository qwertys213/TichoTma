#include "Comm.h"
#include "Sensors.h"

float soundBuffer[60];
float luxBuffer[60];
unsigned long timestampBuffer[60];  // store epoch for each reading
int minuteIndex = 0;
uint64_t epoch = 0;
unsigned long msRemainder = 0;
unsigned long lastMillis = 0;


String nodeName = "bratislava";  // node name

void setup() {
  Serial.begin(115200);
  setupComm();
  setupSensors();
  //delay(2000);
  Serial.println("Getting epoch:");
  epoch = updateTime() - 1760000000;     
  lastMillis = millis();
  msRemainder = 0;
  Serial.print("Epoch (s): ");
  Serial.println((unsigned long)epoch);

}

void loop() {
  float soundDB = measureSound();
  float lux = readLight();

  // Update epoch
  unsigned long currentMillis = millis();
  msRemainder += (currentMillis - lastMillis);
  lastMillis = currentMillis;

  while (msRemainder >= 1000) {
    epoch += 1;
    msRemainder -= 1000;
  }

  Serial.print("Epoch (s): ");
  Serial.println((unsigned long)epoch);



  soundBuffer[minuteIndex] = soundDB;
  luxBuffer[minuteIndex] = lux;
  timestampBuffer[minuteIndex] = (unsigned long)round(epoch);

  Serial.printf("[Minute %d] Sound: %.2f dBA, Light: %.6f lx, Epoch: %lu\n", minuteIndex + 1, soundDB, lux, (unsigned long)round(epoch));

  minuteIndex++;

  if (minuteIndex >= 5) {
    sendBufferedData();
    minuteIndex = 0;  // reset buffer
  }

}

void sendBufferedData() {
  // Build JSON
  String jsonPayload = "{";
  jsonPayload += "\"node_name\":\"" + nodeName + "\"";
  jsonPayload += ",\"voltage\":" + String(FuelGauge.voltage()/1000.0, 2);
  jsonPayload += ",\"battery\":" + String(FuelGauge.percent(), 1);

  // Add timestamps
  jsonPayload += ",\"timestamp\":[";
  for (int i = 0; i < minuteIndex; i++) {
    jsonPayload += String(timestampBuffer[i]);
    if (i < minuteIndex - 1) jsonPayload += ",";
  }
  jsonPayload += "]";

  // Add sound array
  jsonPayload += ",\"sound\":[";
  for (int i = 0; i < minuteIndex; i++) {
    jsonPayload += String(soundBuffer[i], 2);
    if (i < minuteIndex - 1) jsonPayload += ",";
  }
  jsonPayload += "]";

  // Add lux array
  jsonPayload += ",\"lux\":[";
  for (int i = 0; i < minuteIndex; i++) {
    jsonPayload += String(luxBuffer[i], 2);
    if (i < minuteIndex - 1) jsonPayload += ",";
  }
  jsonPayload += "]}";

  Serial.println("Sending buffered data...");
  sendDataJson(jsonPayload);  // send to server
}
