"""
ATP-7 Beacon packet validation.

1. Run atp_7.ino with Serial Monitor open, capture all output to atp7_log.txt.
   Wait for at least 2 beacon transmissions (~31 min total).
2. Run: python validate.py

Checks:
  [1] Packet size   — exactly 13 bytes
  [2] Sync byte     — byte[0] == 0xBE
  [3] GPS match     — decoded lat/lon within 0.001 deg of GPS snapshot
  [4] Interval      — 900 ± 5 s between consecutive beacons (needs >= 2)

Pass: all four checks pass across all captured beacons.
"""

import sys
import os
import struct
import re

sys.stdout.reconfigure(encoding='utf-8')
os.chdir(os.path.dirname(os.path.abspath(__file__)))

import matplotlib.pyplot as plt

LOG_FILE        = 'res.txt'
INTERVAL_TARGET = 60     # s  (firmware configured for 60 s during testing)
INTERVAL_TOL    = 5      # s
COORD_TOL       = 0.001  # degrees (~100 m)


# ── Load ──────────────────────────────────────────────────

if not os.path.exists(LOG_FILE):
    print(f"ERROR: {LOG_FILE} not found.")
    print("  Capture serial output from atp_7.ino to atp7_log.txt and retry.")
    raise SystemExit(1)

with open(LOG_FILE, 'r') as fh:
    lines = fh.readlines()

gps_re    = re.compile(r'#\s*GPS\s+lat=([-\d.]+)\s+lon=([-\d.]+)')
beacon_re = re.compile(r'^BEACON\s+ts=(\d+)\s+([0-9A-Fa-f ]+)')


# ── Parse — pair each BEACON with the GPS snapshot above it ──

records  = []   # {ts, raw, gps_lat, gps_lon}
last_gps = None

for line in lines:
    m = gps_re.search(line)
    if m:
        last_gps = (float(m.group(1)), float(m.group(2)))
        continue

    m = beacon_re.match(line.strip())
    if m:
        ts  = int(m.group(1))
        raw = bytes(int(b, 16) for b in m.group(2).strip().split())
        records.append({
            'ts':      ts,
            'raw':     raw,
            'gps_lat': last_gps[0] if last_gps else None,
            'gps_lon': last_gps[1] if last_gps else None,
        })

print("=" * 52)
print("  ATP-7 BEACON PACKET")
print("=" * 52)
print(f"  Log file:       {LOG_FILE}")
print(f"  Beacons found:  {len(records)}")

if len(records) == 0:
    print("\n  ERROR: no BEACON lines found in log.")
    print("  Make sure atp_7.ino output was captured correctly.")
    raise SystemExit(1)


# ── Per-beacon checks ─────────────────────────────────────

size_ok_all     = []
sync_ok_all     = []
match_ok_all    = []
interval_all    = []   # (dt_s, pass_bool) for each consecutive pair

for i, rec in enumerate(records):
    raw = rec['raw']

    size_ok = (len(raw) == 13)
    sync_ok = size_ok and (raw[0] == 0xBE)

    pkt_lat = pkt_lon = None
    if len(raw) >= 9:
        pkt_lat = struct.unpack_from('<f', raw, 1)[0]
        pkt_lon = struct.unpack_from('<f', raw, 5)[0]

    if rec['gps_lat'] is not None and pkt_lat is not None:
        lat_err  = abs(pkt_lat - rec['gps_lat'])
        lon_err  = abs(pkt_lon - rec['gps_lon'])
        match_ok = (lat_err <= COORD_TOL) and (lon_err <= COORD_TOL)
    else:
        lat_err = lon_err = match_ok = None

    size_ok_all.append(size_ok)
    sync_ok_all.append(sync_ok)
    match_ok_all.append(match_ok)

    if i > 0:
        dt      = records[i]['ts'] - records[i - 1]['ts']
        iv_pass = abs(dt - INTERVAL_TARGET) <= INTERVAL_TOL
        interval_all.append((dt, iv_pass))

    # Per-beacon summary
    print(f"\n  Beacon {i + 1}  ts={rec['ts']} s")
    print(f"    bytes:    {len(raw)}  {'PASS' if size_ok else 'FAIL'}")
    print(f"    sync:     0x{raw[0]:02X}  {'PASS' if sync_ok else 'FAIL'}")
    if pkt_lat is not None:
        print(f"    decoded:  lat={pkt_lat:.6f}  lon={pkt_lon:.6f}")
    if match_ok is not None:
        print(f"    GPS match: lat_err={lat_err:.6f}°  lon_err={lon_err:.6f}°  "
              f"(tol {COORD_TOL}°)  {'PASS' if match_ok else 'FAIL'}")
    else:
        print(f"    GPS match: SKIP — no GPS snapshot in log for this beacon")
    if i > 0:
        dt_val, iv_ok = interval_all[-1]
        print(f"    interval: {dt_val} s  "
              f"(target {INTERVAL_TARGET}±{INTERVAL_TOL} s)  "
              f"{'PASS' if iv_ok else 'FAIL'}")


