#ifndef COMMUNICATION_H
#define COMMUNICATION_H

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <RadioLib.h>

extern const bool ENABLE_WIFI_TRANSMISSION;
extern const bool ENABLE_LORA_TRANSMISSION;

// WiFi and MQTT Configuration
extern const char* wifi_ssid;
extern const char* wifi_password;
extern const char* mqtt_broker;
extern int mqtt_port;
extern const char* mqtt_username;
extern const char* mqtt_password;
extern const char* mqtt_topic;
extern const char* mqtt_ack_topic;

// LoRaWAN Configuration - OTAA Parameters
extern uint8_t DevEui[];
extern uint8_t AppEui[];
extern uint8_t AppKey[];

// LoRaWAN Configuration - General
extern uint32_t license[4];
extern uint16_t userChannelsMask[6];
extern uint32_t appTxDutyCycle;
extern bool overTheAirActivation;
extern bool loraWanAdr;
extern uint8_t loraWanDatarate;
extern bool isTxConfirmed;
extern uint8_t appPort;
extern uint8_t confirmedNbTrials;
extern uint8_t debugLevel;

// Heltec WiFi LoRa 32 V3 Pin Definitions
#ifndef DIO1
#define DIO1 14
#endif
#ifndef DIO0
#define DIO0 13
#endif
#ifndef RST_LoRa
#define RST_LoRa 12
#endif
#ifndef SS
#define SS 8
#endif
#ifndef SCK
#define SCK 9
#endif
#ifndef MOSI
#define MOSI 10
#endif
#ifndef MISO
#define MISO 11
#endif

// WiFi and MQTT clients
extern WiFiClient espClient;
extern PubSubClient mqttClient;

// Queue for passing average values to MQTT task
extern QueueHandle_t mqttQueue;

// WiFi setup function
void setupWiFi();

// MQTT reconnect function
void mqttReconnect();

// MQTT transmission task (handles both MQTT and LoRa transmission)
void mqttTask(void *parameter);

// Queue for passing average values to LoRa task
extern QueueHandle_t loraQueue;

// LoRa setup function
void setupLoRa();

// LoRaWAN state machine task
void loraWanTask(void *parameter);

// Ping test function to verify broker connectivity
void testPingBroker();

#endif // COMMUNICATION_H
