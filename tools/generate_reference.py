#!/usr/bin/env python
"""
Generate the libigrf regression reference datasets in tests/reference/
using the ppigrf reference implementation (IAGA-VMOD compatible).

The datasets cover, per the libigrf implementation specification:
  - times: exact epochs and mid-interval times across 1900..2030,
    including both sides of the 2025 epoch and the 2030 endpoint
  - positions: poles (near/exact), equator, Greenwich, date line,
    longitude-normalization cases (360, 270, >180, negative), and a
    spread of global locations at 0..50000 km altitude
  - both geodetic (X/Y/Z) and geocentric (Br/Btheta/Bphi) output

Shared time convention
----------------------
Both implementations interpolate the Gauss coefficients linearly on
the uniform elapsed-time axis: libigrf maps the decimal year to days
with the 365/366 day-count (the exact inverse of its
igrf_datetime_to_decimal_year), ppigrf maps it to a timestamp with
its own yearfrac_to_datetime.  The two mappings agree exactly, so
this generator evaluates ppigrf directly at the timestamp of each
requested decimal year; any difference is due to the field
computation itself, not the time convention.  (libigrf adopted this
convention on 2026-11-07, superseding its original decimal-year
interpolation; see docs/DECIMAL_YEAR_VS_TIMESTAMP.md.)

Exact poles: at theta = 0 (geodetic lat = +90) ppigrf produces NaN
for the eastward component (a literal 0/0 in its longitude sum), so
the geodetic grid uses lat = +/-89.9 near the north pole; the exact
south pole (theta = 180) is fine in ppigrf and included.

Run from the repository root:
    python tools/generate_reference.py
"""
import os
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "ppigrf-main", "src"))

import pandas as pd  # noqa: E402

import ppigrf  # noqa: E402
from ppigrf.ppigrf import read_shc, yearfrac_to_datetime  # noqa: E402

REF_DIR = os.path.join(ROOT, "tests", "reference")

MODELS = {
    "igrf14": os.path.join(ROOT, "data", "igrf14.shc"),
    "igrf13": os.path.join(ROOT, "data", "igrf13.shc"),
}

# Decimal years: every third epoch, mid-interval points, both sides of
# 2025 and the 2030 endpoint.
TIMES_IGRF14 = [
    1900.0, 1902.5, 1905.0,
    1950.0, 1952.5, 1955.0,
    2000.0, 2002.5, 2005.0,
    2010.0, 2020.0, 2022.5,
    2025.0, 2025.5, 2027.0,
    2027.5,  # mid-interval in 2025-2030 (contains leap year 2028):
             # discriminates elapsed-time from decimal-year interpolation
    2030.0,
]

# Smaller grid for the second model (multi-model coverage).
TIMES_IGRF13 = [1900.0, 1950.0, 2000.0, 2020.0, 2025.0]

# (lon_deg, lat_deg, height_km); lon 360/270/200 and -180 exercise
# longitude normalization.
GEODETIC_POINTS = [
    (0.0, 89.9, 0.0),        # near north pole
    (0.0, -89.9, 0.0),       # near south pole
    (0.0, -90.0, 0.0),       # exact south pole (theta = 180)
    (0.0, 0.0, 0.0),         # equator at Greenwich meridian
    (90.0, 0.0, 0.0),        # equator, 90E
    (180.0, 0.0, 0.0),       # equator, date line
    (270.0, 0.0, 0.0),       # equator, 90W
    (5.32415, 60.39299, 0.0),  # ppigrf documentation point (Bergen)
    (121.5, 25.0, 500.0),    # Taipei, 500 km
    (-74.0, 40.7, 0.0),      # New York
    (139.69, 35.69, 1000.0),  # Tokyo, 1000 km
    (-46.63, -23.55, 0.0),   # Sao Paulo
    (77.21, 28.61, 30000.0),  # Delhi, 30000 km
    (-0.0013, 51.4778, 50000.0),  # Greenwich, 50000 km
    (116.4074, 39.9042, 500.0),   # Beijing
    (151.2093, -33.8688, 2000.0),  # Sydney
    (30.0, 45.0, 313.03),    # generic mid-latitude
    (-135.0, -60.0, 1234.5),  # south ocean
    (12.3456, -78.9012, 4321.0),  # random polar-ish
    (360.0, 45.0, 100.0),    # lon 360 == lon 0
    (200.0, 10.0, 50.0),     # lon > 180
    (-180.0, -45.0, 300.0),  # lon -180 == lon 180
]

# (r_km, theta_deg, phi_deg)
GEOCENTRIC_POINTS = [
    (6371.2, 0.01, 0.0),     # near north pole
    (6371.2, 30.0, 4.0),     # ppigrf documentation point
    (6371.2, 90.0, 90.0),
    (6371.2, 135.0, 240.0),
    (6371.2, 179.99, 100.0),  # near south pole
    (6371.2, 180.0, 0.0),    # exact south pole
    (6671.2, 45.0, 45.0),    # ~300 km altitude
    (7371.2, 60.0, 120.0),   # 1000 km altitude
    (6871.2, 10.0, 350.0),
    (36781.6, 90.0, 0.0),    # 30000 km altitude
    (56371.2, 45.0, 180.0),  # 50000 km altitude
    (42371.2, 120.0, 300.0),
    (70000.0, 75.5, 55.5),
    (6801.2, 0.5, 25.0),
    (6801.2, 179.5, 335.0),
]

