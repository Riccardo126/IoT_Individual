#ifndef SIGNAL_GENERATION_H
#define SIGNAL_GENERATION_H

#include <Arduino.h>

// Signal component structure
struct SignalComponent {
  float amplitude;
  float frequency;
};

// Predefined signal configurations
extern const SignalComponent signal1[];
extern const SignalComponent signal2[];
extern const SignalComponent signal3[];

// Signal lookup table
extern const int LOOKUP_SIZE;
extern double signalLookup[];

extern const bool ENABLE_NOISE_ANOMALY;

// Sampling parameters
extern int SAMPLE_RATE;
extern unsigned long SAMPLE_INTERVAL_US;
extern unsigned long phaseIncrement;
extern unsigned long samplePhase;

// Function declarations
void precalculateSignal(const SignalComponent components[], int numComponents);
void signalGenerationTask(void *parameter);

#endif // SIGNAL_GENERATION_H
