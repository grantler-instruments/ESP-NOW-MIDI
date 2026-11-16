# client_bela_trill

Reads a [Bela Trill](https://bela.io/products/trill/) capacitive touch sensor over
I2C and sends touches as MIDI over ESP-NOW. Supported sensors:

- **Trill Square** (2D): X / Y position and touch size.
- **Trill Ring** (1D): position around the ring, touch size, and the two center
  buttons.

Only the first touch is used.

## MIDI output

| Message | Square | Ring |
|---|---|---|
| CC 70 (`CC_TRILL_X`) | horizontal position | position around the ring |
| CC 71 (`CC_TRILL_Y`) | vertical position | — |
| CC 72 (`CC_TRILL_SIZE`) | touch size (~ pressure), 0 on release | same |
| CC 73 / 74 (`CC_TRILL_BUTTON_0/1`) | — | center buttons, 127 pressed / 0 released |
| Note On / Off 60 (`TRILL_NOTE`) | on touch / release, velocity = touch size | same |

All values are sent on MIDI channel 1 and only when they change. Change the
numbers in `config.h`.

## Wiring

Both sensors use the same 4-pin I2C connector:

| Trill | Board |
|---|---|
| VCC | 3V3 |
| GND | GND |
| SDA | SDA (LOLIN S2 mini: GPIO 33, LOLIN S3 mini: GPIO 35) |
| SCL | SCL (LOLIN S2 mini: GPIO 35, LOLIN S3 mini: GPIO 36) |

Power the sensor from 3.3 V, not 5 V.

The sketch uses each sensor's default I2C address (Square `0x28`, Ring `0x38`).
If you changed the address with the solder jumpers on the back, pass it as the
second argument to `_trill.setup(...)` in the sketch.

## Setup

1. Install the **Trill** library by Bela (Arduino Library Manager). PlatformIO
   fetches it automatically from `platformio.ini`.
2. Select your sensor in `config.h`:

   ```c
   #define TRILL_TYPE TRILL_TYPE_SQUARE   // Trill Square
   // or
   #define TRILL_TYPE TRILL_TYPE_RING     // Trill Ring
   ```

   This also selects the position range (`TRILL_LOCATION_MAX`: Square 1792,
   Ring 3584).
3. Pair with the dongle: run `examples/print_mac` on the dongle and set
   `peerMacAddress` in the sketch.
4. Flash, and open the serial monitor at 115200 baud. You should see
   `Trill Square is connected` or `Trill Ring is connected`. If you see
   `does not respond`, check the wiring and that `TRILL_TYPE` matches the sensor.

## Tuning

Set `TRILL_DEBUG 1` in `config.h` to print raw values to the serial monitor, then
adjust:

- `TRILL_SIZE_MIN` / `TRILL_SIZE_MAX`: raw touch size range mapped to 0…127.
- `TRILL_LOCATION_MAX`: change it if a position CC doesn't reach 127 at the edge.
- `TRILL_BUTTON_THRESHOLD` (Ring only): raw button value above which a button
  counts as pressed.
- `TRILL_INVERT_X` / `TRILL_INVERT_Y`: flip the axis direction. On the Ring,
  `TRILL_INVERT_X` reverses the direction of rotation.

On the Ring, the position wraps around: 127 is next to 0.

To turn off the note messages, set `ENABLE_TRILL_NOTE 0`.
