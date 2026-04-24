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
extern const bool ENABLE_WIFI_TRANSMISSION = false;  // Toggle WiFi transmission - DISABLED to avoid watchdog timeout
extern const bool ENABLE_NOISE_ANOMALY = false;  // Toggle noise anomaly detection

// Queue for signal values (signal generation task -> sampling task)
QueueHandle_t signalQueue = NULL;
const int SIGNAL_QUEUE_SIZE = 1000;  // Buffer for signal values

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
      // Monitor queue usage
      UBaseType_t spaces = uxQueueSpacesAvailable(mqttQueue);
      Serial.print(">MQTT Queue spaces available: ");
      Serial.println(spaces);
      if (xQueueSend(mqttQueue, &averageValue, pdMS_TO_TICKS(100)) != pdPASS) {
        Serial.println("WiFi: Failed to queue average value");
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
      // Monitor queue usage
      UBaseType_t spaces = uxQueueSpacesAvailable(loraQueue);
      Serial.print(">LoRa Queue spaces available: ");
      Serial.println(spaces);
      if (xQueueSend(loraQueue, &averageValue, pdMS_TO_TICKS(1000)) != pdPASS) {
        Serial.println("LoRa: Failed to queue average value");
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
      // Start timing for per-window execution
      unsigned long windowExecStart = micros();
      if (xQueueReceive(signalQueue, &signalValue, pdMS_TO_TICKS(100)) == pdPASS) {
        
        // Only print signal and average during normal mode (not during stress test)
        if (!ENABLE_STRESS_TEST && SAMPLE_RATE <= 100) { // Limit calculation to lower sampling rates for hardware limits
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

            Serial.print(">signal:");
            Serial.println(signalValue);

            Serial.print(">WINDOW_AVERAGE: ");
            Serial.println(windowAverage, 6);

            // Send the average value via WiFi and LoRa
            sendViaWiFi(windowAverage);
            sendViaLoRa(windowAverage);

            // End timing after all processing and transmission
            unsigned long windowExecEnd = micros();
            unsigned long windowExecTime = windowExecEnd - windowExecStart;
            Serial.print(">WINDOW_EXECUTION_TIME_US: ");
            Serial.println(windowExecTime);
          }
        }
        else if (!ENABLE_STRESS_TEST && SAMPLE_RATE > 100) {
          // print signal value only every 100 samples to avoid overwhelming the serial output at high sampling rates
          static int sampleCounter = 0;
          sampleCounter++;
          if (sampleCounter >= 50) {
            Serial.print(">signal:");
            Serial.println(signalValue);
            sampleCounter = 0;
          }
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
  precalculateSignal(signal, 2);
  
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
  
  // If enabled Perform FFT analysis to find optimal sampling frequency
  if (ENABLE_FFT_ANALYSIS) {
    Serial.println("\n=== FFT ANALYSIS & ADAPTIVE SAMPLING ===");
    // Set initial oversampling rate (e.g., 4x Nyquist or a high value)
    #ifdef SAMPLE_RATE
      #undef SAMPLE_RATE
    #endif
    #define SAMPLE_RATE 4000  // Example: start with 4 kHz
    Serial.println("Initial oversampling rate set to: " + String(SAMPLE_RATE) + " Hz");

    // Sample for 5 seconds at this rate
    delay(5000);

    // Apply FFT-based adaptive sampling
    Serial.println("Applying adaptive sampling based on FFT analysis...");
    applyOptimalSamplingFrequency();
    Serial.println("=== End FFT/Adaptive Sampling ===\n");
  } else {
    Serial.println("FFT Analysis disabled. Using default sampling rate: " + String(SAMPLE_RATE) + " Hz");
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
