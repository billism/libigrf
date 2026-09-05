# IMPLEMENTATION NOTES

This file records the decisions taken where the implementation
specification left room for interpretation, following its final
instruction: prefer the IGRF official definitions and the
IAGA-VMOD/ppigrf reference behaviour, and otherwise choose the simplest
KISS-compliant option. Nothing here changes the public API mandated by
the specification.

## 1. Time interpolation space (decimal year vs timestamp)

The specification mandates linear interpolation of the Gauss
coefficients between epochs **in decimal-year space**:

```text
f = (year - epoch1) / (epoch2 - epoch1)
```

ppigrf (via pandas `interpolate(method='time')`) interpolates in
**timestamp space**. The two are not equivalent, because the decimal
year is a piecewise-linear (per-year) function of real time: a point at
decimal year 2022.5 has timestamp-space interpolation parameter
0.50027 between the 2020 and 2025 epochs, not 0.5. The resulting
coefficient difference can reach ~0.05 nT.

libigrf follows the specification (decimal-year space). For the
regression comparison, `tools/generate_reference.py` therefore
evaluates ppigrf at the timestamp

```text
t = T1 + f * (T2 - T1)
```

so that ppigrf's interpolation parameter equals the one libigrf uses,
and the comparison isolates the field computation itself. With that
alignment the two implementations agree to ~5e-08 nT. Comparing at
unaligned timestamps would show differences up to ~0.1 nT that are pure
convention, not error.

## 2. Pole handling (theta = 0 / theta = 180)

At the poles the `Bphi` summation divides by `sin(theta)` = 0. The
analytic limits of the individual terms exist (`P(n,m) ~ sin(theta)^m`),
but the limit of `Bphi` itself depends on the direction of approach,
because at a pole the "eastward" unit vector is undefined.

libigrf returns the smooth limit along the meridian of the requested
longitude: for the `m = 1` terms, `P(n,1)/sin(theta)` is replaced by its
limit `dP(n,1)/dtheta` evaluated at the pole. This makes the returned
field continuous when moving towards a pole along a meridian, keeps
every component finite, and adds only a few lines of code. The exact
value of `Bphi` at a pole is, by nature of the coordinate system, a
convention.

Notes:

- ppigrf produces NaN for `Bphi` at the exact north pole (a literal
  0/0 in numpy), which is why the reference datasets use lat = +/-89.9
  and theta = 0.01 near the north pole. The exact south pole
  (theta = 180) flows through the normal code path in both
  implementations because `sin(pi)` in double precision is ~1.2e-16,
  not zero, and is validated directly against ppigrf.
- `Btheta` at the poles is well-defined (its limit is
  direction-independent) and comes out of the normal recursion.

## 3. `igrf_datetime_to_decimal_year()` error reporting

The specification fixes the signature as returning `double` with no
status channel. Invalid calendar input (month outside 1..12, day
outside the month, hour/minute/second out of range, NaN/Inf second)
therefore returns `NaN`, which callers can test with `isnan()`. This is
documented in the header.

## 4. Longitude normalization

Any finite longitude is accepted and normalized to `[0, 360)` with
`fmod`, so `360`, `-180`, `270` and equivalent representations all
denote the same meridian without corner cases. The header documents
this. Latitude (`[-90, 90]`), colatitude (`[0, 180]`) and radius
(`> 0`) are strictly validated; heights may be any finite value
(negative heights below the ellipsoid are mathematically well-defined).

## 5. Batch semantics

- `count == 0` is rejected with `IGRF_ERROR_INVALID_INPUT` ("invalid
  count" in the specification's validation list).
- All points are validated before any computation, so a batch with one
  invalid point produces an error and no partial results
  (all-or-nothing).
- Fixed-time batches prepare the interpolated coefficients exactly
  once; varying-time batches prepare them only when the decimal year
  changes relative to the previous point (which also handles
  non-monotonic year sequences correctly).

## 6. SHC parser hardening

The parser follows the ppigrf `read_shc()` format and additionally:

- rejects non-finite values, duplicated or missing coefficients,
  non-ascending epochs, wrong value counts and unknown degrees/orders
  (as `IGRF_ERROR_INVALID_MODEL`);
- skips blank lines and accepts CRLF line endings and a UTF-8 BOM;
- imposes defensive dimension limits (degree <= 100, epochs <= 10000)
  so that corrupt headers cannot request absurd allocations.

The epoch range is taken from the file (no hardcoded 1900/2030), so
`igrf13.shc` (1900..2025) and `igrf14.shc` (1900..2030) work with the
same code. No separate secular-variation API exists: the IGRF-14 SHC
already contains the 2030 epoch computed from the 2025 coefficients and
SV, and all epochs are treated uniformly.

## 7. Shared-library symbols

The public header uses a minimal `IGRF_API` macro that expands to
`__declspec(dllexport/dllimport)` only when building a Windows shared
library (CMake defines `igrf_EXPORTS` there) and to nothing otherwise.
This is the smallest change that makes `-DBUILD_SHARED_LIBS=ON` work on
Windows while keeping the header free of platform-specific types.

## 8. `igrf_ned_elements()` output selection

Each of the four output pointers (`H_nT`, `F_nT`, `D_deg`, `I_deg`) may
be NULL to skip that element; only `field` is required. This is a
superset of the specification's signature and keeps call sites simple.

## 9. Geodetic NED rotation

The geocentric-to-NED rotation uses the `psi` angle exactly as in the
ppigrf reference, but computes it directly from the input geodetic
latitude instead of recovering the latitude from (theta, r) via the
Olsen series (libigrf knows the latitude it converted from). The
difference is below the regression tolerance (the total round-trip
error of the series is negligible), and the frame-invariance test of
F confirms the rotation's consistency.

## 10. Demo design

The web demo loads every `*.shc` file found in a model directory at
startup and exposes them in the UI's "Model file" selector; the HTTP
compute endpoint references models by name, so no arbitrary filesystem
paths are accepted over HTTP. The server is single-threaded,
localhost-only and GET-only. HTTP/socket code lives entirely in the
`igrf_demo` target; the library target contains none of it.
