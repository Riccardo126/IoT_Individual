#include "communication.h"
#include <cstring>


// WiFi and MQTT Configuration
const char* wifi_ssid = "Galaxy_A52";  // WiFi SSID
const char* wifi_password = "qnrr8104";  // WiFi password
const char* mqtt_broker = "10.61.217.62";  // Broker address (localhost)
int mqtt_port = 1883;                   // Default  port
const char* mqtt_username = "";         // MQTT username (empty if not required)
const char* mqtt_password = "";         // MQTT password (empty if not required)
const char* mqtt_topic = "iot/average";
const char* mqtt_ack_topic = "iot/ack";

// Latency measurement variables
static unsigned long lastSendTime = 0;
static bool waitingForAck = false;

// WiFi and MQTT clients
WiFiClient espClient;
PubSubClient mqttClient(espClient);

// LoraWAN
uint32_t  license[4] = {0x9E080891,0x9C9EE657,0xB5DB54C1,0x4A40705D};
/* OTAA para*/
uint8_t DevEui[] = { 0x70, 0xB3, 0xD5, 0x7E, 0xD0, 0x07, 0x70, 0xF3 };
uint8_t AppEui[] = { 0x70, 0xB3, 0xD5, 0x7E, 0xD0, 0x02, 0xB1, 0x8A};
uint8_t AppKey[] = { 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x66, 0x01 };

SX1262 radio = new Module(8, 14, 12, 13);
const uint32_t uplinkIntervalSeconds = 5UL * 60UL;
#define RADIOLIB_LORAWAN_JOIN_EUI  0x70B3D57ED002B18A

// the Device EUI & two keys can be generated on the TTN console 
#ifndef RADIOLIB_LORAWAN_DEV_EUI   // Replace with your Device EUI
#define RADIOLIB_LORAWAN_DEV_EUI   0x70B3D57ED00770F3
#endif
#ifndef RADIOLIB_LORAWAN_APP_KEY   // Replace with your App Key 
#define RADIOLIB_LORAWAN_APP_KEY   0x21, 0x27, 0x33, 0x5F, 0x29, 0xEC, 0x79, 0x38, 0x82, 0xC2, 0x7C, 0xC9, 0x0A, 0xA3, 0x05, 0x0C
#endif
#ifndef RADIOLIB_LORAWAN_NWK_KEY   // Put your Nwk Key here
#define RADIOLIB_LORAWAN_NWK_KEY   0x--, 0x--, 0x--, 0x--, 0x--, 0x--, 0x--, 0x--, 0x--, 0x--, 0x--, 0x--, 0x--, 0x--, 0x--, 0x-- 
#endif
const LoRaWANBand_t Region = EU868;

// subband choice: for US915/AU915 set to 2, for CN470 set to 1, otherwise leave on 0
const uint8_t subBand = 0;


LoRaWANNode node(&radio, &Region, subBand);

uint64_t joinEUI =   RADIOLIB_LORAWAN_JOIN_EUI;
uint64_t devEUI  =   RADIOLIB_LORAWAN_DEV_EUI;
uint8_t appKey[] = { RADIOLIB_LORAWAN_APP_KEY };
uint8_t nwkKey[] = { RADIOLIB_LORAWAN_APP_KEY };

// Queue for passing average values to MQTT task
QueueHandle_t mqttQueue = NULL;
QueueHandle_t loraQueue = NULL;

// MQTT callback function for handling incoming messages
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // Check if the message is on the ACK topic
  if (strcmp(topic, mqtt_ack_topic) == 0) {
    if (waitingForAck) {
      unsigned long currentTime = micros();
      unsigned long latency = currentTime - lastSendTime;
      Serial.print(">MQTT_Latency: ");
      Serial.print(latency);
      Serial.println(" µs");
      waitingForAck = false;
    }
  }
}

// WiFi setup function
void setupWiFi() {
  Serial.print(">Connecting to WiFi: ");
  Serial.println(wifi_ssid);
  
  WiFi.begin(wifi_ssid, wifi_password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n>WiFi connected!");
    Serial.print(">IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n>WiFi connection failed!");
  }
}

