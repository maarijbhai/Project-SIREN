import numpy as np
import matplotlib.pyplot as plt
from scipy import signal

# ── Time base ──────────────────────────────────────────────────
fs_imu = 100        # IMU sample rate (Hz)
fs_gps = 1          # GPS sample rate (Hz)
duration = 60       # seconds to simulate

t_imu = np.arange(0, duration, 1/fs_imu)
t_gps = np.arange(0, duration, 1/fs_gps)

# ── Simulate IMU — accelerometer (m/s²) ───────────────────────
# Ice floe in MIZ: slow drift + wave oscillation + sensor noise

# Wave component — dominant wave period ~10-15s in MIZ
wave_freq = 0.1         # Hz (10 second period)
wave_amp  = 0.8         # m/s² (moderate swell)

# Inertial oscillation — ~13hr period from Alberello
inertial_freq = 1/46800 # Hz
inertial_amp  = 0.05    # m/s²

# Sensor noise — MEMS noise floor
noise_std = 0.02        # m/s²

# Vertical heave (Z axis) — what matters for wave elevation
az = (wave_amp   * np.sin(2 * np.pi * wave_freq    * t_imu) +
      inertial_amp * np.sin(2 * np.pi * inertial_freq * t_imu) +
      np.random.normal(0, noise_std, len(t_imu)))

# Horizontal axes — smaller amplitude, mostly noise + drift
ax = (0.1 * np.sin(2 * np.pi * 0.08 * t_imu) +
      np.random.normal(0, noise_std, len(t_imu)))

ay = (0.1 * np.sin(2 * np.pi * 0.12 * t_imu) +
      np.random.normal(0, noise_std, len(t_imu)))

# ── Simulate IMU — gyroscope (deg/s) ──────────────────────────
# Floe tilts slowly with waves
gx = 2.0 * np.sin(2 * np.pi * wave_freq * t_imu) + \
     np.random.normal(0, 0.05, len(t_imu))
gy = 1.5 * np.sin(2 * np.pi * wave_freq * t_imu + 0.3) + \
     np.random.normal(0, 0.05, len(t_imu))
gz = np.random.normal(0, 0.02, len(t_imu))  # yaw — minimal

# ── Simulate GPS position ─────────────────────────────────────
# Start near Antarctic MIZ — 62.8°S, 29.8°E (Alberello deployment)
lat0 = -62.8
lon0 =  29.8

# Drift at ~0.3 m/s converted to degrees/second
# 1 degree lat ≈ 111km, 1 degree lon ≈ 111km * cos(lat)
drift_speed_ms   = 0.3          # m/s typical drift
drift_direction  = np.radians(45)  # NE drift

lat_rate = (drift_speed_ms * np.cos(drift_direction)) / 111000
lon_rate = (drift_speed_ms * np.sin(drift_direction)) / \
           (111000 * np.cos(np.radians(lat0)))

lat = lat0 + lat_rate * t_gps + \
      np.random.normal(0, 1e-5, len(t_gps))   # GPS noise
lon = lon0 + lon_rate * t_gps + \
      np.random.normal(0, 1e-5, len(t_gps))

# ── Simulate BME280 — environmental ──────────────────────────
t_env = np.arange(0, duration, 60)  # once per minute

temp_air  = -15 + 2 * np.sin(2 * np.pi * t_env / 3600) + \
             np.random.normal(0, 0.3, len(t_env))   # °C
pressure  = 980 + 5 * np.sin(2 * np.pi * t_env / 7200) + \
             np.random.normal(0, 0.1, len(t_env))   # hPa
humidity  = 85  + 3 * np.sin(2 * np.pi * t_env / 3600) + \
             np.random.normal(0, 1.0, len(t_env))   # %RH

# ── Simulate floe-floe collision event ───────────────────────
# Sudden spike in acceleration at t=30s
collision_idx = int(30 * fs_imu)
az[collision_idx:collision_idx+10] += \
    np.array([3, 5, 4, 2, 1, 0.5, 0.2, 0.1, 0.05, 0.02])

# ── Plot ──────────────────────────────────────────────────────
fig, axes = plt.subplots(4, 1, figsize=(12, 10))
fig.suptitle('SIREN Sensor Simulation — Antarctic MIZ', fontsize=14)

axes[0].plot(t_imu, az, linewidth=0.5, color='steelblue')
axes[0].set_ylabel('Accel Z (m/s²)')
axes[0].set_title('IMU — Vertical Acceleration (wave + collision at 30s)')
axes[0].axvline(x=30, color='red', linestyle='--',
                linewidth=0.8, label='Collision event')
axes[0].legend(fontsize=8)

axes[1].plot(t_imu, gx, linewidth=0.5, color='darkorange', label='Gx')
axes[1].plot(t_imu, gy, linewidth=0.5, color='green',      label='Gy')
axes[1].set_ylabel('Gyro (deg/s)')
axes[1].set_title('IMU — Angular Velocity')
axes[1].legend(fontsize=8)

axes[2].plot(t_gps, lat, marker='.', markersize=2,
             linewidth=0.5, color='purple', label='Latitude')
axes[2].set_ylabel('Latitude (°)')
axes[2].set_title('GPS — Floe Drift Track')

axes[3].plot(t_env, temp_air, marker='o', markersize=3,
             linewidth=1, color='red',   label='Temp (°C)')
axes[3].plot(t_env, pressure/100, marker='s', markersize=3,
             linewidth=1, color='blue',  label='Pressure/100 (hPa)')
axes[3].set_ylabel('Value')
axes[3].set_title('BME280 — Environmental Conditions')
axes[3].legend(fontsize=8)
axes[3].set_xlabel('Time (s)')

plt.tight_layout()
plt.savefig('siren_simulation.png', dpi=150)
plt.show()

# ── Spectral analysis of vertical acceleration ────────────────
freqs, psd = signal.welch(az, fs=fs_imu, nperseg=512)

plt.figure(figsize=(10, 4))
plt.semilogy(freqs, psd, color='steelblue')
plt.xlabel('Frequency (Hz)')
plt.ylabel('Power Spectral Density')
plt.title('Wave Spectrum from Vertical Acceleration (Welch method)')
plt.axvline(x=wave_freq, color='red', linestyle='--',
            label=f'Dominant wave freq ({wave_freq} Hz)')
plt.legend()
plt.xlim([0, 0.5])
plt.tight_layout()
plt.savefig('siren_spectrum.png', dpi=150)
plt.show()

print("Simulation complete.")
print(f"IMU samples:  {len(t_imu)} at {fs_imu}Hz")
print(f"GPS fixes:    {len(t_gps)} at {fs_gps}Hz")
print(f"Env samples:  {len(t_env)} (1/min)")