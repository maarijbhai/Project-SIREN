# CLAUDE.md

This file provides guidance to Claude Code when working
with code in this repository. Read this file before
making any changes to firmware or analysis scripts.

---

## Project Overview

**SIREN** — Sea Ice Remote Environmental Node.
An IMU data logger and signal-processing project for
EEE4113F (UCT Year 4 Engineering Design 2026).

The system is a low-power autonomous buoy designed to
measure Antarctic Marginal Ice Zone (MIZ) ice floe
dynamics. It logs raw 6-DoF IMU data and GPS position
to an SD card for land-based post-processing.

Each subfolder corresponds to one Acceptance Test
Procedure (ATP). Every ATP has two parts:

1. **Firmware** (`.ino`) — Arduino sketch for STM32F401
2. **Analysis script** (`.py`) — processes CSV from SD card

---

## Hardware

### MCU — STM32F401CE Black Pill
- Use STM32 pin names (`PB12`, `PB7`) not Arduino aliases
- Programmed via Arduino IDE with STM32duino board package
- Upload method: STM32CubeProgrammer (SWD) via ST-Link V2
- Clock: 84MHz
- Flash: 256KB — keep firmware lean
- SRAM: 64KB — buffer sizes must be conservative

### IMU — Adafruit LSM9DS1
- Interface: I2C on SDA=`PB7`, SCL=`PB6`
- I2C address: `0x6A` (SDO tied to GND), try `0x6B` if fails
- Magnetometer: ALWAYS DISABLED in all sketches
- Accel range: 8G, ODR: 952Hz
- Gyro scale: 245 DPS
- Achieved sample rate: ~74Hz (not 100Hz — I2C overhead)

### GPS — u-blox NEO-6M
- Interface: UART on RX=`PA3`, TX=`PA2` (Serial2)
- Baud rate: 9600
- Runs in continuous mode — no duty cycling
- Will not get a fix indoors — always test outside

### SD Card
- Interface: SPI, CS=`PB12`
- Format: FAT32
- Data file: `siren.csv`
- Old file deleted on every run

### Power
- USB 5V via onboard 3.3V regulator
- All sensors at 3.3V logic

---

## Folder Structure

```
SIREN/
├── CLAUDE.md
├── atp_1/
│   └── atp_1.ino
├── atp_2/
│   └── atp_2.ino
├── atp_3/
│   ├── atp_3_tap.ino
│   ├── atp_3_shake.ino
│   ├── tap_analysis.py
│   └── shake_analysis.py
├── atp_4/
│   └── atp_4.ino
├── atp_5/
│   └── validate.py
├── atp_6/
│   └── atp_6.ino
├── atp_7/
│   └── atp_7.ino
├── atp_8/
│   └── atp_8.ino
└── data/
    └── *.csv
```

---

## Firmware Conventions

### Timing
- Never use `delay()` in `loop()` — always use `millis()`
- Sample interval: `SAMPLE_INTERVAL_MS = 10` (100Hz target)
- Achieved rate is ~74Hz due to I2C overhead — this is expected

### Buffering
- Buffer 100 samples in RAM then flush to SD once per second
- This prevents SD write latency from blocking the sample loop
- Struct for buffer:

```cpp
struct Sample {
  uint32_t timestamp;
  float ax, ay, az_raw, az_filt;
  float gx, gy, gz;
};
Sample buffer[BUFFER_SIZE];
uint16_t bufferIndex = 0;
```

### High-pass filter
```cpp
// Removes gravity DC offset and low-frequency drift
// Cutoff ~0.08Hz at 100Hz sample rate
const float alpha = 0.995;
az_filtered = alpha * (az_filtered + az_raw - az_prev);
az_prev     = az_raw;
```

### CSV format
```
timestamp_ms, ax, ay, az_raw, az_filt, gx, gy, gz
```

### Wire initialisation — always explicit
```cpp
Wire.setSDA(PB7);
Wire.setSCL(PB6);
Wire.begin();
```

### IMU initialisation — always include these settings
```cpp
imu.settings.mag.enabled          = false;  // always off
imu.settings.accel.enabled        = true;
imu.settings.accel.scale          = 8;      // 8G range
imu.settings.accel.sampleRate     = 6;      // 952Hz ODR
imu.settings.gyro.enabled         = true;
imu.settings.gyro.scale           = 245;
imu.settings.gyro.sampleRate      = 6;
```

### Serial
- Baud rate: always 115200
- Boot messages: `IMU OK`, `SD OK`, `GPS OK`
- Comment lines prefixed with `#`

---

## Python Analysis Conventions

