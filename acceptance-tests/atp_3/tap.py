import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from scipy import signal

# ── Load data ─────────────────────────────────────────────────
df = pd.read_csv('SIREN.csv',
                 names=['timestamp_ms','ax','ay',
                        'az_raw','az_filt',
                        'gx','gy','gz'],
                 comment='#')

df = df.apply(pd.to_numeric, errors='coerce').dropna()
t   = df['timestamp_ms'] / 1000
azf = df['az_filt']

fs = round(len(df) / t.max())
print(f"Samples loaded: {len(df)}")
print(f"Duration:       {t.max():.1f}s")
print(f"Sample rate:    {fs} Hz")

# ── Find taps ─────────────────────────────────────────────────
settled   = azf[t > 10]
baseline  = np.sqrt(np.mean(settled**2))
threshold = 2 * baseline


print(f"\nBaseline RMS:   {baseline:.4f} m/s²")
print(f"Threshold (2x): {threshold:.4f} m/s²")

above = np.where(np.abs(azf) > threshold)[0]

tap_events = []
if len(above) > 0:
    group = [above[0]]
    for i in range(1, len(above)):
        if above[i] - above[i-1] <= 10:
            group.append(above[i])
        else:
            tap_events.append(group)
            group = [above[i]]
    tap_events.append(group)

print(f"\n=== TAP DETECTION ===")
print(f"Tap events found: {len(tap_events)}")
tap_times = []
for i, grp in enumerate(tap_events):
    peak_idx  = grp[np.argmax(np.abs(azf.iloc[grp]))]
    peak_time = t.iloc[peak_idx]
    peak_val  = azf.iloc[peak_idx]
    duration  = len(grp)
    tap_times.append(peak_time)
    print(f"  Tap {i+1}: t={peak_time:.2f}s  "
          f"peak={peak_val:.3f} m/s²  "
          f"duration={duration} samples")
    if duration >= 2:
        print(f"           ✅ PASS (≥2 consecutive samples)")
    else:
        print(f"           ⚠  only {duration} sample")

# ── Welch PSD ─────────────────────────────────────────────────
freqs, psd = signal.welch(azf.dropna().values,
                           fs=fs, nperseg=256)
psd_db     = 10 * np.log10(psd)

peak_idx_psd = np.argmax(psd[1:]) + 1
peak_freq    = freqs[peak_idx_psd]
peak_db      = psd_db[peak_idx_psd]
noise_db     = np.median(
    psd_db[(freqs > 10) & (freqs < 40)])
snr          = peak_db - noise_db

print(f"\n=== WELCH PSD ===")
print(f"Dominant frequency: {peak_freq:.2f} Hz")
print(f"Peak power:         {peak_db:.1f} dB")
print(f"Noise floor:        {noise_db:.1f} dB")
print(f"SNR:                {snr:.1f} dB")

# ── Plots ─────────────────────────────────────────────────────
fig, axes = plt.subplots(3, 1, figsize=(13, 10))

# Full time series
axes[0].plot(t, azf, linewidth=0.4, color='steelblue',
             label='az filtered')
axes[0].axhline(y=threshold,  color='red',
                linestyle='--', linewidth=0.8,
                label=f'2× RMS = ±{threshold:.3f} m/s²')
axes[0].axhline(y=-threshold, color='red',
                linestyle='--', linewidth=0.8)
for i, tt in enumerate(tap_times):
    axes[0].axvline(x=tt, color='orange',
                    linestyle=':', linewidth=1.2,
                    label=f'Tap {i+1} at {tt:.2f}s')
axes[0].set_ylabel('Az filtered (m/s²)')
axes[0].set_xlabel('Time (s)')
axes[0].set_title('ATP-3 — Full Time Series with Tap Detection')
axes[0].legend(fontsize=8)
axes[0].grid(True, alpha=0.3)

# Zoom around first tap
if len(tap_times) > 0:
    first_tap = tap_times[0]
    mask = (t >= first_tap - 1) & (t <= first_tap + 1)
    axes[1].plot(t[mask], azf[mask],
                 linewidth=0.8, color='darkorange',
                 marker='.', markersize=4)
    axes[1].axhline(y=threshold,  color='red',
                    linestyle='--', linewidth=0.8)
    axes[1].axhline(y=-threshold, color='red',
                    linestyle='--', linewidth=0.8)
    axes[1].axvline(x=first_tap, color='orange',
                    linestyle=':', linewidth=1.2,
                    label=f'Tap at {first_tap:.2f}s')
    axes[1].set_ylabel('Az filtered (m/s²)')
    axes[1].set_xlabel('Time (s)')
    axes[1].set_title(f'Tap 1 Zoomed — ±1s around '
                      f't={first_tap:.2f}s')
    axes[1].legend(fontsize=8)
    axes[1].grid(True, alpha=0.3)

# Full PSD
axes[2].plot(freqs, psd_db, color='purple', linewidth=0.8)
axes[2].axhline(y=noise_db, color='gray',
                linestyle='--', linewidth=0.8,
                label=f'Noise floor ({noise_db:.1f} dB)')
axes[2].axvline(x=peak_freq, color='red',
                linestyle='--', linewidth=1,
                label=f'Peak: {peak_freq:.2f} Hz '
                      f'(SNR {snr:.1f} dB)')
axes[2].set_xlabel('Frequency (Hz)')
axes[2].set_ylabel('PSD (dB)')
axes[2].set_title('Welch PSD — Full Signal')
axes[2].legend(fontsize=8)
axes[2].grid(True, alpha=0.3)
axes[2].set_xlim([0, 40])

plt.tight_layout()
plt.savefig('atp3_results.png', dpi=150)
plt.show()