# Smaller grids for igrf13 (index into the lists above).
GEODETIC_POINTS_13 = GEODETIC_POINTS[:10]
GEOCENTRIC_POINTS_13 = GEOCENTRIC_POINTS[:10]


def epoch_timestamps(coeff_fn):
    """(decimal_year, pd.Timestamp) for each model epoch, sorted."""
    g, _ = read_shc(coeff_fn)
    epochs = []
    for ts in g.index:
        year = ts.year
        start = pd.Timestamp(year, 1, 1)
        days_in_year = (pd.Timestamp(year + 1, 1, 1) - start).days
        frac = (ts - start) / pd.Timedelta(days=days_in_year)
        epochs.append((year + float(frac), pd.Timestamp(ts)))
    epochs.sort()
    return epochs


def decimal_year_to_timestamp(y, epochs):
    """Timestamp at which both implementations evaluate the model.

    libigrf maps a decimal year to the uniform elapsed-time axis with
    the 365/366 day-count (the exact inverse of its
    igrf_datetime_to_decimal_year); ppigrf maps it to a timestamp with
    its own yearfrac_to_datetime.  The two mappings agree exactly, so
    ppigrf is evaluated directly at that timestamp."""
    if y < epochs[0][0] - 1e-12 or y > epochs[-1][0] + 1e-12:
        raise ValueError("year %r outside model range" % y)
    return yearfrac_to_datetime([y])[0]


def fmt(x):
    """Round-trip-exact double formatting for the CSV files."""
    return "%.17g" % x


def generate(model_name, times, geodetic_points, geocentric_points):
    coeff_fn = MODELS[model_name]
    epochs = epoch_timestamps(coeff_fn)
    print("model %s: %d epochs  [%.1f .. %.1f]"
          % (model_name, len(epochs), epochs[0][0], epochs[-1][0]))

    gd_path = os.path.join(REF_DIR, "%s_geodetic.csv" % model_name)
    gc_path = os.path.join(REF_DIR, "%s_geocentric.csv" % model_name)

    with open(gd_path, "w", newline="\n") as fg, \
            open(gc_path, "w", newline="\n") as fc:
        fg.write(
            "# libigrf regression reference: geodetic (X north, Y east,"
            " Z down) [nT]\n")
        fg.write("# model: %s.shc, reference: ppigrf (IAGA-VMOD)\n"
                 % model_name)
        fg.write("# decimal_year,lon_deg,lat_deg,height_km,X_nT,Y_nT,Z_nT\n")
        fc.write(
            "# libigrf regression reference: geocentric spherical [nT]\n")
        fc.write("# model: %s.shc, reference: ppigrf (IAGA-VMOD)\n"
                 % model_name)
        fc.write(
            "# decimal_year,r_km,theta_deg,phi_deg,Br_nT,Btheta_nT,Bphi_nT\n")

        n_gd = n_gc = 0
        for y in times:
            ts = decimal_year_to_timestamp(y, epochs)
            for lon, lat, h in geodetic_points:
                be, bn, bu = ppigrf.igrf(lon, lat, h, ts,
                                         coeff_fn=coeff_fn)
                x, yy, z = float(bn[0]), float(be[0]), -float(bu[0])
                if not all(np.isfinite([x, yy, z])):
                    raise RuntimeError(
                        "non-finite reference at %r %r" % (y, (lon, lat, h)))
                fg.write(",".join([fmt(y), fmt(lon), fmt(lat), fmt(h),
                                   fmt(x), fmt(yy), fmt(z)]) + "\n")
                n_gd += 1
            for r, th, ph in geocentric_points:
                br, bt, bp = ppigrf.igrf_gc(r, th, ph, ts,
                                            coeff_fn=coeff_fn)
                br, bt, bp = float(br[0]), float(bt[0]), float(bp[0])
                if not all(np.isfinite([br, bt, bp])):
                    raise RuntimeError(
                        "non-finite reference at %r %r" % (y, (r, th, ph)))
                fc.write(",".join([fmt(y), fmt(r), fmt(th), fmt(ph),
                                   fmt(br), fmt(bt), fmt(bp)]) + "\n")
                n_gc += 1

    print("  wrote %s (%d rows)" % (os.path.relpath(gd_path, ROOT), n_gd))
    print("  wrote %s (%d rows)" % (os.path.relpath(gc_path, ROOT), n_gc))


def main():
    if not os.path.isdir(REF_DIR):
        os.makedirs(REF_DIR)
    generate("igrf14", TIMES_IGRF14, GEODETIC_POINTS, GEOCENTRIC_POINTS)
    generate("igrf13", TIMES_IGRF13, GEODETIC_POINTS_13,
             GEOCENTRIC_POINTS_13)


if __name__ == "__main__":
    main()