### Loading CSV
```python
df = pd.read_csv('siren.csv',
                 names=['timestamp_ms','ax','ay',
                        'az_raw','az_filt',
                        'gx','gy','gz'],
                 comment='#')
df = df.apply(pd.to_numeric, errors='coerce').dropna()
```

### Sample rate — always infer, never hardcode
```python
t  = df['timestamp_ms'] / 1000
fs = round(len(df) / t.max())
# Expected: ~74 Hz not 100 Hz
```

### Welch PSD — always use .values
```python
# Must convert pandas Series to numpy array first
freqs, psd = signal.welch(azf.dropna().values,
                           fs=fs, nperseg=256)
```

### Tap detection rules
- Compute baseline RMS from settled window only (`t > 10s`)
- Threshold = 2× baseline RMS
- Events = groups of indices where consecutive gap ≤ 10 samples
- Only search for taps in the tap phase (never in shake phase)
- Tap and shake phases must always be analysed separately

### Shake test rules
- PSD computed from shake phase only
- Search for peak in 0.5–2.0 Hz band only
- SNR = peak dB minus median noise floor (10–40 Hz)
- Pass: peak at 1.0 ± 0.1 Hz AND SNR ≥ 10 dB

---

## Known Issues — Read Before Debugging

| Issue | Cause | Fix |
|-------|-------|-----|
| Sample rate ~74Hz not 100Hz | I2C overhead on STM32 | Expected — infer fs from data |
| Filter takes 10s to settle | High-pass filter cold start | Skip first 10s for baseline |
| "Tap 1 at 0.01s" is fake | Filter startup transient | Settled window must start at t>10s |
| 40+ taps detected | Shake phase mixed with tap phase | Separate tap and shake recordings |
| IMU not found | Wrong I2C address | Try 0x6B instead of 0x6A |
| SD init fails | Wrong VCC voltage | Some modules need 5V not 3.3V |
| GPS no fix | Indoors | Go outside, clear sky view |
| Welch KeyError | Pandas Series passed to scipy | Add .values before passing |

---

## ATP Pass Criteria

| ATP | What it tests | Pass criteria |
|-----|---------------|---------------|
| ATP-1 | MCU boot | All init OK, 3.3V rail 3.2–3.4V |
| ATP-2 | IMU static | Flat: 9.81±0.5 m/s², 45°: 6.94±0.5 m/s², filt <0.1 m/s² |
| ATP-3 tap | Collision detection | Spike >2×RMS, ≥2 consecutive samples |
| ATP-3 shake | Wave simulation | 1.0±0.1 Hz peak, SNR ≥10 dB |
| ATP-4 | GPS accuracy | Fix <5 min, error <10m, 60±3 sentences/min |
| ATP-5 | SD integrity | 60000±300 rows, zero nulls, no gaps >15ms |
| ATP-6 | Current draw | IMU 4–6mA, GPS 35–50mA, documented |
| ATP-7 | Beacon packet | Matches GPS, 900±5s interval, 13 bytes |
| ATP-8 | 30 min endurance | Zero errors, 180000±900 rows |

---

## Flashing Firmware

```
1. Open .ino in Arduino IDE 2.x
2. Tools → Board → STM32 boards → Generic STM32F4 series
3. Tools → Board part number → BlackPill F411CE
4. Tools → Upload method → STM32CubeProgrammer (SWD)
5. Tools → Port → select ST-Link port
6. Upload
```

### Required libraries
- `Adafruit_LSM9DS1`
- `Adafruit_Sensor`
- `TinyGPS++`
- `SD` (built-in)
- `Wire` (built-in)
- `SPI` (built-in)

---

## Running Analysis Scripts

```powershell
# Install dependencies once
pip install pandas numpy matplotlib scipy

# Copy CSV from SD card into the ATP folder
# Rename to siren.csv

# Run from inside the ATP folder
cd atp_3
python tap_analysis.py
python shake_analysis.py
```

---

## Design Decisions — Do Not Change Without Reason

- **Magnetometer disabled** — unreliable near Antarctic poles
- **Raw data only** — no onboard FFT or spectral processing
- **I2C for IMU** — chosen for prototype simplicity despite
  lower reliability than SPI for long-term deployment
- **Continuous GPS** — avoids cold start failures in freezing
  temperatures documented in SHARC V3.0
- **alpha = 0.995** — do not change, tuned for ~0.08Hz cutoff
- **8G accel range** — needed to capture collision spikes
  without clipping (was 2G originally, caused missed taps)
- **Buffer size 100** — 1 second of data at 100Hz, fits in
  64KB SRAM with headroom for other variables