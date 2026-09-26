"""
ATP-6 Current draw validation.

1. Run atp_6.ino and note the ammeter reading at each prompt.
2. Fill in atp6_measurements.csv (template below).
3. Run: python validate.py

atp6_measurements.csv format (no extra whitespace):
    state,description,current_mA
    0,MCU only,<value>
    1,MCU + IMU,<value>
    2,MCU + IMU + GPS,<value>
    3,Full system,<value>

Pass: IMU delta 4–6 mA, GPS delta 35–50 mA
"""

import sys
import os
sys.stdout.reconfigure(encoding='utf-8')

os.chdir(os.path.dirname(os.path.abspath(__file__)))

import pandas as pd
import matplotlib.pyplot as plt
import matplotlib.patches as mpatches

CSV_FILE = 'atp6_measurements.csv'

# ── Load ──────────────────────────────────────────────────

if not os.path.exists(CSV_FILE):
    print(f"ERROR: {CSV_FILE} not found.")
    print("  Create it with your ammeter readings — see script header.")
    raise SystemExit(1)

df = pd.read_csv(CSV_FILE)
df.columns = df.columns.str.strip()
df['current_mA'] = pd.to_numeric(df['current_mA'], errors='coerce')

if df['current_mA'].isnull().any():
    print("ERROR: missing or non-numeric values in current_mA column.")
    raise SystemExit(1)

if len(df) != 4:
    print(f"ERROR: expected 4 rows, found {len(df)}.")
    raise SystemExit(1)

mcu_only  = df.loc[df['state'] == 0, 'current_mA'].values[0]
mcu_imu   = df.loc[df['state'] == 1, 'current_mA'].values[0]
mcu_gps   = df.loc[df['state'] == 2, 'current_mA'].values[0]
full_sys  = df.loc[df['state'] == 3, 'current_mA'].values[0]

imu_delta = mcu_imu  - mcu_only   # IMU contribution
gps_delta = mcu_gps  - mcu_imu    # GPS contribution
sd_delta  = full_sys - mcu_gps    # SD write contribution


# ── Checks ────────────────────────────────────────────────

IMU_MIN, IMU_MAX = 4.0, 6.0
GPS_MIN, GPS_MAX = 35.0, 50.0

imu_pass = IMU_MIN <= imu_delta <= IMU_MAX
gps_pass = GPS_MIN <= gps_delta <= GPS_MAX
passed   = imu_pass and gps_pass


# ── Report ────────────────────────────────────────────────

print("=" * 52)
print("  ATP-6 CURRENT DRAW")
print("=" * 52)
print(f"  State 0  MCU only:         {mcu_only:.1f} mA")
print(f"  State 1  MCU + IMU:        {mcu_imu:.1f} mA")
print(f"  State 2  MCU + IMU + GPS:  {mcu_gps:.1f} mA")
print(f"  State 3  Full system:      {full_sys:.1f} mA")
print()

print(f"  [1] IMU delta:  {imu_delta:.1f} mA"
      f"  (target: {IMU_MIN}–{IMU_MAX} mA)")
print(f"      {'PASS' if imu_pass else 'FAIL'}")

print(f"\n  [2] GPS delta:  {gps_delta:.1f} mA"
      f"  (target: {GPS_MIN}–{GPS_MAX} mA)")
print(f"      {'PASS' if gps_pass else 'FAIL'}")

print(f"\n  [3] SD write delta:  {sd_delta:.1f} mA  (informational)")

verdict = "PASS" if passed else "FAIL"
print(f"\n  {'=' * 20}")
print(f"  OVERALL  {verdict}")
print(f"  {'=' * 20}")


# ── Plot ──────────────────────────────────────────────────

labels  = ['MCU only', 'MCU\n+IMU', 'MCU+IMU\n+GPS', 'Full\nsystem']
values  = [mcu_only, mcu_imu, mcu_gps, full_sys]
deltas  = [0, imu_delta, gps_delta, sd_delta]
colours = ['steelblue', 'steelblue', 'steelblue', 'steelblue']

fig, axes = plt.subplots(1, 2, figsize=(12, 5))

# Bar chart — total current per state
bars = axes[0].bar(labels, values, color=colours, edgecolor='none', width=0.5)
for bar, val in zip(bars, values):
    axes[0].text(bar.get_x() + bar.get_width() / 2,
                 bar.get_height() + 0.5,
                 f'{val:.1f}', ha='center', va='bottom', fontsize=9)
axes[0].set_ylabel('Current (mA)')
axes[0].set_title('Total current per state')
axes[0].grid(axis='y', alpha=0.3)
axes[0].set_ylim(0, max(values) * 1.2)

# Delta chart — component contributions
delta_labels = ['MCU\nbaseline', 'IMU\ndelta', 'GPS\ndelta', 'SD\ndelta']
delta_values = [mcu_only, imu_delta, gps_delta, sd_delta]

imu_colour = 'seagreen' if imu_pass else 'tomato'
gps_colour = 'seagreen' if gps_pass else 'tomato'
delta_colours = ['steelblue', imu_colour, gps_colour, 'orchid']

bars2 = axes[1].bar(delta_labels, delta_values,
                    color=delta_colours, edgecolor='none', width=0.5)
for bar, val in zip(bars2, delta_values):
    axes[1].text(bar.get_x() + bar.get_width() / 2,
                 bar.get_height() + 0.3,
                 f'{val:.1f}', ha='center', va='bottom', fontsize=9)

# Pass band overlays
axes[1].axhspan(IMU_MIN, IMU_MAX, alpha=0.12, color='seagreen',
                label=f'IMU target {IMU_MIN}–{IMU_MAX} mA')
axes[1].axhspan(GPS_MIN, GPS_MAX, alpha=0.08, color='gold',
                label=f'GPS target {GPS_MIN}–{GPS_MAX} mA')

axes[1].set_ylabel('Current delta (mA)')
axes[1].set_title('Component current contributions')
axes[1].grid(axis='y', alpha=0.3)
axes[1].set_ylim(0, max(delta_values) * 1.25)
axes[1].legend(fontsize=7)

plt.suptitle(f'ATP-6 Current Draw  |  {verdict}',
             fontsize=13, fontweight='bold',
             color='green' if passed else 'red')
plt.tight_layout()
plt.savefig('atp6_results.png', dpi=150)
print(f"\n  Plot saved: atp6_results.png")
plt.show()
