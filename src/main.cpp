#include <Arduino.h>
#include <math.h>
#include <deque>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "arduinoFFT.h"
#include "communication.h"
#include "signal_generation.h"
#include "frequency_fft.h"
#include "stress_test.h"

// Toggles for enabling/disabling features
extern const bool ENABLE_STRESS_TEST = false;  // Toggle this to enable/disable stress test
extern const bool ENABLE_FFT_ANALYSIS = false;  // Toggle FFT-based optimal frequency calculation
extern const bool ENABLE_LORA_TRANSMISSION = false;  // Toggle LoRa transmission
extern const bool ENABLE_WIFI_TRANSMISSION = false;  // Toggle WiFi transmission

// Queue for signal values (signal generation task -> sampling task)
QueueHandle_t signalQueue = NULL;
const int SIGNAL_QUEUE_SIZE = 50;  // Buffer for signal values

// Window averaging parameters
const unsigned long WINDOW_SIZE_MS = 5000;  // 5 second moving window for averaging
double windowAverage = 0.0;

// Structure for storing timestamped samples
struct TimestampedSample {
  unsigned long timestamp;
  double value;
};

// WiFi transmission function - sends every 0.1 seconds
void sendViaWiFi(double averageValue) {
  if (!ENABLE_WIFI_TRANSMISSION) return;  // Skip if WiFi transmission is disabled
  
  static unsigned long lastWiFiSendTime = 0;
  unsigned long currentTime = millis();
  
  // Send every 100 ms (0.1 seconds)
  if (currentTime - lastWiFiSendTime >= 100) {
    if (mqttQueue != NULL) {
      if (xQueueSend(mqttQueue, &averageValue, pdMS_TO_TICKS(100)) != pdPASS) {
        Serial.println(">WiFi: Failed to queue average value");
      }
    }
    lastWiFiSendTime = currentTime;
  }
}

// LoRa transmission function - sends every 10 seconds
void sendViaLoRa(double averageValue) {
  if (!ENABLE_LORA_TRANSMISSION) return;  // Skip if LoRa transmission is disabled
  
  static unsigned long lastLoRaSendTime = 0;
  unsigned long currentTime = millis();
  
  // Send every 10 seconds (10000 ms)
  if (currentTime - lastLoRaSendTime >= 10000) {
    if (loraQueue != NULL) {
      if (xQueueSend(loraQueue, &averageValue, pdMS_TO_TICKS(100)) != pdPASS) {
        Serial.println(">LoRa: Failed to queue average value");
      }
    }
    lastLoRaSendTime = currentTime;
  }
}

// FreeRTOS task for sampling
void samplingTask(void *parameter) {
  double signalValue = 0.0;
  std::deque<TimestampedSample> windowSamples;
  
  while (1) {
    // Receive signal value from the generation task
    if (signalQueue != NULL) {
      if (xQueueReceive(signalQueue, &signalValue, pdMS_TO_TICKS(200)) == pdPASS) {
        unsigned long currentTime = millis();
        
        // Add new sample to window with timestamp
        windowSamples.push_back({currentTime, signalValue});
        
        // Remove samples older than 5 seconds (moving window)
        while (!windowSamples.empty() && (currentTime - windowSamples.front().timestamp) > WINDOW_SIZE_MS) {
          windowSamples.pop_front();
        }
        
        // Calculate moving window average at every sample
        if (!windowSamples.empty()) {
          double sum = 0.0;
          for (const auto& sample : windowSamples) {
            sum += sample.value;
          }
          windowAverage = sum / windowSamples.size();
          
          // Only print signal during normal mode (not during stress test)
          if (!ENABLE_STRESS_TEST) {
            Serial.print(">signal:");
            Serial.println(signalValue);
          }
          
          Serial.print(">WINDOW_AVERAGE: ");
          Serial.println(windowAverage, 6);
          
          // Send the average value via WiFi and LoRa
          sendViaWiFi(windowAverage);
          sendViaLoRa(windowAverage);
        }
        
        sampleCount++;
        
        if (ENABLE_STRESS_TEST && !stressTestComplete) {
          // Calculate expected vs actual samples for the current interval
          unsigned long expectedSamples = (STATUS_INTERVAL * 1000) / currentIntervalUs;  // Expected samples in STATUS_INTERVAL
          handleStressTest(expectedSamples);
        }
      }
    }
  }
}