// MQTT reconnect function
void mqttReconnect() {
  int attempts = 0;
  while (!mqttClient.connected() && attempts < 5) {
    Serial.print(">Connecting to MQTT broker: ");
    Serial.println(mqtt_broker);
    
    // Connect with credentials if provided
    bool connected = false;
    if (strlen(mqtt_username) > 0 && strlen(mqtt_password) > 0) {
      connected = mqttClient.connect("ESP32_IoT_Client", mqtt_username, mqtt_password);
    } else {
      connected = mqttClient.connect("ESP32_IoT_Client");
    }
    
    if (connected) {
      Serial.println(">MQTT connected!");
      return;
    } else {
      Serial.print(">MQTT connection failed, rc=");
      int rc = mqttClient.state();
      Serial.println(rc);
      // Print error descriptions
      switch(rc) {
        case -4: Serial.println("  (MQTT_CONNECTION_TIMEOUT)"); break;
        case -3: Serial.println("  (MQTT_CONNECTION_LOST)"); break;
        case -2: Serial.println("  (MQTT_CONNECT_FAILED)"); break;
        case -1: Serial.println("  (MQTT_DISCONNECTED)"); break;
        case 0: Serial.println("  (Connected)"); break;
        case 1: Serial.println("  (Bad protocol version)"); break;
        case 2: Serial.println("  (Bad client ID)"); break;
        case 3: Serial.println("  (Broker unavailable)"); break;
        case 4: Serial.println("  (Bad credentials)"); break;
        case 5: Serial.println("  (Not authorized)"); break;
        default: Serial.println("  (Unknown error)"); break;
      }
      delay(1000);
    }
    attempts++;
  }
}

// Connectivity test function to verify broker is reachable
void testPingBroker() {
  Serial.print(">Testing connectivity to broker: ");
  Serial.print(mqtt_broker);
  Serial.print(":");
  Serial.println(mqtt_port);
  
  WiFiClient testClient;
  
  if (testClient.connect(mqtt_broker, mqtt_port)) {
    Serial.println(">Broker is reachable!");
    testClient.stop();
  } else {
    Serial.println(">Broker is NOT reachable - connection failed");
    Serial.print(">WiFi status: ");
    Serial.println(WiFi.status());
    Serial.print(">Local IP: ");
    Serial.println(WiFi.localIP());
  }
}

// WiFi and MQTT transmission task
extern const bool ENABLE_WIFI_TRANSMISSION;
extern const bool ENABLE_LORA_TRANSMISSION;

void mqttTask(void *parameter) {
  double averageValue = 0.0;
  char payload[50];
  
  // Wait for WiFi to be available
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
  
  // Connect MQTT client
  mqttClient.setServer(mqtt_broker, mqtt_port);
  mqttClient.setCallback(mqttCallback);
  mqttReconnect();
  
  // Subscribe to ACK topic
  if (mqttClient.connected()) {
    mqttClient.subscribe(mqtt_ack_topic);
    Serial.print(">Subscribed to topic: ");
    Serial.println(mqtt_ack_topic);
  }
  
  while (1) {
    // Check if there's a value in the queue
    if (xQueueReceive(mqttQueue, &averageValue, pdMS_TO_TICKS(1000)) == pdPASS) {
      // Send via MQTT if enabled
      if (ENABLE_WIFI_TRANSMISSION) {
        // Ensure MQTT is connected
        if (!mqttClient.connected()) {
          mqttReconnect();
        }
        
        // Format and send the value
        if (mqttClient.connected()) {
          snprintf(payload, sizeof(payload), "%.6f", averageValue);
          if (mqttClient.publish(mqtt_topic, payload)) {
            // Record send time for latency measurement (using microseconds)
            lastSendTime = micros();
            waitingForAck = true;
            Serial.print(">MQTT:");
            Serial.println(payload);
          } else {
            Serial.println(">MQTT publish failed");
          }
        }
      }
    }
    
    // Keep MQTT connection alive
    if (mqttClient.connected()) {
      mqttClient.loop();
    }
    
    delay(100);  // Prevent task from hogging CPU
  }
}

