"""
ATP-3 analysis — tap detection and shake/wave simulation.
Copy TAP_DAT.CSV and/or SHK_DAT.CSV from the SD card into this folder, then run:
    python atp3_analysis.py
"""

import os
import sys
import glob as _glob

sys.stdout.reconfigure(encoding='utf-8')
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
from scipy import signal


# ── Helpers ───────────────────────────────────────────────────

def find_csv(*candidates):
    """Return the first existing file from a list of candidate names (case-insensitive on Windows)."""
    for name in candidates:
        matches = _glob.glob(name)
        if matches:
            return matches[0]
    return None

def load_csv(path):
    df = pd.read_csv(path, comment='#')
    df = df.apply(pd.to_numeric, errors='coerce').dropna()
    return df

def infer_fs(df):
    t = df['timestamp_ms'] / 1000
    return round(len(df) / t.max())


# ── Tap analysis ──────────────────────────────────────────────

def analyse_tap(path):
    print("\n" + "=" * 52)
    print("  ATP-3 TAP TEST")
    print("=" * 52)

    df  = load_csv(path)
    t   = df['timestamp_ms'] / 1000
    azf = df['az_filt']
    fs  = infer_fs(df)

    print(f"  Samples:      {len(df)}")
    print(f"  Duration:     {t.max():.1f} s")
    print(f"  Sample rate:  {fs} Hz")

    # Baseline from settled window only (first 10s = filter warm-up)
    settled   = azf[t > 10]
    baseline  = np.sqrt(np.mean(settled ** 2))
    threshold = 2 * baseline

    print(f"\n  Baseline RMS:  {baseline:.4f} m/s²")
    print(f"  Threshold 2×:  {threshold:.4f} m/s²")

    # Only search in settled region
    settled_mask = (t > 10).values
    above = np.where(settled_mask & (np.abs(azf) > threshold).values)[0]

    tap_events = []
    if len(above) > 0:
        group = [above[0]]
        for i in range(1, len(above)):
            if above[i] - above[i - 1] <= 10:
                group.append(above[i])
            else:
                tap_events.append(group)
                group = [above[i]]
        tap_events.append(group)

    print(f"\n  Tap events found: {len(tap_events)}")

    passed    = False
    tap_times = []
    for i, grp in enumerate(tap_events):
        peak_idx  = grp[np.argmax(np.abs(azf.iloc[grp]))]
        peak_time = t.iloc[peak_idx]
        peak_val  = azf.iloc[peak_idx]
        duration  = len(grp)
        tap_times.append(peak_time)
        ok = duration >= 2
        if ok:
            passed = True
        status = "PASS" if ok else "WARN — only 1 sample"
        print(f"    Tap {i + 1}: t={peak_time:.2f}s  "
              f"peak={peak_val:+.3f} m/s²  "
              f"samples={duration}  [{status}]")

    verdict = "PASS" if passed else "FAIL"
    reason  = (">=1 tap with >=2 consecutive samples above threshold"
               if passed else "no valid tap detected")
    print(f"\n  {verdict} — {reason}")

    # Plot
    fig, axes = plt.subplots(2, 1, figsize=(12, 7))

    axes[0].plot(t, azf, linewidth=0.5, color='steelblue', label='az_filt')
    axes[0].axhline( threshold, color='red', linestyle='--', linewidth=0.8,
                     label=f'±{threshold:.3f} m/s² (2×RMS)')
    axes[0].axhline(-threshold, color='red', linestyle='--', linewidth=0.8)
    axes[0].axvline(10, color='gray', linestyle=':', linewidth=1,
                    label='Settle boundary (t=10s)')
    for i, tt in enumerate(tap_times[:5]):   # cap legend entries
        axes[0].axvline(tt, color='orange', linestyle=':', linewidth=1.2,
                        label=f'Tap {i + 1} at {tt:.2f}s')
    axes[0].set_ylabel('Az filtered (m/s²)')
    axes[0].set_xlabel('Time (s)')
    axes[0].set_title('ATP-3 Tap — Full Time Series')
    axes[0].legend(fontsize=7)
    axes[0].grid(True, alpha=0.3)

    if tap_times:
        first = tap_times[0]
        mask  = (t >= first - 1.5) & (t <= first + 1.5)
        axes[1].plot(t[mask], azf[mask], linewidth=0.8,
                     color='darkorange', marker='.', markersize=4,
                     label='az_filt')
        axes[1].axhline( threshold, color='red', linestyle='--', linewidth=0.8)
        axes[1].axhline(-threshold, color='red', linestyle='--', linewidth=0.8)
        axes[1].axvline(first, color='orange', linestyle=':', linewidth=1.5,
                        label=f'Tap at {first:.2f}s')
        axes[1].set_title(f'Tap 1 Zoomed (±1.5s around t={first:.2f}s)')
        axes[1].legend(fontsize=7)
    else:
        axes[1].text(0.5, 0.5, 'No taps detected',
                     transform=axes[1].transAxes,
                     ha='center', va='center', fontsize=14, color='red')
        axes[1].set_title('Zoom — no taps')

    axes[1].set_ylabel('Az filtered (m/s²)')
    axes[1].set_xlabel('Time (s)')
    axes[1].grid(True, alpha=0.3)

    plt.suptitle(f'ATP-3 Tap  |  {verdict}', fontsize=13, fontweight='bold',
                 color='green' if passed else 'red')
    plt.tight_layout()
    plt.savefig('tap_results.png', dpi=150)
    print("  Plot saved: tap_results.png")
    plt.show()

    return passed


