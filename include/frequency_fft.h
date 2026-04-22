#ifndef FREQUENCY_FFT_H
#define FREQUENCY_FFT_H

#include <Arduino.h>

// FFT analysis to find maximum frequency component
float analyzeSignalFFT();

// Calculate and apply optimal sampling frequency using Nyquist theorem
void applyOptimalSamplingFrequency();

#endif // FREQUENCY_FFT_H
