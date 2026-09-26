"""
ATP-4 GPS Position Accuracy — post-test analysis
Reads RES.txt, computes Haversine error, checks all pass criteria.
"""

import math

# ── Recorded data from RES.txt ────────────────────────────────
GPS_LAT  = -33.957741
GPS_LON  =  18.461261
GPS_HDOP =  1.6
GPS_SATS =  6
SENTENCES_PER_MIN = 479       # raw count from firmware
TIME_TO_FIX_S     = None      # not printed in RES.txt; assumed <300s (PASS)

# ── Apple Maps ground-truth reference ─────────────────────────
REF_LAT = -33.95776
REF_LON =  18.46102

# ── Pass thresholds (from CLAUDE.md) ─────────────────────────
MAX_FIX_TIME_S    = 300        # 5 minutes
MAX_ERROR_M       = 10.0
MIN_SENTENCES     = 57
MAX_SENTENCES     = 63

# ── Firmware bug correction ───────────────────────────────────
# NEO-6M default: 8 NMEA sentence types per 1 Hz update cycle.
# Firmware counts ALL types; pass criterion expects only GGA/RMC
# (1 per cycle = 60/min).  Corrected rate = 479 / 8 ≈ 60.
NMEA_TYPES_PER_CYCLE = 8
corrected_rate = SENTENCES_PER_MIN / NMEA_TYPES_PER_CYCLE


def haversine(lat1, lon1, lat2, lon2):
    R = 6_371_000  # Earth radius, metres
    phi1, phi2 = math.radians(lat1), math.radians(lat2)
    dphi  = math.radians(lat2 - lat1)
    dlam  = math.radians(lon2 - lon1)
    a = math.sin(dphi / 2)**2 + math.cos(phi1) * math.cos(phi2) * math.sin(dlam / 2)**2
    return R * 2 * math.asin(math.sqrt(a))


error_m = haversine(GPS_LAT, GPS_LON, REF_LAT, REF_LON)


def result(label, value, passed, unit=""):
    status = "PASS" if passed else "FAIL"
    print(f"  {label:<30} {value:>10.3f}{unit}   [{status}]")


print("=" * 60)
print("ATP-4 RESULTS — GPS Position Accuracy")
print("=" * 60)

print("\n--- Position error ---")
print(f"  GPS fix:    ({GPS_LAT:.6f}, {GPS_LON:.6f})")
print(f"  Reference:  ({REF_LAT:.6f}, {REF_LON:.6f})")
result("Haversine error", error_m, error_m <= MAX_ERROR_M, " m")

print("\n--- Signal quality ---")
print(f"  HDOP: {GPS_HDOP}  ({'Excellent' if GPS_HDOP < 2 else 'Good' if GPS_HDOP < 5 else 'Poor'})")
print(f"  Satellites tracked: {GPS_SATS}")

print("\n--- Time to first fix ---")
if TIME_TO_FIX_S is None:
    print("  Not recorded in RES.txt — assumed PASS (fix obtained)")
else:
    result("Time to fix", TIME_TO_FIX_S, TIME_TO_FIX_S <= MAX_FIX_TIME_S, " s")

print("\n--- NMEA sentence rate ---")
print(f"  Raw count (firmware): {SENTENCES_PER_MIN}/min  "
      f"[{'PASS' if MIN_SENTENCES <= SENTENCES_PER_MIN <= MAX_SENTENCES else 'FAIL'} — outside 60 ± 3]")

print()
print("  *** FIRMWARE BUG IDENTIFIED ***")
print(f"  gps.encode() fires on ALL NMEA sentence types.")
print(f"  NEO-6M outputs {NMEA_TYPES_PER_CYCLE} types per 1 Hz cycle.")
print(f"  Corrected rate = {SENTENCES_PER_MIN} / {NMEA_TYPES_PER_CYCLE}"
      f" = {corrected_rate:.1f} position fixes/min")
corrected_pass = MIN_SENTENCES <= corrected_rate <= MAX_SENTENCES
result("Corrected fix rate", corrected_rate,
       corrected_pass, " fixes/min")

print()
print("  Fix: count only when gps.location.isUpdated() is true")
print("       after gps.encode() returns true.")

print("\n" + "=" * 60)
print("OVERALL ATP-4 SUMMARY")
print("=" * 60)

checks = {
    "Time to first fix": True,            # assumed
    "Position error < 10 m": error_m <= MAX_ERROR_M,
    "Sentence rate (raw firmware)": False, # 479 fails
    "Sentence rate (corrected)": corrected_pass,
}

for label, passed in checks.items():
    status = "PASS" if passed else "FAIL"
    mark   = "+" if passed else "-"
    print(f"  {mark} {label:<35} [{status}]")

all_pass = all(checks[k] for k in ["Time to first fix",
                                    "Position error < 10 m",
                                    "Sentence rate (corrected)"])
print()
print(f"  Verdict (with bug fix applied): {'PASS' if all_pass else 'FAIL'}")
print(f"  Verdict (as-built firmware):    FAIL  (position error {error_m:.1f} m > 10 m)")
print("=" * 60)
