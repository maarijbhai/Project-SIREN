"""
ATP-8 30-minute endurance validation.
Copy atp8.csv from SD card into this folder, then run:
    python validate.py
Pass: zero SD errors, row count within 0.5% of expected, zero nulls,
      no timestamp gaps > (interval + 14) ms.
"""

import sys
import os
sys.stdout.reconfigure(encoding='utf-8')

os.chdir(os.path.dirname(os.path.abspath(__file__)))

import glob as _glob
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt


# ── File finder ───────────────────────────────────────────────

def find_csv(*candidates):
    for name in candidates:
        matches = _glob.glob(name)
        if matches:
            return matches[0]
    return None


# ── Load ──────────────────────────────────────────────────────

path = find_csv('atp8.csv', 'ATP8.CSV', 'ATP8_DAT.CSV', 'atp8_dat.csv')

if not path:
    print("ERROR: No data file found.")
    print("  Expected atp8.csv from SD card in this folder.")
    raise SystemExit(1)

print("=" * 52)
print("  ATP-8 30-MINUTE ENDURANCE VALIDATION")
print("=" * 52)
print(f"  File: {path}")

COLS = ['timestamp_ms', 'ax', 'ay', 'az_raw', 'az_filt',
        'gx', 'gy', 'gz', 'lat', 'lon']

df = pd.read_csv(path, comment='#')
df.columns = df.columns.str.strip()
df_num = df.apply(pd.to_numeric, errors='coerce')

duration_s = df_num['timestamp_ms'].dropna().max() / 1000
fs         = round(len(df_num.dropna(subset=['timestamp_ms'])) / duration_s)

print(f"  Rows loaded:  {len(df)}")
print(f"  Duration:     {duration_s:.1f} s  (target: 1800 s)")
print(f"  Sample rate:  {fs} Hz")


# ── Check 1: Row count (0.5% tolerance, firmware uses 14 ms interval) ─

FIRMWARE_INTERVAL_MS  = 14
TEST_DURATION_MS      = 1_800_000
expected_rows         = TEST_DURATION_MS // FIRMWARE_INTERVAL_MS  # 128571
tolerance             = round(expected_rows * 0.005)               # 0.5%
row_count             = len(df)
row_pass              = abs(row_count - expected_rows) <= tolerance

print(f"\n  [1] Row count: {row_count}"
      f"  (target: {expected_rows} +/- {tolerance}  [0.5%])")
print(f"      {'PASS' if row_pass else 'FAIL'}")


# ── Check 2: Zero nulls ───────────────────────────────────────

null_counts = df_num.isnull().sum()
total_nulls = null_counts.sum()
null_pass   = total_nulls == 0

print(f"\n  [2] Null values: {total_nulls}")
if not null_pass:
    for col, n in null_counts[null_counts > 0].items():
        print(f"      {col}: {n} nulls")
print(f"      {'PASS' if null_pass else 'FAIL'}")


# ── Check 3: No timestamp gaps > (interval + 14 ms headroom) ─

ts            = df_num['timestamp_ms'].dropna()
diffs         = ts.diff().dropna()
max_gap_limit = FIRMWARE_INTERVAL_MS + 14          # 28 ms
max_gap       = diffs.max()
gap_count     = (diffs > max_gap_limit).sum()
gap_pass      = gap_count == 0

print(f"\n  [3] Max timestamp gap: {max_gap:.1f} ms  (limit: {max_gap_limit} ms"
      f"  [{FIRMWARE_INTERVAL_MS} ms interval + 14 ms])")
print(f"      Gaps > {max_gap_limit} ms: {gap_count}")
print(f"      {'PASS' if gap_pass else 'FAIL'}")


# ── Check 4: GPS fix coverage (informational, not pass/fail) ─

gps_fixed     = ((df_num['lat'] != 0.0) | (df_num['lon'] != 0.0)).sum()
gps_pct       = 100 * gps_fixed / len(df_num) if len(df_num) else 0

print(f"\n  [4] GPS fix coverage: {gps_fixed}/{len(df_num)} rows  ({gps_pct:.1f}%)"
      f"  [informational]")


# ── Summary ───────────────────────────────────────────────────

passed  = row_pass and null_pass and gap_pass
verdict = "PASS" if passed else "FAIL"

print(f"\n  {'=' * 20}")
print(f"  OVERALL  {verdict}")
print(f"  {'=' * 20}")


# ── Plot ──────────────────────────────────────────────────────

t_s = ts.values / 1000

fig, axes = plt.subplots(3, 1, figsize=(13, 9))

# Timestamp gaps
axes[0].plot(t_s[1:], diffs.values,
             linewidth=0.3, color='steelblue', label='sample gap (ms)')
axes[0].axhline(max_gap_limit, color='red', linestyle='--', linewidth=0.8,
                label=f'{max_gap_limit} ms limit')
axes[0].set_ylabel('Gap (ms)')
axes[0].set_title('Timestamp Gaps')
axes[0].legend(fontsize=7)
axes[0].grid(True, alpha=0.3)

# az_raw and az_filt over the full run
az_raw  = df_num['az_raw'].values
az_filt = df_num['az_filt'].values
axes[1].plot(t_s, az_raw,  linewidth=0.2, color='steelblue', alpha=0.6, label='az_raw')
axes[1].plot(t_s, az_filt, linewidth=0.4, color='darkorange', label='az_filt')
axes[1].set_ylabel('Accel Z (m/s²)')
axes[1].set_title('IMU Signal — Full 30 min')
axes[1].legend(fontsize=7)
axes[1].grid(True, alpha=0.3)

# GPS fix over time (binary: fixed vs no fix)
fix_mask = ((df_num['lat'] != 0.0) | (df_num['lon'] != 0.0)).astype(int).values
axes[2].fill_between(t_s, fix_mask, alpha=0.5, color='green', label='GPS fix')
axes[2].set_ylim(-0.05, 1.2)
axes[2].set_yticks([0, 1])
axes[2].set_yticklabels(['No fix', 'Fix'])
axes[2].set_xlabel('Time (s)')
axes[2].set_title(f'GPS Fix  ({gps_pct:.1f}% coverage)')
axes[2].legend(fontsize=7)
axes[2].grid(True, alpha=0.3)

plt.suptitle(f'ATP-8 Endurance  |  {verdict}  |  {row_count} rows @ {fs} Hz',
             fontsize=13, fontweight='bold',
             color='green' if passed else 'red')
plt.tight_layout()
plt.savefig('atp8_results.png', dpi=150)
print(f"\n  Plot saved: atp8_results.png")
plt.show()
