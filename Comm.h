#pragma once

#define TINY_GSM_MODEM_SIM800

#include <Wire.h>
#include <TinyGsmClient.h>
#include "MAX17043.h"
#include <ArduinoJson.h>


// GPRS credentials
const char apn[]      = "internet";
const char gprsUser[] = "";
const char gprsPass[] = "";

const char simPIN[]   = "0000";
const char server[] = "xxx.xxx.xxx.xxx";
const char resource[] = "/receive_data";        
const int  port = 80;

// TTGO T-Call pins
#define MODEM_RST      5
#define MODEM_PWKEY    4
#define MODEM_POWER    23
#define MODEM_TX       27
#define MODEM_RX       26

#define SerialMon Serial
#define SerialAT  Serial1

#define TINY_GSM_MODEM_SIM800
#define TINY_GSM_RX_BUFFER 1024

#include <TinyGsmClient.h>
TinyGsm modem(SerialAT);
TinyGsmClient client(modem);

void setupComm() {
  SerialMon.begin(115200);
  delay(10);

  // Power setup
  pinMode(MODEM_PWKEY, OUTPUT);
  pinMode(MODEM_RST, OUTPUT);
  pinMode(MODEM_POWER, OUTPUT);
  digitalWrite(MODEM_PWKEY, LOW);
  digitalWrite(MODEM_RST, HIGH);
  digitalWrite(MODEM_POWER, HIGH);

  SerialAT.begin(115200, SERIAL_8N1, MODEM_RX, MODEM_TX);
  delay(3000);

  SerialMon.println("restarting modem...");
  modem.restart();

  Wire.begin(21, 22);   // SDA=21, SCL=22 on TTGO T-Call

  Serial.println("Initializing MAX17043...");

  if (FuelGauge.begin(&Wire)) {
    Serial.println("Fuel gauge initialized.");
    FuelGauge.quickstart();
    delay(125);
  } else {
    Serial.println("MAX17043 not detected. Check wiring!");
    while (true);
  }

}
void TurnOnModem(){
  SerialMon.println("Initializing modem...");
  if (strlen(simPIN) && modem.getSimStatus() != 3 ) {
    modem.simUnlock(simPIN);
  }
  delay(500);
long rssi = modem.getSignalQuality();

// Wait until connection is established
while (rssi == 0 || rssi == 99) {
  Serial.println("Waiting for network connection...");
  delay(1000);
  rssi = modem.getSignalQuality();
}

// When connected, print the signal strength
Serial.print("Signal quality (RSSI): ");
Serial.println(rssi);
  SerialMon.print("Connecting to APN: ");
  SerialMon.println(apn);
  delay(500);
  if (!modem.gprsConnect(apn)) {
    SerialMon.println("GPRS connection failed");
    delay(10000);
    TurnOnModem();
  }
  SerialMon.println("GPRS connected");

  SerialMon.print("Connecting to server: ");
  SerialMon.println(server);
  delay(500);
  if (!client.connect(server, port)) {
    SerialMon.println("Server connection failed");
    modem.gprsDisconnect();
    delay(10000);
    TurnOnModem();
  }
  SerialMon.println("Server connected");
  
}
unsigned long updateTime() {
  TurnOnModem();
  delay(1000);

  // --- Proper POST request ---
  String request =
    "POST /time HTTP/1.1\r\n"
    "Host: " + String(server) + "\r\n"
    "Content-Type: application/json\r\n"
    "Content-Length: 0\r\n"
    "Connection: close\r\n\r\n";

  SerialMon.println("=== HTTP REQUEST ===");
  SerialMon.println(request);
  SerialMon.println("====================");

  client.print(request);
  delay(500);
  // Wait for response
  unsigned long timeout = millis();
  while (!client.available() && millis() - timeout < 5000) {
    delay(10);
  }
  delay(500);
  if (!client.available()) {
    Serial.println("No response from server");
    client.stop();
    delay(1000);
    modem.gprsDisconnect();
    return 0;
  }
  
  // Read full response
  String resp;
  while (client.available()) {
    resp += (char)client.read();
  }

  SerialMon.println("=== HTTP RESPONSE ===");
  SerialMon.println(resp);
  SerialMon.println("====================");

  int jsonStart = resp.indexOf("{");
  if (jsonStart == -1) {
    Serial.println("Invalid response: " + resp);
    return 0;
  }

  resp = resp.substring(jsonStart);

  StaticJsonDocument<200> doc;
  DeserializationError error = deserializeJson(doc, resp);
  if (error) {
    Serial.print("JSON parse error: ");
    Serial.println(error.c_str());
    return 0;
  }

  unsigned long epoch = doc["time"];
  Serial.print("Current epoch: ");
  Serial.println(epoch);
  
  modem.gprsDisconnect();
  return epoch;
}




void sendDataJson(String jsonPayload) {
  SerialMon.println(jsonPayload.length());
  TurnOnModem();
  delay(1000);
  

  // === Send HTTP POST ===
  client.print(String("POST ") + resource + " HTTP/1.1\r\n");
  client.print(String("Host: ") + server + "\r\n");
  client.println("Connection: close");
  client.println("Content-Type: application/json");
  client.print("Content-Length: ");
  client.println(jsonPayload.length());
  client.println();
  client.print(jsonPayload);
  client.println();
  client.flush();

  SerialMon.println("HTTP POST sent:");
  SerialMon.println(jsonPayload);
  delay(jsonPayload.length()*10);
  // === Read HTTP response ===
  unsigned long timeout = millis();
  while (client.connected() && millis() - timeout < 10000L) {
    while (client.available()) {
      char c = client.read();
      SerialMon.print(c);
      timeout = millis();
    }
  }

  client.stop();
  SerialMon.println("\nServer disconnected");

  //modem.gprsDisconnect();
  SerialMon.println("GPRS disconnected");
  modem.gprsDisconnect();
}
