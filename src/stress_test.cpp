#include "stress_test.h"

// Stress test parameters
const unsigned long testIntervals[] = {1000, 500, 200, 150, 100, 50, 10};  // Test intervals in microseconds
const int NUM_INTERVALS = sizeof(testIntervals) / sizeof(testIntervals[0]);
int currentIntervalIndex = 0;
unsigned long currentIntervalUs = testIntervals[0];  // Start with first interval
unsigned long sampleCount = 0;
unsigned long lastStatusTime = 0;
const unsigned long STATUS_INTERVAL = 5000;  // Report every 5 seconds
float maxFrequency = 0.0;
unsigned long intervalAtMaxFrequency = testIntervals[0];  // Track which interval achieved max
int plateauCount = 0;
const int PLATEAU_THRESHOLD = 2;  // Require 2 stable periods before moving to next
float frequencyVariance = 0.0;
const float STABILITY_THRESHOLD = 0.02;  // 2% deviation from expected samples
int stabilityFailCount = 0;
bool stressTestComplete = false;

// Function to handle stress test logic
void handleStressTest(unsigned long expectedSamples) {
  // Calculate expected vs actual samples for the current interval
  unsigned long deviation = abs((long)sampleCount - (long)expectedSamples);
  frequencyVariance = (float)deviation / expectedSamples;  // Deviation as percentage
  
  Serial.print(">expected:");
  Serial.print(expectedSamples);
  Serial.print(" >actual:");
  Serial.print(sampleCount);
  Serial.print(" >deviation:");
  Serial.println(frequencyVariance);
  
  // Check if current interval achieves expected sample count (stability check)
  bool isStable = (frequencyVariance < STABILITY_THRESHOLD);
  
  if (!isStable) {
    stabilityFailCount++;
    // If unstable, skip to next interval immediately
    if (stabilityFailCount >= PLATEAU_THRESHOLD) {
      Serial.print(">UNSTABLE_interval_us:");
      Serial.println(currentIntervalUs);
      currentIntervalIndex++;
      plateauCount = 0;
      stabilityFailCount = 0;
    }
  } else {
    stabilityFailCount = 0;  // Reset if stable
    
    // Track maximum frequency (only from stable measurements)
    float frequency = (sampleCount * 1000.0) / STATUS_INTERVAL;
    if (frequency > maxFrequency) {
      maxFrequency = frequency;
      intervalAtMaxFrequency = currentIntervalUs;  // Store which interval gave max
      plateauCount = 0;  // Reset plateau counter when we get new max
    } else {
      plateauCount++;  // Increment if frequency didn't improve
    }
    
    // Check if we should move to next interval or stop
    if (plateauCount >= PLATEAU_THRESHOLD) {
      currentIntervalIndex++;
      plateauCount = 0;
      stabilityFailCount = 0;
    }
  }
  
  // Check if we've tested all intervals
  if (currentIntervalIndex >= NUM_INTERVALS) {
    // All intervals tested
    stressTestComplete = true;
    Serial.println("\n=== STRESS TEST COMPLETE ===");
    Serial.print(">max_frequency:");
    Serial.println(maxFrequency);
    Serial.print(">max_interval_us:");
    Serial.println(intervalAtMaxFrequency);  // Report the interval that achieved max
    Serial.println("=== End of stress test ===");
  } else {
    // Move to next interval
    currentIntervalUs = testIntervals[currentIntervalIndex];
  }
  
  sampleCount = 0;
  lastStatusTime = millis();
}
