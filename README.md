# libigrf

A small, stable, portable, dependency-free C library for computing the
**International Geomagnetic Reference Field (IGRF)**.

libigrf reads IGRF `.shc` spherical harmonic coefficient files (e.g.
`igrf13.shc`, `igrf14.shc`) and evaluates the field at arbitrary
locations and times within the model's epoch range. It is designed to be
embedded as a long-term dependency in C/C++ software on Windows, Linux,
Android and iOS:

```text
load model  ->  compute  ->  use result  ->  free model
```

All complexity (coefficient parsing, epoch interpolation, Schmidt
quasi-normalized spherical harmonics, coordinate conversions) is hidden
behind a small public API of plain C.

## Features

- `.shc` model file loading with strict validation
- Geodetic input (WGS84): returns local NED components `X, Y, Z`
- Geocentric spherical input: returns `Br, Btheta, Bphi`
- Single-point, fixed-time batch and varying-time batch computation
- Linear interpolation of Gauss coefficients between model epochs on
  the uniform elapsed-time axis (like ppigrf; no silent extrapolation)
- Geodetic → geocentric coordinate conversion
- Calendar date/time → decimal year conversion
- Derived elements `H, F, D, I`
- CMake build: static library by default, shared library optional
- Tested against the [ppigrf](https://github.com/klaundal/ppigrf)
  reference implementation (IAGA-VMOD compatible) to ~1e-7 nT

## Conventions

- **Geodetic coordinates** (WGS84): east-positive longitude `[°]`,
  geodetic latitude `[-90, 90] [°]`, height above the reference
  ellipsoid `[km]`.
- **Geocentric coordinates**: `r` distance from the Earth's center
  `[km]`, `theta` geocentric colatitude `[°]` with
  `theta = 0° -> North pole`, `theta = 90° -> Equator`,
  `theta = 180° -> South pole`, `phi` east-positive longitude `[°]`.
- The spherical harmonic expansion is always evaluated in the
  **geocentric** spherical frame, exactly as IGRF is defined. Geodetic
  input is converted internally, and the result is rotated into the
  local geodetic NED frame (`X = north`, `Y = east`, `Z = down`).
- `Btheta` points towards **increasing theta** (from north to south),
  `Bphi` points east.
- All field components are in **nanotesla (nT)**.
- Longitudes accept any finite value (e.g. `360`, `-180`, `270`) and are
  normalized internally to `[0, 360)`.
- Time is a **decimal year** (`2026.0`, `2026.5`, `2027.25`, ...).
  Requests outside the model's epoch range return
  `IGRF_ERROR_YEAR_OUT_OF_RANGE` instead of extrapolating.

## Building

Requirements: CMake >= 3.15 and a C99 compiler. There are no third-party
dependencies beyond the C standard library and libm.

### Windows (MinGW-w64)

Verified with MinGW-w64 GCC 15.1.0 and the "MinGW Makefiles" generator:

```bash
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build -j
cd build && ctest --output-on-failure
```

Ninja works equally well (`-G Ninja`). Running the example and the demo
from the build directory works out of the box because the model data and
web assets are copied there at configure time:

```bash
./build/igrf_example.exe            # CLI example
./build/igrf_demo.exe               # then open http://localhost:8080/
```

### Linux / macOS

```bash
cmake -S . -B build
cmake --build build -j
cd build && ctest --output-on-failure
```

### Shared library

The library builds as a **static** library by default. Configure with
`-DBUILD_SHARED_LIBS=ON` to produce a shared library (`.dll` on Windows,
`.so` on Linux):

```bash
cmake -S . -B build-shared -DBUILD_SHARED_LIBS=ON
cmake --build build-shared -j
```

CMake targets: `igrf` (library), `igrf_example`, `igrf_demo`,
`igrf_benchmark`, and `igrf_tests` (aggregates the seven test
executables registered with CTest).

The core library sources are plain standard C and contain no
platform-specific code, so they compile unchanged with the Android NDK
and Apple Clang.

## C usage

```c
#include "igrf.h"

/* 1. load a model (caller owns the returned handle) */
igrf_model_t *model = igrf_model_load("data/igrf14.shc");
if (model == NULL) {
    /* file missing or not a valid .shc model */
}

/* 2. describe a geodetic position */
igrf_geodetic_t pos = {
    .lon_deg   = 121.5,   /* east-positive degrees            */
    .lat_deg   = 25.0,    /* geodetic latitude                */
    .height_km = 500.0,   /* above the WGS84 ellipsoid        */
};

/* 3. compute the field (nT) in the local geodetic NED frame */
igrf_ned_t B;
igrf_status_t status = igrf_compute_geodetic(model, 2026.5, &pos, &B);
if (status != IGRF_OK) {
    /* see igrf_status_t: NULL argument, invalid input, bad year, ... */
}

/* X = north, Y = east, Z = down */

/* 4. derived elements (any output pointer may be NULL) */
double H, F, D, I;
igrf_ned_elements(&B, &H, &F, &D, &I);

/* 5. release the model */
igrf_model_free(model);
```

Geocentric example:

```c
igrf_geocentric_t gc = { .r_km = 6500.0, .theta_deg = 30.0, .phi_deg = 4.0 };
igrf_spherical_t s;
igrf_compute_geocentric(model, 2026.5, &gc, &s);
/* s.Br_nT, s.Btheta_nT (southward), s.Bphi_nT (eastward) */
```

Batches:

```c
igrf_geodetic_t    positions[1000];
igrf_ned_t         fields[1000];
igrf_compute_geodetic_batch(model, 2026.5, positions, 1000, fields);

igrf_geodetic_time_point_t track[N];  /* each point carries its year */
igrf_compute_geodetic_batch_varying_time(model, track, N, fields);
```

The caller provides all input/output arrays; the library never allocates
memory the caller must track, apart from the model handle itself.

### Calendar dates

```c
double decimal_year =
    igrf_datetime_to_decimal_year(2026, 7, 2, 12, 0, 0.0); /* 2026.5 */
igrf_compute_geodetic(model, decimal_year, &pos, &B);
```

Invalid calendar input (e.g. month 13, second 60) returns `NaN`.

### Model file location

The library never assumes a filesystem path: `igrf_model_load()` takes
whatever path (or portable, platform-specific mechanism) the caller
supplies. Applications typically deploy `igrf14.shc` alongside their
data; the example and demo default to `data/igrf14.shc`.

## Error handling

Every fallible function returns an `igrf_status_t`:

| status | meaning |
| --- | --- |
| `IGRF_OK` | success |
| `IGRF_ERROR_NULL_ARGUMENT` | a required pointer was NULL |
| `IGRF_ERROR_INVALID_INPUT` | NaN/Inf or out-of-domain value, invalid count or date |
| `IGRF_ERROR_FILE` | (internal) model file could not be opened |
| `IGRF_ERROR_INVALID_MODEL` | file readable but not a valid `.shc` model |
| `IGRF_ERROR_YEAR_OUT_OF_RANGE` | decimal year outside the model's epoch range |

The library never prints, exits or aborts; `igrf_model_load()` returns
NULL on any failure (file or content).

## C++ usage

The header is C++-linkable without any wrapper:

```cpp
extern "C" {
#include "igrf.h"
}
```

## Python

The library exposes a plain C ABI and can therefore be wrapped by Python
or other languages in the future; no binding ships with this project.

## Performance

Measured with `tools/benchmark.c` (build it via the `igrf_benchmark`
target) on 100000 deterministic global points, best of 5, GCC 15.1.0
`-O3`, single-threaded, Intel Core Ultra 7 (Meteor Lake), Windows 11:

| scenario                       | per point | throughput      |
| ------------------------------ | --------- | --------------- |
| single-point geodetic          | 4.08 µs   | ~245 000 pts/s  |
| single-point geocentric        | 3.90 µs   | ~256 000 pts/s  |
| fixed-time batch geodetic      | 3.80 µs   | ~263 000 pts/s  |
| fixed-time batch geocentric    | 3.62 µs   | ~276 000 pts/s  |
| varying-time batch (distinct years) | 3.95 µs | ~253 000 pts/s |
| varying-time batch (same year) | 3.81 µs   | ~262 000 pts/s  |

The cost is dominated by the field evaluation itself (Legendre
recursion and summation, plus one `sin`/`cos` pair per order); the
per-call preparation that batches avoid is worth roughly 5–7%, and a
coefficient preparation for a new decimal year adds ~0.15 µs per
point. Model loading (parsing `igrf14.shc`) takes ~0.3 ms. Use a
Release build for any measurement; unoptimized builds are several
times slower.

## Reference validation

The regression datasets in `tests/reference/` were generated with
[ppigrf](https://github.com/klaundal/ppigrf) (IAGA-VMOD compatible,
MIT licensed) using `tools/generate_reference.py`. The regression test
compares 729 configurations — poles, equator, Greenwich, date line,
longitude normalization cases, 0–50000 km altitudes, exact epochs and
mid-interval times across 1900–2030 — with a maximum absolute error of
about 5e-08 nT (tolerance 1e-04 nT or 1e-09 relative). See
[IMPLEMENTATION_NOTES.md](IMPLEMENTATION_NOTES.md) for the conventions
behind the comparison.

Regenerating the datasets requires Python with `numpy`, `pandas` and
`ppigrf` (`pip install ppigrf`); a local ppigrf checkout at
`ppigrf-main/` is used instead if present.

## Acknowledgements

libigrf was developed with the assistance of GLM 5.3 Flash (Z.ai).

## License

MIT — see [LICENSE](LICENSE).