// result code to text - these are error codes that can be raised when using LoRaWAN
// however, RadioLib has many more - see https://jgromes.github.io/RadioLib/group__status__codes.html for a complete list
String stateDecode(const int16_t result) {
  switch (result) {
  case RADIOLIB_ERR_NONE:
    return "ERR_NONE";
  case RADIOLIB_ERR_CHIP_NOT_FOUND:
    return "ERR_CHIP_NOT_FOUND";
  case RADIOLIB_ERR_PACKET_TOO_LONG:
    return "ERR_PACKET_TOO_LONG";
  case RADIOLIB_ERR_RX_TIMEOUT:
    return "ERR_RX_TIMEOUT";
  case RADIOLIB_ERR_MIC_MISMATCH:
    return "ERR_MIC_MISMATCH";
  case RADIOLIB_ERR_INVALID_BANDWIDTH:
    return "ERR_INVALID_BANDWIDTH";
  case RADIOLIB_ERR_INVALID_SPREADING_FACTOR:
    return "ERR_INVALID_SPREADING_FACTOR";
  case RADIOLIB_ERR_INVALID_CODING_RATE:
    return "ERR_INVALID_CODING_RATE";
  case RADIOLIB_ERR_INVALID_FREQUENCY:
    return "ERR_INVALID_FREQUENCY";
  case RADIOLIB_ERR_INVALID_OUTPUT_POWER:
    return "ERR_INVALID_OUTPUT_POWER";
  case RADIOLIB_ERR_NETWORK_NOT_JOINED:
	  return "RADIOLIB_ERR_NETWORK_NOT_JOINED";
  case RADIOLIB_ERR_DOWNLINK_MALFORMED:
    return "RADIOLIB_ERR_DOWNLINK_MALFORMED";
  case RADIOLIB_ERR_INVALID_REVISION:
    return "RADIOLIB_ERR_INVALID_REVISION";
  case RADIOLIB_ERR_INVALID_PORT:
    return "RADIOLIB_ERR_INVALID_PORT";
  case RADIOLIB_ERR_NO_RX_WINDOW:
    return "RADIOLIB_ERR_NO_RX_WINDOW";
  case RADIOLIB_ERR_INVALID_CID:
    return "RADIOLIB_ERR_INVALID_CID";
  case RADIOLIB_ERR_UPLINK_UNAVAILABLE:
    return "RADIOLIB_ERR_UPLINK_UNAVAILABLE";
  case RADIOLIB_ERR_COMMAND_QUEUE_FULL:
    return "RADIOLIB_ERR_COMMAND_QUEUE_FULL";
  case RADIOLIB_ERR_COMMAND_QUEUE_ITEM_NOT_FOUND:
    return "RADIOLIB_ERR_COMMAND_QUEUE_ITEM_NOT_FOUND";
  case RADIOLIB_ERR_JOIN_NONCE_INVALID:
    return "RADIOLIB_ERR_JOIN_NONCE_INVALID";
  case RADIOLIB_ERR_DWELL_TIME_EXCEEDED:
    return "RADIOLIB_ERR_DWELL_TIME_EXCEEDED";
  case RADIOLIB_ERR_CHECKSUM_MISMATCH:
    return "RADIOLIB_ERR_CHECKSUM_MISMATCH";
  case RADIOLIB_ERR_NO_JOIN_ACCEPT:
    return "RADIOLIB_ERR_NO_JOIN_ACCEPT";
  case RADIOLIB_LORAWAN_SESSION_RESTORED:
    return "RADIOLIB_LORAWAN_SESSION_RESTORED";
  case RADIOLIB_LORAWAN_NEW_SESSION:
    return "RADIOLIB_LORAWAN_NEW_SESSION";
  case RADIOLIB_ERR_NONCES_DISCARDED:
    return "RADIOLIB_ERR_NONCES_DISCARDED";
  case RADIOLIB_ERR_SESSION_DISCARDED:
    return "RADIOLIB_ERR_SESSION_DISCARDED";
  }
  return "See https://jgromes.github.io/RadioLib/group__status__codes.html";
}

// helper function to display any issues
void debug(bool failed, const __FlashStringHelper* message, int state, bool halt) {
  if(failed) {
    Serial.print(message);
    Serial.print(" - ");
    Serial.print(stateDecode(state));
    Serial.print(" (");
    Serial.print(state);
    Serial.println(")");
    while(halt) { delay(1); }
  }
}

// helper function to display a byte array
void arrayDump(uint8_t *buffer, uint16_t len) {
  for(uint16_t c = 0; c < len; c++) {
    char b = buffer[c];
    if(b < 0x10) { Serial.print('0'); }
    Serial.print(b, HEX);
  }
  Serial.println();
}

void setupLoRa() {
  // create the LoRaWAN node
  Serial.println(F("Initialise the radio"));
  int16_t state = radio.begin();
  debug(state != RADIOLIB_ERR_NONE, F("Initialise radio failed"), state, true);

  // Setup the OTAA session information
  state = node.beginOTAA(joinEUI, devEUI, nwkKey, appKey);
  debug(state != RADIOLIB_ERR_NONE, F("Initialise node failed"), state, true);

  Serial.println(F("Join ('login') the LoRaWAN Network"));
  state = node.activateOTAA();
  debug(state != RADIOLIB_LORAWAN_NEW_SESSION, F("Join failed"), state, true);

  Serial.println(F("Ready!\n"));
}

// FreeRTOS task for LoRaWAN state machine
void loraWanTask(void *parameter)
{
  double averageValue = 0.0;
  Serial.println(F("Sending uplink"));

  if (xQueueReceive(loraQueue, &averageValue, pdMS_TO_TICKS(100)) == pdPASS)
  {
    // Build payload byte array
    uint8_t uplinkPayload[] = { (uint8_t)(averageValue) };
    
    // Perform an uplink
    int16_t state = node.sendReceive(uplinkPayload, sizeof(uplinkPayload));    
    debug(state < RADIOLIB_ERR_NONE, F("Error in sendReceive"), state, false);

    // Check if a downlink was received 
    // (state 0 = no downlink, state 1/2 = downlink in window Rx1/Rx2)
    if(state > 0) {
      Serial.println(F("Received a downlink"));
    } else {
      Serial.println(F("No downlink received"));
    }

    Serial.print(F("Next uplink in "));
    Serial.print(uplinkIntervalSeconds);
    Serial.println(F(" seconds\n"));
    
    // Wait until next uplink - observing legal & TTN FUP constraints
    delay(uplinkIntervalSeconds * 1000UL);  // delay needs milli-seconds
  }
}