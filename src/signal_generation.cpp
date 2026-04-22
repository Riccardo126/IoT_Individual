#include "signal_generation.h"
#include <math.h>

// Signal component definitions
const SignalComponent signal1[] = {
  {2.0, 3.0},
  {4.0, 5.0}
};

const SignalComponent signal2[] = {
  {1.0, 1.0},
  {3.0, 10.0}
};

const SignalComponent signal3[] = {
  {5.0, 2.0},
  {2.0, 7.0},
  {1.0, 15.0}
};

// Precalculated signal lookup table (covers 1 second, signal repeats)
const int LOOKUP_SIZE = 10000;  // 10000 samples for 1 second period
double signalLookup[LOOKUP_SIZE];  // Use double for better precision

// Sampling parameters
int SAMPLE_RATE = 50;  // samples per second - will be adjusted later based on FFT analysis or stress test results
unsigned long SAMPLE_INTERVAL_US = 1000000 / SAMPLE_RATE;  // microseconds
unsigned long phaseIncrement = LOOKUP_SIZE / SAMPLE_RATE;  // Precalculated phase advance per sample

// Phase-based lookup for consistent sampling
unsigned long samplePhase = 0;  // Phase counter (0 to LOOKUP_SIZE-1)

void precalculateSignal(const SignalComponent components[], int numComponents) {
  // Precalculate signal values for 1 full second
  for (int i = 0; i < LOOKUP_SIZE; i++) {
    float t = (float)i / LOOKUP_SIZE;  // Time from 0 to 1 second
    float signal = 0.0;
    for (int j = 0; j < numComponents; j++) {
      signal += components[j].amplitude * sin(2.0 * M_PI * components[j].frequency * t);
    }
    signalLookup[i] = signal;  // Store as float without rounding
  }
  Serial.println(">Signal lookup table precalculated");
}

// FreeRTOS task for signal generation
void signalGenerationTask(void *parameter) {
  // Forward declaration from main.cpp
  extern QueueHandle_t signalQueue;
  
  unsigned long lastPhase = 0;  // Track phase for period detection
  
  while (1) {
    // Get precalculated signal value using phase-based lookup
    double signalValue = signalLookup[samplePhase];
    
    // Advance phase uniformly using precalculated increment (no CPU waste)
    samplePhase = (samplePhase + phaseIncrement) % LOOKUP_SIZE;
    
    // Queue the signal value for the sampling task
    if (signalQueue != NULL) {
      if (xQueueSend(signalQueue, &signalValue, pdMS_TO_TICKS(100)) != pdPASS) {
        Serial.println(">Signal Generation: Failed to queue signal value");
      }
    }
    
    // Delay until next sample
    extern const bool ENABLE_STRESS_TEST;
    extern unsigned long currentIntervalUs;
    
    if (ENABLE_STRESS_TEST) {
      delayMicroseconds(currentIntervalUs);
    } else {
      delayMicroseconds(SAMPLE_INTERVAL_US);
    }
  }
}
