"""
ATP-3 shake analysis on SIREN.CSV.
Run from atp_3/:  python shake_siren.py
Pass: PSD peak at 1.0 +/- 0.1 Hz AND SNR >= 10 dB
"""

import sys
sys.stdout.reconfigure(encoding='utf-8')

import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from scipy import signal

# ── Load ──────────────────────────────────────────────────────
df = pd.read_csv('SIREN.CSV', comment='#')
df = df.apply(pd.to_numeric, errors='coerce').dropna()

t   = df['timestamp_ms'] / 1000
azf = df['az_filt']
fs  = round(len(df) / t.max())

print("=" * 52)
print("  ATP-3 SHAKE TEST  (SIREN.CSV)")
print("=" * 52)
print(f"  Samples:      {len(df)}")
print(f"  Duration:     {t.max():.1f} s")
print(f"  Sample rate:  {fs} Hz")

# ── PSD ───────────────────────────────────────────────────────
# Skip first 5s for filter warm-up
azf_analysis = azf[t > 5]

freqs, psd = signal.welch(azf_analysis.dropna().values, fs=fs, nperseg=256)
psd_db     = 10 * np.log10(psd)

# Peak restricted to 0.5-2.0 Hz band
search_mask = (freqs >= 0.5) & (freqs <= 2.0)
search_psd  = np.where(search_mask, psd_db, -np.inf)
peak_idx    = np.argmax(search_psd)
peak_freq   = freqs[peak_idx]
peak_db     = psd_db[peak_idx]

# Noise floor: median of 10-40 Hz
noise_mask = (freqs > 10) & (freqs < 40)
noise_db   = np.median(psd_db[noise_mask])
snr        = peak_db - noise_db

print(f"\n  Peak frequency:  {peak_freq:.2f} Hz  (target: 1.0 +/- 0.1 Hz)")
print(f"  Peak power:      {peak_db:.1f} dB")
print(f"  Noise floor:     {noise_db:.1f} dB")
print(f"  SNR:             {snr:.1f} dB  (target: >= 10 dB)")

freq_pass = abs(peak_freq - 1.0) <= 0.1
snr_pass  = snr >= 10.0
passed    = freq_pass and snr_pass

print(f"\n  Frequency: {'PASS' if freq_pass else 'FAIL'} "
      f"({peak_freq:.2f} Hz {'within' if freq_pass else 'outside'} 1.0 +/- 0.1 Hz)")
print(f"  SNR:       {'PASS' if snr_pass else 'FAIL'} "
      f"({snr:.1f} dB {'meets' if snr_pass else 'below'} >= 10 dB)")

verdict = "PASS" if passed else "FAIL"
print(f"\n  {verdict} -- both criteria must be met")
print("=" * 52)

# ── Plot ──────────────────────────────────────────────────────
fig, axes = plt.subplots(2, 1, figsize=(12, 7))

axes[0].plot(t, azf, linewidth=0.5, color='steelblue', label='az_filt')
axes[0].axvline(5, color='gray', linestyle=':', linewidth=1,
                label='Analysis start (t=5s)')
axes[0].set_ylabel('Az filtered (m/s^2)')
axes[0].set_xlabel('Time (s)')
axes[0].set_title('ATP-3 Shake (SIREN.CSV) -- Full Time Series')
axes[0].legend(fontsize=7)
axes[0].grid(True, alpha=0.3)

axes[1].plot(freqs, psd_db, color='purple', linewidth=0.8)
axes[1].axvspan(0.5, 2.0, alpha=0.08, color='green',
                label='Search band (0.5-2.0 Hz)')
axes[1].axvline(1.0, color='green', linestyle=':', linewidth=1,
                label='Target: 1.0 Hz')
axes[1].axvline(peak_freq, color='red', linestyle='--', linewidth=1,
                label=f'Peak: {peak_freq:.2f} Hz  SNR {snr:.1f} dB')
axes[1].axhline(noise_db, color='gray', linestyle='--', linewidth=0.8,
                label=f'Noise floor ({noise_db:.1f} dB)')
axes[1].set_xlabel('Frequency (Hz)')
axes[1].set_ylabel('PSD (dB)')
axes[1].set_title('Welch PSD')
axes[1].set_xlim([0, 10])
axes[1].legend(fontsize=7)
axes[1].grid(True, alpha=0.3)

plt.suptitle(f'ATP-3 Shake (SIREN.CSV)  |  {verdict}',
             fontsize=13, fontweight='bold',
             color='green' if passed else 'red')
plt.tight_layout()
plt.savefig('shake_siren_results.png', dpi=150)
print("  Plot saved: shake_siren_results.png")
plt.show()
