import serial
import sys

try:
    ser = serial.Serial('COM5', 115200, timeout=0.5)
    print("Monitoring COM5...", flush=True)
    while True:
        line = ser.readline()
        if line:
            try:
                text = line.decode('utf-8', errors='replace').strip()
                print(text, flush=True)
                sys.stdout.flush()
            except:
                print(line, flush=True)
except KeyboardInterrupt:
    print("\nStopped.", flush=True)
except Exception as e:
    print(f"Error: {e}", flush=True)
finally:
    if 'ser' in dir() and ser.is_open:
        ser.close()
