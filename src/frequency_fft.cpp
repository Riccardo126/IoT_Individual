#include "frequency_fft.h"
#include "signal_generation.h"
#include "arduinoFFT.h"

// FFT analysis to find maximum frequency component
float analyzeSignalFFT() {  
  const int FFT_SIZE = 512;  // FFT size
  
  // Allocate memory dynamically on heap to avoid stack overflow
  double *vReal = (double *)malloc(FFT_SIZE * sizeof(double));
  double *vImag = (double *)malloc(FFT_SIZE * sizeof(double));
  
  if (vReal == NULL || vImag == NULL) {
    Serial.println("FFT Error: Memory allocation failed");
    if (vReal) free(vReal);
    if (vImag) free(vImag);
    return 0.0;
  }
  
  // Sample the signal at the specified FFT_SAMPLING_RATE
  for (int i = 0; i < FFT_SIZE; i++) {
    vReal[i] = signalLookup[i * (LOOKUP_SIZE / FFT_SIZE)];
    vImag[i] = 0.0;
  }
  
  const float FFT_SAMPLING_RATE = 512.0;  // FFT analysis sampling rate (Hz), defined to match FFT_SIZE for simplicity

  // Perform FFT at FFT_SAMPLING_RATE Hz
  ArduinoFFT<double> fft = ArduinoFFT<double>(vReal, vImag, FFT_SIZE, FFT_SAMPLING_RATE);
  
  fft.windowing(FFTWindow::Hamming, FFTDirection::Forward);
  fft.compute(FFTDirection::Forward);
  fft.complexToMagnitude();
  
  // Find the peak frequency (skip DC component at index 0)
  double maxMagnitude = 0.0;
  int maxIndex = 1;
  for (int i = 1; i < FFT_SIZE / 2; i++) {
    if (vReal[i] > maxMagnitude) {
      maxMagnitude = vReal[i];
      maxIndex = i;
    }
  }
  
  // Calculate frequency corresponding to the peak index
  float peakFrequency = (float)(maxIndex * FFT_SAMPLING_RATE) / FFT_SIZE;
  
  Serial.print("FFT Sampling Rate: ");
  Serial.print(FFT_SAMPLING_RATE);
  Serial.println(" Hz");
  Serial.print("FFT Peak Frequency: ");
  Serial.print(peakFrequency);
  Serial.println(" Hz");
  
  // Free allocated memory
  free(vReal);
  free(vImag);
  
  return peakFrequency;
}

// Calculate and apply optimal sampling frequency using Nyquist theorem
void applyOptimalSamplingFrequency() {
  // Get peak frequency from FFT analysis
  float peakFrequency = analyzeSignalFFT();
  
  // Calculate optimal sampling frequency using Nyquist theorem: Fs = 2 * f_max
  float optimalSamplingFreq = 5.0 * peakFrequency;
  SAMPLE_RATE = (int)optimalSamplingFreq;
  SAMPLE_INTERVAL_US = 1000000 / SAMPLE_RATE;
  phaseIncrement = LOOKUP_SIZE / SAMPLE_RATE;  // Update phase increment when SAMPLE_RATE changes
  
  Serial.print("Optimal Sampling Frequency: ");
  Serial.print(optimalSamplingFreq);
  Serial.println(" Hz");
  //Serial.print("Sampling Interval: ");
  //Serial.print(SAMPLE_INTERVAL_US);
  //Serial.println(" µs");
}
