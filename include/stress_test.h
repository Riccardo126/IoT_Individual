#ifndef STRESS_TEST_H
#define STRESS_TEST_H

#include <Arduino.h>

// Stress test parameters
extern const unsigned long testIntervals[];
extern const int NUM_INTERVALS;
extern int currentIntervalIndex;
extern unsigned long currentIntervalUs;
extern unsigned long sampleCount;
extern unsigned long lastStatusTime;
extern const unsigned long STATUS_INTERVAL;
extern float maxFrequency;
extern unsigned long intervalAtMaxFrequency;
extern int plateauCount;
extern const int PLATEAU_THRESHOLD;
extern float frequencyVariance;
extern const float STABILITY_THRESHOLD;
extern int stabilityFailCount;
extern bool stressTestComplete;

// Function to handle stress test logic
void handleStressTest(unsigned long expectedSamples);

#endif // STRESS_TEST_H
