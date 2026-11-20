# libpd for ESP-IDF

The component fetches libpd 0.16.1 and builds its Pd 0.56-5 engine. Local Git
patches reduce software double-precision arithmetic in 18 commonly used DSP
objects. The patches leave object layouts and the libpd API unchanged.

## Versions and patch application

| Dependency | Version | Commit |
| --- | --- | --- |
| libpd | 0.16.1 | `ba0dc63262901d658af8bbda5e619a60fa975e78` |
| Pure Data | 0.56-5 | `f009fd8d7b537e209e09898d487fdf1bf547da2b` |

On normal CMake configuration, after fetching the sources, the component checks
the Pd revision and applies `patches/0001` through `0004` in order. Subsequent
configurations recognise already-applied patches. Conflicting edits or a different
Pd revision stop configuration with an explicit error; local edits are not reset.
The dependency-discovery pass does not fetch or patch anything.

`CONFIG_LIBPD_FLOAT_DSP` is enabled by default. In `idf.py menuconfig`, use
**Component config → libpd → Use single-precision DSP optimisations** to disable
it and compile the original routines. Patch files remain applied: their original
branches compile whenever `PD_FLOAT_DSP` is undefined. Do not define the macro as
zero to disable it. The optimised build requires 32-bit Pd samples.

## Changes

| Patch | Objects | Implementation |
| --- | --- | --- |
| `0001-float-math.patch` | `mtof~`, `ftom~`, `dbtorms~`, `rmstodb~`, `dbtopow~`, `powtodb~`, `sqrt~`, `rsqrt~`, `exp~`, `log~`, `pow~` | Float maths functions and constants, including unrolled and scalar-input variants. Setup-time tables and legacy quick square-root routines retain their original implementation. |
| `0002-float-oscillators.patch` | `phasor~`, `osc~`, `cos~`, `tabosc4~`, `vcf~` | Bounded float phase and table lookup; compensated oscillator increments retain a residual in the existing double state between blocks. Reads input before writing output, supporting Pd's in-place buffers. Legacy 512-point routines retain their original implementation. |
| `0003-float-table-index.patch` | `tabread4~` | TwoSum retains the residual from adding signal index and onset; separate integral and fractional parts preserve interpolation for large onsets. Wide integer arithmetic handles bounds before narrowing. |
| `0004-vline-spans.patch` | `vline~` | Double scheduler timestamps and segment calculations; uninterrupted spans render as float `value + increment * sample_offset`. Endpoint state remains double. |

`d_array.c`, `d_osc.c`, and `d_ctl.c` compile with `-fno-fast-math` after the
component's general optimisation flags. Reassociation would invalidate TwoSum and
compensated phase addition. The remaining sources keep the existing flags.

The optimisations are numerically compatible within tested tolerances, rather
than generally bit-identical. Oscillator phase rounding, low-frequency increments,
maths rounding and ramp accumulation can differ. Double operations remain at
oscillator block boundaries, at envelope scheduling boundaries, in compatibility
routines, and in unrelated objects. This is not a conversion of the entire engine
to single precision.

## Host verification

Run from the repository root. libpd must already have been fetched. CMake 3.25+,
a C compiler, Git and Python 3.10+ are required; the tests use Python's standard
library only.

```sh
cmake -DPD_SOURCE_DIR="$PWD/components/libpd/libpd/pure-data" \
  -P components/libpd/cmake/ApplyPatches.cmake
python3 test/libpd/test_patch_application.py
cmake -S test/libpd -B /tmp/enomik-pd-stock \
  -DCMAKE_BUILD_TYPE=Release -DENOMIK_FLOAT_DSP=OFF
cmake --build /tmp/enomik-pd-stock --parallel
cmake -S test/libpd -B /tmp/enomik-pd-float \
  -DCMAKE_BUILD_TYPE=Release -DENOMIK_FLOAT_DSP=ON
cmake --build /tmp/enomik-pd-float --parallel
python3 test/libpd/test_dsp.py \
  --stock /tmp/enomik-pd-stock/libpd/libs/libpd.dylib \
  --float /tmp/enomik-pd-float/libpd/libs/libpd.dylib
```

On Linux, replace `libpd.dylib` with `libpd.so`. Host builds use identical IEEE
flags on the three sensitive files to isolate implementation differences from
compiler reassociation. Each render uses a separate process to isolate Pd globals.
The reference uses the original branches in the patched sources.

The render suite covers all 18 objects at 48 kHz: scalar and signal binary
operators, domain guards, oscillator resets and wrapping, positive/negative/high
frequencies, slow increments, legacy oscillator compatibility, large table
onsets, overlapping and instantaneous ramps, stop, 128-sample reblocking,
deterministic scheduling stress, and scheduling after a ten-minute simulated
clock advance. Maths comparisons use absolute and relative tolerances; oscillator
phase comparisons account for circular distance.

Verification on 2026-11-20: the host render comparisons and patch-application tests
passed. An isolated ESP-IDF 6.0.1 project targeting ESP32-S3 compiled and linked
with this component, including its compiled-in extras. Inspection of target object
code confirmed float maths calls in the patched current maths routines and no
software double references in `tabread4~`'s perform routine. Double work remains
outside the oscillator sample loops and within envelope scheduling.

The reproducible target compile check is in `test/libpd/idf`. With ESP-IDF
activated, run:

```sh
idf.py -C test/libpd/idf -B /tmp/enomik-pd-idf-check \
  -DSDKCONFIG=/tmp/enomik-pd-idf-check.sdkconfig -DIDF_TARGET=esp32s3 build
python3 test/libpd/audit_target.py /tmp/enomik-pd-idf-check/compile_commands.json
```

No board was flashed. CPU savings, audio underruns and the `kalimba_groove`
workload still require measurement on hardware, using identical firmware settings
with `CONFIG_LIBPD_FLOAT_DSP` enabled and disabled. Measure average and worst-case
`libpd_process_float()` duration against the audio deadline; include MIDI/radio
traffic and long-running audio. Host numerical tests do not establish a target
speedup.

## Remaining audit scope

The initial 18 objects are covered. A separate profiling-led audit remains for
`d_fft_fftsg.c` (default double FFT storage), `bob~` (double solver state), `loop~`
(double phase), `expr~`/`fexpr~`, and analysis extras such as `fiddle~`, `sigmund~`
and `bonk~`. Their precision requirements differ; they are not changed by these
patches. Control-rate objects and setup-time trigonometric tables are also outside
this optimisation scope.

To update a patch, edit the pinned downloaded Pd sources and regenerate the
corresponding diff with `git -C components/libpd/libpd/pure-data diff -- src/...`.
Test against a clean local clone and regenerate all affected patches together;
configuration deliberately rejects partially matching patch versions.