# ── Overall verdict ───────────────────────────────────────

check1 = all(size_ok_all)
check2 = all(sync_ok_all)
check3 = all(v for v in match_ok_all if v is not None) if any(
         v is not None for v in match_ok_all) else None
check4 = all(r[1] for r in interval_all) if interval_all else None

print(f"\n  [1] Packet size (13 bytes):  {'PASS' if check1 else 'FAIL'}")
print(f"  [2] Sync byte (0xBE):        {'PASS' if check2 else 'FAIL'}")

if check3 is None:
    print(f"  [3] GPS coordinate match:    SKIP — no GPS snapshots in log")
else:
    print(f"  [3] GPS coordinate match:    {'PASS' if check3 else 'FAIL'}")

if check4 is None:
    print(f"  [4] Interval (900±5 s):      SKIP — need >= 2 beacons")
else:
    print(f"  [4] Interval (900±5 s):      {'PASS' if check4 else 'FAIL'}")

checks = [check1, check2,
          check3 if check3 is not None else True,
          check4 if check4 is not None else True]
passed  = all(checks)
verdict = "PASS" if passed else "FAIL"
if check4 is None:
    verdict += "  [incomplete — run until >= 2 beacons]"

print(f"\n  {'=' * 20}")
print(f"  OVERALL  {verdict}")
print(f"  {'=' * 20}")


# ── Plot ──────────────────────────────────────────────────

fig, axes = plt.subplots(1, 2, figsize=(12, 5))

# ── Panel 1: intervals ────────────────────────────────────
if interval_all:
    labels  = [f'B{i+1}→B{i+2}' for i in range(len(interval_all))]
    dts     = [r[0] for r in interval_all]
    colours = ['seagreen' if r[1] else 'tomato' for r in interval_all]

    axes[0].bar(labels, dts, color=colours, edgecolor='none', width=0.4)
    axes[0].axhline(INTERVAL_TARGET, color='black', linestyle='--',
                    linewidth=0.8, label=f'{INTERVAL_TARGET} s target')
    axes[0].axhspan(INTERVAL_TARGET - INTERVAL_TOL,
                    INTERVAL_TARGET + INTERVAL_TOL,
                    alpha=0.15, color='seagreen',
                    label=f'±{INTERVAL_TOL} s band')
    for label, val in zip(labels, dts):
        axes[0].text(labels.index(label), val + 0.5, f'{val} s',
                     ha='center', va='bottom', fontsize=9)
    axes[0].set_ylabel('Interval (s)')
    axes[0].set_ylim(INTERVAL_TARGET - 15, INTERVAL_TARGET + 15)
    axes[0].legend(fontsize=8)
    axes[0].grid(axis='y', alpha=0.3)
else:
    axes[0].text(0.5, 0.5, 'Need ≥ 2 beacons\nfor interval check',
                 ha='center', va='center', transform=axes[0].transAxes,
                 fontsize=11, color='grey')
axes[0].set_title('Beacon intervals')

# ── Panel 2: GPS vs decoded coordinates ──────────────────
paired = [(rec['gps_lat'], rec['gps_lon'],
           struct.unpack_from('<f', rec['raw'], 1)[0],
           struct.unpack_from('<f', rec['raw'], 5)[0])
          for rec in records
          if rec['gps_lat'] is not None and len(rec['raw']) >= 9]

if paired:
    gps_lats  = [p[0] for p in paired]
    gps_lons  = [p[1] for p in paired]
    pkt_lats  = [p[2] for p in paired]
    pkt_lons  = [p[3] for p in paired]

    axes[1].scatter(gps_lons, gps_lats, marker='+', s=140,
                    color='steelblue', zorder=3, label='GPS report')
    axes[1].scatter(pkt_lons, pkt_lats, marker='o', s=70,
                    facecolors='none', edgecolors='tomato', linewidths=1.4,
                    zorder=4, label='Beacon decoded')
    axes[1].set_xlabel('Longitude (°)')
    axes[1].set_ylabel('Latitude (°)')
    axes[1].legend(fontsize=8)
    axes[1].grid(alpha=0.3)
else:
    axes[1].text(0.5, 0.5, 'No GPS snapshots in log\nto compare coordinates',
                 ha='center', va='center', transform=axes[1].transAxes,
                 fontsize=11, color='grey')
axes[1].set_title('GPS report vs beacon decoded')

verdict_clean = verdict.split('[')[0].strip()
colour = 'green' if verdict_clean == 'PASS' else ('darkorange' if 'incomplete' in verdict else 'red')
plt.suptitle(f'ATP-7 Beacon Packet  |  {verdict_clean}',
             fontsize=13, fontweight='bold', color=colour)
plt.tight_layout()
plt.savefig('atp7_results.png', dpi=150)
print(f"\n  Plot saved: atp7_results.png")
plt.show()
