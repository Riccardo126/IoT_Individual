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

extern const bool ENABLE_NOISE_ANOMALY;

// Precalculated signal lookup table (covers 1 second, signal repeats)
const int LOOKUP_SIZE = 10000;  // 10000 samples for 1 second period
double signalLookup[LOOKUP_SIZE];  // Use double for better precision

// Sampling parameters
int SAMPLE_RATE = 50;  // samples per second - will be adjusted later based on FFT analysis or stress test results
unsigned long SAMPLE_INTERVAL_US = 1000000 / SAMPLE_RATE;  // microseconds
unsigned long phaseIncrement = LOOKUP_SIZE / SAMPLE_RATE;  // Precalculated phase advance per sample

// Phase-based lookup for consistent sampling
unsigned long samplePhase = 0;  // Phase counter (0 to LOOKUP_SIZE-1)

// Simple Box-Muller for Gaussian Noise n(t)
double generateGaussianNoise(double mu, double sigma) {
  static double V1, V2, S;
  static int phase = 0;
  double X;

  if (phase == 0) {
    do {
      double U1 = (double)random(0, 10000) / 10000.0;
      double U2 = (double)random(0, 10000) / 10000.0;
      V1 = 2 * U1 - 1;
      V2 = 2 * U2 - 1;
      S = V1 * V1 + V2 * V2;
    } while (S >= 1 || S == 0);
    X = V1 * sqrt(-2 * log(S) / S);
  } else {
    X = V2 * sqrt(-2 * log(S) / S);
  }
  phase = 1 - phase;
  return (X * sigma + mu);
}

void precalculateSignal(const SignalComponent components[], int numComponents) {
  // Precalculate signal values for 1 full second
  for (int i = 0; i < LOOKUP_SIZE; i++) {
    float t = (float)i / LOOKUP_SIZE;  // Time from 0 to 1 second
    float signal = 0.0;
    for (int j = 0; j < numComponents; j++) {
      signal += components[j].amplitude * sin(2.0 * M_PI * components[j].frequency * t);
    }

    // Add Gaussian noise n(t) if enabled (sigma = 0.2)
    if (ENABLE_NOISE_ANOMALY) {
      signal += generateGaussianNoise(0.0, 0.2);
    }

    signalLookup[i] = signal;  // Store as float without rounding
  }
  Serial.println("Signal lookup table precalculated");
}

// FreeRTOS task for signal generation
void signalGenerationTask(void *parameter) {
  // Forward declaration from main.cpp
  extern QueueHandle_t signalQueue;
  
  unsigned long lastPhase = 0;  // Track phase for period detection
  
  while (1) {
    // Get precalculated signal value using phase-based lookup
    double signalValue = signalLookup[samplePhase];
    
    // Inject Anomaly A(t) in real-time
    if (ENABLE_NOISE_ANOMALY) {
      // Probability p = 0.02
      if ((random(0, 1000) / 1000.0) < 0.02) {
        // Large-magnitude outlier +/- U(5, 15)
        double spike = (double)random(500, 1500) / 100.0; 
        if (random(0, 2) == 0) spike *= -1; // Randomize sign
        signalValue += spike;
      }
    }

    // Advance phase uniformly using precalculated increment (no CPU waste)
    samplePhase = (samplePhase + phaseIncrement) % LOOKUP_SIZE;
    
    // Queue the signal value for the sampling task
    if (signalQueue != NULL) {
      if (xQueueSend(signalQueue, &signalValue, pdMS_TO_TICKS(100)) != pdPASS) {
        Serial.println("Signal Generation: Failed to queue signal value");
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