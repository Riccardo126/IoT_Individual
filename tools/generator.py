"""
Generates signals and sends over serial using binary protocol
Scales samples to 12-bit ADC range (0-4095)
Binary Protocol: 921600 baud for maximum throughput

Binary Format (3 bytes per sample):
  [0xAA] [VALUE_LOW] [VALUE_HIGH]

With 921600 baud: achieves ~9,216 samples/sec (vs ~1,645 at 115200)

Commands (ASCII):
  START\n  - Begin transmission
  END\n    - End transmission
  PLOT\n   - Enable SerialPlotter output

Transmission: CONTINUOUS LOOP (repeats until stopped)

Interactive controls (while transmitting):
- Press SPACE to pause/resume transmission
- Press 's' to stop and exit
- Press 'q' to quit immediately
"""
import numpy as np
import serial
import time
import sys
import threading

# Serial configuration
PORT = '/dev/ttyUSB0'  # Change to 'COM3' or your ESP32's port on Windows
BAUD_RATE = 921600  # High-speed binary protocol

# Signal parameters
sps = 50000  # Samples per second (10 kHz - match ESP32 ADC speed)
duration_s = 5.0  # Duration

# Attenuation to scale signal to 12-bit ADC range (0-4095)
# Sine wave ranges from -1 to 1, scale to 0-4095
atten = 2047  # Half of 4095 to center at ~2048

def generate_sine_wave(sps, freq_hz, duration_s, atten):
    """Generate a sine wave"""
    each_sample_number = np.arange(duration_s * sps)
    waveform = np.sin(2 * np.pi * each_sample_number * freq_hz / sps)
    waveform_scaled = (waveform * atten + 2048).astype(np.uint16)
    return waveform_scaled, f"{freq_hz} Hz sine wave"

def generate_square_wave(sps, freq_hz, duration_s, atten):
    """Generate a square wave"""
    each_sample_number = np.arange(duration_s * sps)
    period = sps / freq_hz
    waveform = np.sign(np.sin(2 * np.pi * each_sample_number / period))
    waveform_scaled = (waveform * atten + 2048).astype(np.uint16)
    return waveform_scaled, f"{freq_hz} Hz square wave"

def generate_chirp_wave(sps, freq_start, freq_end, duration_s, atten):
    """Generate a chirp (frequency sweep)"""
    each_sample_number = np.arange(duration_s * sps)
    # Linear frequency sweep from freq_start to freq_end
    frequency = np.linspace(freq_start, freq_end, len(each_sample_number))
    phase = 2 * np.pi * np.cumsum(frequency) / sps
    waveform = np.sin(phase)
    waveform_scaled = (waveform * atten + 2048).astype(np.uint16)
    return waveform_scaled, f"Chirp sweep {freq_start}-{freq_end} Hz"

def generate_noisy_signal(sps, duration_s, atten, noise_sigma=0.2, anomaly_prob=0.02):
    """Generate a multi-component signal with noise and anomalies
    
    s(t) = 2*sin(2*pi*3*t) + 4*sin(2*pi*5*t) + n(t) + A(t)
    
    Components:
    - 3 Hz sine: amplitude 2
    - 5 Hz sine: amplitude 4
    - n(t): Gaussian noise with sigma=noise_sigma (baseline sensor noise)
    - A(t): Sparse anomaly injection with probability anomaly_prob per sample
             magnitude uniformly distributed in [5, 15] with random sign
             (models hardware faults, EMI interference, transients)
    """
    each_sample_number = np.arange(duration_s * sps)
    
    # Base signal: two frequency components
    signal_3hz = 2 * np.sin(2 * np.pi * 3 * each_sample_number / sps)
    signal_5hz = 4 * np.sin(2 * np.pi * 5 * each_sample_number / sps)
    base_signal = signal_3hz + signal_5hz
    
    # Add Gaussian noise (baseline sensor noise)
    noise = np.random.normal(0, noise_sigma, len(each_sample_number))
    
    # Add anomalies (sparse random spikes for hardware faults/EMI)
    anomalies = np.zeros(len(each_sample_number))
    num_anomalies = 0
    for i in range(len(each_sample_number)):
        if np.random.random() < anomaly_prob:
            # Random magnitude between 5 and 15, random sign
            magnitude = np.random.uniform(5, 15)
            sign = np.random.choice([-1, 1])
            anomalies[i] = sign * magnitude
            num_anomalies += 1
    
    # Combine all components
    combined_signal = base_signal + noise + anomalies
    
    # Scale to 12-bit ADC range (0-4095)
    # Clip to reasonable range to avoid extreme outliers dominating the scale
    # Keep anomalies visible but don't let them flatten the main signal
    combined_signal_clipped = np.clip(combined_signal, -20, 20)
    
    # Scale from [-20, 20] to [0, 4095]
    waveform_scaled = ((combined_signal_clipped / 20) * atten + 2048).astype(np.uint16)
    waveform_scaled = np.clip(waveform_scaled, 0, 4095)  # Ensure in valid range
    
    signal_desc = (f"Multi-component noisy signal (3+5 Hz + noise σ={noise_sigma}, "
                   f"{num_anomalies} anomalies)")
    return waveform_scaled, signal_desc

