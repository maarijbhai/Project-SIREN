"""
ATP-5 SD card data integrity validation.
Copy ATP5_DAT.CSV from SD card into this folder, then run:
    python validate.py
Pass: 60000 +/- 300 rows, zero nulls, no gaps > 15ms
"""

import sys
import os
sys.stdout.reconfigure(encoding='utf-8')

# Always search relative to the script's own folder
os.chdir(os.path.dirname(os.path.abspath(__file__)))

import glob as _glob
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt


# ── File finder (handles 8.3 SD card names) ──────────────────

def find_csv(*candidates):
    for name in candidates:
        matches = _glob.glob(name)
        if matches:
            return matches[0]
    return None


# ── Load ──────────────────────────────────────────────────────

path = find_csv('ATP5_DAT.CSV', 'atp5_dat.csv',
                'ATP5_DA.CSV',  'atp5_da.csv')

if not path:
    print("ERROR: No data file found.")
    print("  Expected ATP5_DAT.CSV from SD card in this folder.")
    raise SystemExit(1)

print("=" * 52)
print("  ATP-5 SD CARD INTEGRITY")
print("=" * 52)
print(f"  File: {path}")

df = pd.read_csv(path, comment='#')
df_numeric = df.apply(pd.to_numeric, errors='coerce')

t    = df_numeric['timestamp_ms'] / 1000
fs   = round(len(df_numeric.dropna()) / t.dropna().max())
print(f"  Rows loaded:  {len(df)}")
print(f"  Duration:     {t.max():.1f} s")
print(f"  Sample rate:  {fs} Hz")


# ── Check 1: Row count ────────────────────────────────────────

TARGET_ROWS = 60000
TOLERANCE   = 300
row_count   = len(df)
row_pass    = abs(row_count - TARGET_ROWS) <= TOLERANCE

print(f"\n  [1] Row count: {row_count}  (target: {TARGET_ROWS} +/- {TOLERANCE})")
print(f"      {'PASS' if row_pass else 'FAIL'}")


# ── Check 2: Zero nulls ───────────────────────────────────────

null_counts = df_numeric.isnull().sum()
total_nulls = null_counts.sum()
null_pass   = total_nulls == 0

print(f"\n  [2] Null values: {total_nulls}")
if not null_pass:
    for col, n in null_counts[null_counts > 0].items():
        print(f"      {col}: {n} nulls")
print(f"      {'PASS' if null_pass else 'FAIL'}")


# ── Check 3: No gaps > 15ms ───────────────────────────────────

ts    = df_numeric['timestamp_ms'].dropna()
diffs = ts.diff().dropna()
max_gap   = diffs.max()
gap_count = (diffs > 15).sum()
gap_pass  = gap_count == 0

print(f"\n  [3] Max timestamp gap: {max_gap:.1f} ms  (limit: 15 ms)")
print(f"      Gaps > 15ms: {gap_count}")
print(f"      {'PASS' if gap_pass else 'FAIL'}")


# ── Summary ───────────────────────────────────────────────────

passed = row_pass and null_pass and gap_pass
verdict = "PASS" if passed else "FAIL"

print(f"\n  {'=' * 20}")
print(f"  OVERALL  {verdict}")
print(f"  {'=' * 20}")


# ── Plot ──────────────────────────────────────────────────────

fig, axes = plt.subplots(2, 1, figsize=(12, 7))

# Timestamp gaps over time
axes[0].plot(ts.values[1:] / 1000, diffs.values,
             linewidth=0.4, color='steelblue', label='sample gap (ms)')
axes[0].axhline(15, color='red', linestyle='--', linewidth=0.8,
                label='15ms limit')
axes[0].set_ylabel('Gap (ms)')
axes[0].set_xlabel('Time (s)')
axes[0].set_title('ATP-5 -- Timestamp Gaps')
axes[0].legend(fontsize=7)
axes[0].grid(True, alpha=0.3)

# Gap histogram
axes[1].hist(diffs.values, bins=50, color='steelblue', edgecolor='none')
axes[1].axvline(15, color='red', linestyle='--', linewidth=0.8,
                label='15ms limit')
axes[1].set_xlabel('Gap (ms)')
axes[1].set_ylabel('Count')
axes[1].set_title('Gap Distribution')
axes[1].legend(fontsize=7)
axes[1].grid(True, alpha=0.3)

plt.suptitle(f'ATP-5 SD Integrity  |  {verdict}',
             fontsize=13, fontweight='bold',
             color='green' if passed else 'red')
plt.tight_layout()
plt.savefig('atp5_results.png', dpi=150)
print(f"\n  Plot saved: atp5_results.png")
plt.show()