# ── Shake analysis ────────────────────────────────────────────

def analyse_shake(path):
    print("\n" + "=" * 52)
    print("  ATP-3 SHAKE TEST")
    print("=" * 52)

    df  = load_csv(path)
    t   = df['timestamp_ms'] / 1000
    azf = df['az_filt']
    fs  = infer_fs(df)

    print(f"  Samples:      {len(df)}")
    print(f"  Duration:     {t.max():.1f} s")
    print(f"  Sample rate:  {fs} Hz")

    # Skip first 5s (filter warm-up); shake starts immediately
    azf_analysis = azf[t > 5]

    freqs, psd = signal.welch(azf_analysis.dropna().values, fs=fs, nperseg=256)
    psd_db     = 10 * np.log10(psd)

    # Peak search restricted to 0.5–2.0 Hz
    search_mask = (freqs >= 0.5) & (freqs <= 2.0)
    search_psd  = np.where(search_mask, psd_db, -np.inf)
    peak_idx    = np.argmax(search_psd)
    peak_freq   = freqs[peak_idx]
    peak_db     = psd_db[peak_idx]

    # Noise floor: median of 10–40 Hz
    noise_mask = (freqs > 10) & (freqs < 40)
    noise_db   = np.median(psd_db[noise_mask])
    snr        = peak_db - noise_db

    print(f"\n  Peak frequency:  {peak_freq:.2f} Hz  (target: 1.0 ± 0.1 Hz)")
    print(f"  Peak power:      {peak_db:.1f} dB")
    print(f"  Noise floor:     {noise_db:.1f} dB")
    print(f"  SNR:             {snr:.1f} dB  (target: >= 10 dB)")

    freq_pass = abs(peak_freq - 1.0) <= 0.1
    snr_pass  = snr >= 10.0
    passed    = freq_pass and snr_pass

    print(f"\n  Frequency: {'PASS' if freq_pass else 'FAIL'} "
          f"({peak_freq:.2f} Hz {'within' if freq_pass else 'outside'} 1.0 ± 0.1 Hz)")
    print(f"  SNR:       {'PASS' if snr_pass else 'FAIL'} "
          f"({snr:.1f} dB {'>=' if snr_pass else '<'} 10 dB)")

    verdict = "PASS" if passed else "FAIL"
    print(f"\n  {verdict} — both criteria must be met")

    # Plot
    fig, axes = plt.subplots(2, 1, figsize=(12, 7))

    axes[0].plot(t, azf, linewidth=0.5, color='steelblue', label='az_filt')
    axes[0].axvline(5, color='gray', linestyle=':', linewidth=1,
                    label='Analysis start (t=5s)')
    axes[0].set_ylabel('Az filtered (m/s²)')
    axes[0].set_xlabel('Time (s)')
    axes[0].set_title('ATP-3 Shake — Full Time Series')
    axes[0].legend(fontsize=7)
    axes[0].grid(True, alpha=0.3)

    axes[1].plot(freqs, psd_db, color='purple', linewidth=0.8)
    axes[1].axvspan(0.5, 2.0, alpha=0.08, color='green',
                    label='Search band (0.5–2.0 Hz)')
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

    plt.suptitle(f'ATP-3 Shake  |  {verdict}', fontsize=13, fontweight='bold',
                 color='green' if passed else 'red')
    plt.tight_layout()
    plt.savefig('shake_results.png', dpi=150)
    print("  Plot saved: shake_results.png")
    plt.show()

    return passed


# ── Main ──────────────────────────────────────────────────────

if __name__ == '__main__':
    # SD card writes 8.3 names — accept all known variants
    TAP_CSV   = find_csv('TAP_DAT.CSV',   'tap_dat.csv',
                          'TAP_DATA.CSV',  'tap_data.csv')
    SHAKE_CSV = find_csv('SHK_DAT.CSV',   'shk_dat.csv',
                          'SHAKE_DAT.CSV', 'shake_dat.csv',
                          'SHAKE_DATA.CSV','shake_data.csv')

    if not TAP_CSV and not SHAKE_CSV:
        print("ERROR: No data found.")
        print("  Expected TAP_DAT.CSV and/or SHK_DAT.CSV from the SD card.")
        print("  Copy the file(s) into this folder and re-run.")
        raise SystemExit(1)

    if not TAP_CSV:
        print("NOTE: no tap CSV found — skipping tap test")
    if not SHAKE_CSV:
        print("NOTE: no shake CSV found — skipping shake test")

    results = {}
    if TAP_CSV:
        results['TAP']   = analyse_tap(TAP_CSV)
    if SHAKE_CSV:
        results['SHAKE'] = analyse_shake(SHAKE_CSV)

    print("\n" + "=" * 52)
    print("  ATP-3 SUMMARY")
    print("=" * 52)
    for name, passed in results.items():
        print(f"  {name:<8} {'PASS' if passed else 'FAIL'}")
    overall = all(results.values())
    print(f"  {'─' * 16}")
    print(f"  OVERALL  {'PASS' if overall else 'FAIL'}")
    print("=" * 52)