def show_signal_menu():
    """Display signal selection menu"""
    print("\n" + "="*50)
    print("SELECT SIGNAL TYPE")
    print("="*50)
    print("1. Sine Wave (1 kHz)")
    print("2. Square Wave (1 kHz)")
    print("3. Chirp Sweep (500 Hz → 2 kHz)")
    print("4. Noisy Multi-Component (3+5 Hz + noise + anomalies)")
    print("="*50)
    
    while True:
        try:
            choice = input("Enter choice (1-4): ").strip()
            if choice in ['1', '2', '3', '4']:
                return choice
            else:
                print("Invalid choice. Please enter 1, 2, 3, or 4.")
        except KeyboardInterrupt:
            print("\nExiting...")
            sys.exit(0)

# Control flags
is_paused = False
should_stop = False
should_restart = False

# Binary protocol constants
FRAME_MARKER = 0xAA  # Marks start of each sample frame

def get_keyboard_input():
    """Listen for keyboard input in a separate thread"""
    global is_paused, should_stop, should_restart
    
    import select
    import sys
    
    print("\n[Controls] SPACE=pause/resume | s=stop | q=quit")
    print("           (Transmission loops continuously)\n")
    
    while not should_stop:
        if sys.stdin in select.select([sys.stdin], [], [], 0)[0]:
            key = sys.stdin.read(1).lower()
            
            if key == ' ':
                is_paused = not is_paused
                status = "PAUSED" if is_paused else "RESUMED"
                print(f"\n>>> Transmission {status}\n")
            elif key == 's':
                should_stop = True
                print("\n>>> Stop requested\n")
            elif key == 'q':
                should_stop = True
                print("\n>>> Quit requested\n")
        
        time.sleep(0.1)

try:
    # Show signal selection menu
    signal_choice = show_signal_menu()
    
    # Open serial connection
    ser = serial.Serial(PORT, BAUD_RATE, timeout=1)
    print(f"\nConnected to ESP32 on {PORT} at {BAUD_RATE} baud")
    time.sleep(2)  # Wait for ESP32 to initialize
    
    # Generate waveform based on selection
    if signal_choice == '1':
        waveform_scaled, signal_desc = generate_sine_wave(sps, 1000, duration_s, atten)
    elif signal_choice == '2':
        waveform_scaled, signal_desc = generate_square_wave(sps, 1000, duration_s, atten)
    elif signal_choice == '3':
        waveform_scaled, signal_desc = generate_chirp_wave(sps, 500, 2000, duration_s, atten)
    elif signal_choice == '4':
        waveform_scaled, signal_desc = generate_noisy_signal(sps, duration_s, atten)
    
    print(f"Generated {len(waveform_scaled)} samples at {sps} Hz")
    print(f"Signal: {signal_desc}")
    print(f"Sample range: {waveform_scaled.min()}-{waveform_scaled.max()}")
    
    # Start keyboard input thread
    keyboard_thread = threading.Thread(target=get_keyboard_input, daemon=True)
    keyboard_thread.start()
    
    # Wait for user to start
    input("\nPress ENTER to start transmission...")
    print("\nStarting transmission...\n")
    
    # Continuous transmission loop
    transmission_count = 0
    print("Continuous transmission started. Press 's' or 'q' to stop.\n")
    
    while not should_stop:
        transmission_count += 1
        print(f">>> Transmission cycle #{transmission_count}")
        transmission_start = time.time()
        
        for i in range(len(waveform_scaled)):
            # Check for stop flag
            if should_stop:
                break
            
            # Handle pause
            while is_paused and not should_stop:
                time.sleep(0.1)
            
            if should_stop:
                break
            
            sample = waveform_scaled[i]
            
            # Send sample as binary: [0xAA][LOW][HIGH]
            # Little-endian format for 16-bit value
            frame = bytes([FRAME_MARKER, sample & 0xFF, (sample >> 8) & 0xFF])
            ser.write(frame)
            
            # Print progress every 1000 samples
            if (i + 1) % 1000 == 0:
                elapsed = time.time() - transmission_start
                print(f"  Sent {i + 1}/{len(waveform_scaled)} samples ({elapsed:.2f}s)")
            
            # Maintain timing (optional - can be commented out for faster testing)
            elapsed = time.time() - transmission_start
            expected_time = (i + 1) / sps
            if expected_time > elapsed:
                time.sleep(expected_time - elapsed)
        
        # Completed one cycle, print summary and restart
        if not should_stop:
            cycle_time = time.time() - transmission_start
            print(f"  Cycle complete in {cycle_time:.2f}s. Looping...\n")
            # Automatically loop to next cycle
    
    # Send end marker when finally stopping
    ser.write(b"END\n")
    print(f"\n>>> Transmission stopped after {transmission_count} cycle(s)")
    
    time.sleep(1)
    ser.close()
    print("Serial connection closed")
    
except FileNotFoundError:
    print(f"ERROR: Serial port {PORT} not found!")
    print("Available ports: /dev/ttyUSB* or COM* (Windows)")
    sys.exit(1)
except serial.SerialException as e:
    print(f"Serial error: {e}")
    sys.exit(1)
except Exception as e:
    print(f"Unexpected error: {e}")
    sys.exit(1)
