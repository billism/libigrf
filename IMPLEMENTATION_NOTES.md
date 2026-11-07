# IMPLEMENTATION NOTES

This file records the decisions taken where the implementation
specification left room for interpretation, following its final
instruction: prefer the IGRF official definitions and the
IAGA-VMOD/ppigrf reference behaviour, and otherwise choose the simplest
KISS-compliant option. Nothing here changes the public API mandated by
the specification.

## 1. Time interpolation convention (elapsed time, like ppigrf)

The specification's §6.3 formula interpolates the Gauss coefficients
between epochs in **decimal-year space**:

```text
f = (year - epoch1) / (epoch2 - epoch1)
```

The decimal year, however, is not a uniform time scale: the mapping
from calendar instants to decimal years is piecewise linear with a
kink at every year boundary (1/365 vs 1/366 per day), while elapsed
time is strictly uniform.  ppigrf (via pandas
`interpolate(method='time')`) interpolates in **elapsed time**.  The
two conventions coincide exactly at the epochs and drift in between.

**Worked example (2025–2030 interval, contains leap year 2028, 1826
days).**  Decimal year 2027.5 (2027 is a common year) is the calendar
instant 2027-07-02 12:00.  Decimal-year interpolation gives
f = 0.5 exactly, which corresponds to 2027-07-03 00:00 — a 12-hour
shift; elapsed-time interpolation gives f = 912.5/1826 ≈ 0.499726.
(2022.5, by contrast, is useless for demonstrating the difference:
leap years 2020 and 2024 sit symmetrically around it, so both
conventions give f = 0.5 there.)

**Measured magnitude.**  Evaluating the two conventions at the same
instant (2027-07-02 12:00) over five global stations gives a maximum
component difference of 0.18 nT for 2025–2030 — the strongest secular
variation of any IGRF generation; the same comparison in 1950–1955
yields ~0.07 nT.  Equivalently: in decimal-year space the per-day
coefficient increment differs between common and leap years by exactly
366/365 ≈ 1.00274, while on the elapsed-time axis it is constant and
continuous down to the second.

**Relation to the official IGRF definition.**  Secular variation is
published in nT/year as a linear approximation of the coefficient
change over the 5-year window, and evaluating
`g(t) = g(2025.0) + (t − 2025.0)·SV` is a continuous linear ramp in
which "days" never enter.  The 2030 column of `igrf14.shc` equals the
2025 column plus 5.0·SV (verified: all 195 implied SV values lie on
the official 0.1 nT/yr grid to floating-point noise), so linear
interpolation towards that endpoint is algebraically identical to the
official SV extrapolation — one "epoch coefficients + linear
interpolation" scheme covers the whole 1900–2030 range, which is why
libigrf has no separate SV logic.  The official definition does not
pin down a day-count; the 365/366 convention is a property of the
SHC/ppigrf toolchain.

**Decision (2026-11-07): libigrf interpolates in elapsed-time space,
matching the IAGA-VMOD/ppigrf reference behaviour.**  This follows the
specification's own tie-breaker (prefer the IAGA-VMOD/ppigrf reference
behaviour where the specification leaves room) and intentionally
supersedes the literal decimal-year formula above.  It also makes the
decimal year ↔ calendar instant round-trip unambiguous: a caller who
passes `igrf_datetime_to_decimal_year(d)` gets exactly the coefficients
for the instant `d`, as a user of ppigrf would expect.

**Implementation.**  The public API is unchanged: callers still pass a
decimal year, which the library maps onto a uniform proleptic-Gregorian
day axis (`igrf__decimal_year_to_days()` in `src/coordinates.c`, using
Howard Hinnant's `days_from_civil` algorithm).  That mapping is the
exact inverse of `igrf_datetime_to_decimal_year()` and identical to
ppigrf's `yearfrac_to_datetime()` convention.  The interpolation
parameter is then `f = (t − t₁)/(t₂ − t₁)` in days; the range check
and exact-epoch matching stay in the decimal-year domain (equivalent
and simpler).  For the regression comparison,
`tools/generate_reference.py` evaluates ppigrf directly at the
timestamp of each requested decimal year — both implementations then
use identical coefficients, and the comparison isolates the field
computation itself: they agree to ~5e-08 nT over the 729-row dataset
(which includes 2027.5, the mid-interval case that discriminates
between the two conventions).

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