void setup() {
  // Initialize serial communication
  Serial.begin(921600); // Use high baud rate for faster output during stress tests and FFT analysis
  delay(1000);  // Wait for serial to initialize
  
  const SignalComponent *signal = signal1;  // Select which signal to generate (signal1, signal2, or signal3)
  
  // Precalculate signal lookup table
  precalculateSignal(signal1, 2);
  
  // Create queue for MQTT values
  mqttQueue = xQueueCreate(100, sizeof(double));
  // Create queue for LoRa values
  loraQueue = xQueueCreate(100, sizeof(double));
  
  // Setup WiFi if transmission is enabled
  if (ENABLE_WIFI_TRANSMISSION) {
    setupWiFi();
    // Test connectivity to broker via ping
    testPingBroker();
  }
  if (ENABLE_LORA_TRANSMISSION) {
    setupLoRa();
  }
  
  // Perform FFT analysis to find optimal sampling frequency (optional)
  if (ENABLE_FFT_ANALYSIS) {
    Serial.println("\n=== FFT ANALYSIS ===");
    applyOptimalSamplingFrequency();
    Serial.println("=== End FFT ===\n");
  } else {
    Serial.println(">FFT Analysis disabled. Using default sampling rate: " + String(SAMPLE_RATE) + " Hz");
  }
  
  Serial.println("Heltec ESP32 LoRa v3");
  Serial.println("Generating composite signal: " + String(signal[0].amplitude) + "*sin(2*pi* " + String(signal[0].frequency) + "*t) + " + String(signal[1].amplitude) + "*sin(2*pi* " + String(signal[1].frequency) + "*t)");
  
  if (ENABLE_STRESS_TEST) {
    Serial.println("STRESS TEST MODE ENABLED - Gradually decreasing interval to find max frequency");
  }
  
  Serial.println("Ready to transmit...\n");
  
  // Create queue for signal values
  signalQueue = xQueueCreate(SIGNAL_QUEUE_SIZE, sizeof(double));
  
  if (signalQueue == NULL) {
    Serial.println("Failed to create signal queue");
  }
  
  // Create signal generation task with priority 1, running on core 1
  xTaskCreatePinnedToCore(
    signalGenerationTask,   // Task function
    "SignalGenerationTask", // Task name
    2048,                   // Stack size (bytes)
    NULL,                   // Parameter
    1,                      // Priority
    NULL,                   // Task handle
    1                       // Core ID (0 or 1)
  );
  
  // Create sampling task with priority 2, running on core 0
  xTaskCreatePinnedToCore(
    samplingTask,           // Task function
    "SamplingTask",         // Task name
    2048,                   // Stack size (bytes)
    NULL,                   // Parameter
    2,                      // Priority
    NULL,                   // Task handle
    0                       // Core ID (0 or 1)
  );

  if (ENABLE_LORA_TRANSMISSION){
    // Create LoRaWAN task with priority 1, running on core 0
    xTaskCreatePinnedToCore(
      loraWanTask,           // Task function
      "LoRaWANTask",         // Task name
      4096,                  // Stack size (bytes) - larger stack for LoRaWAN
      NULL,                  // Parameter
      1,                     // Priority
      NULL,                  // Task handle
      0                      // Core ID (0 for core 0)
    );
  }
  
  if (ENABLE_WIFI_TRANSMISSION) {
    // Create WiFi/MQTT transmission task with priority 1, running on core 0
    xTaskCreatePinnedToCore(
      mqttTask,  // Task function
      "WiFiTransmissionTask",// Task name
      4096,                  // Stack size (bytes)
      NULL,                  // Parameter
      1,                     // Priority
      NULL,                  // Task handle
      0                      // Core ID (0 for core 0)
    );
  }
}

void loop() {
  // Main loop can be used for other tasks
  delay(10000);  // Minimal activity on core 0
}
