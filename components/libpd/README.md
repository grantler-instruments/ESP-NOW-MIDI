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

## Optional IRAM placement

`CONFIG_LIBPD_IRAM_DSP` places 55 common DSP routine variants into IRAM using
`libpd_hot.lf`, registered through `LDFRAGMENTS`. It is available on ESP32-S3 and
is disabled by default for the reusable component. The Waveshare audio example
enables it in `sdkconfig.defaults`. For existing configurations, enable
**Component config → libpd → Place common Pd DSP routines in IRAM** using
`idf.py menuconfig`; defaults do not override a saved configuration.

The selection includes DSP dispatch and scheduling, `libpd_process_float`,
signal copy/zero/addition, scalar and unrolled addition/subtraction/multiplication,
`line~`, `vline~`, `lop~`, `hip~`, `biquad~`, current oscillators and `vcf~`,
`tabread4~`, fixed and interpolated delay reads, delay writes, and the 11 current
maths objects listed above. Legacy compatibility routines stay in flash. The
selection changes placement only; it does not duplicate or rewrite DSP routines.
Associated function literals move with the code. Pooled constants, external maths
helpers and other callees may remain in flash; signal buffers and tables are not
moved. This is not a guarantee of operation while flash cache is disabled.

Measured with ESP-IDF 6.0.1 on the complete Waveshare audio firmware, 2026-11-20:

| Linker measurement | Flash placement | IRAM placement | Change |
| --- | ---: | ---: | ---: |
| `.iram0.text` | 83,855 bytes | 94,211 bytes | +10,356 bytes |
| Statically unallocated shared instruction/data SRAM | 209,771 bytes | 199,415 bytes | −10,356 bytes |
| Static `.data` in shared SRAM | 25,394 bytes | 25,394 bytes | 0 |
| Static `.bss` in shared SRAM | 38,096 bytes | 38,096 bytes | 0 |

All 55 selected function symbols were verified in IRAM in the linked ELF. Their
function bodies occupy 9,944 bytes after linker relaxation; the 10,356-byte
increase includes literals and alignment. The remaining shared SRAM is a linker
estimate, not the free heap after startup. Boot-time allocation, task stacks,
Wi-Fi, audio buffers and Pd patch allocations still consume internal memory.
Placement fits this firmware, but runtime headroom and timing gains need board
measurements. No IRAM firmware was flashed during this check.

To verify an enabled build (from the repository root):

```sh
python3 test/libpd/audit_iram.py \
  examples_idf/client_pd_waveshare_audio_s3/build/esp_now_midi_client_pd_waveshare_audio_s3.elf \
  --compile-commands examples_idf/client_pd_waveshare_audio_s3/build/compile_commands.json
```

The audit checks every selected scalar/unrolled/static variant and verifies that
legacy oscillator/filter routines remain in flash. Use `--expect flash` against
an otherwise equivalent disabled build. ESP-IDF's `idf.py size` reports overall
memory allocation; use the map file for literals and alignment. Runtime testing
should report minimum internal free heap, largest internal free block, Pd task
stack margin, processing-time percentiles and audio failures under MIDI/radio
traffic before treating this placement as production-validated.

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

CPU savings, audio underruns and the `kalimba_groove`
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
