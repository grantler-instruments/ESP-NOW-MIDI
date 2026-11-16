# client_bela_trill

Reads a [Bela Trill](https://bela.io/products/trill/) capacitive touch sensor over
I2C and sends touches as MIDI over ESP-NOW. The sensor type is detected
automatically at startup, so the same firmware works with every Trill:

| Sensor | Shape | What it sends |
|---|---|---|
| **Bar** | straight strip (1D) | position, touch size, note |
| **Flex** | flexible strip (1D) | position, touch size, note |
| **Ring** | circle (1D) + 2 buttons | position, touch size, note, buttons |
| **Square** | square pad (2D) | X / Y position, touch size, note |
| **Hex** | hexagon pad (2D) | X / Y position, touch size, note |
| **Craft** | 30 pads for your own electrodes | one note per pad |

Only one sensor is used: the first one found. Only the first touch on it is used.

## MIDI output

All messages go out on MIDI channel 1 and only when a value changes. Change the
numbers in `config.h`.

| Message | Bar / Flex / Ring | Square / Hex |
|---|---|---|
| CC 70 (`CC_TRILL_X`) | position along the strip / around the ring | horizontal position |
| CC 71 (`CC_TRILL_Y`) | — | vertical position |
| CC 72 (`CC_TRILL_SIZE`) | touch size (~ pressure), 0 on release | same |
| Note On / Off 60 (`TRILL_NOTE`) | on touch / release, velocity = touch size | same |
| CC 73 / 74 (`CC_TRILL_BUTTON_0/1`) | Ring only: center buttons, 127 pressed / 0 released | — |

**Craft:** pad *n* sends Note On / Off `TRILL_CRAFT_NOTE_BASE + n` (pad 0 = note
36) with velocity `TRILL_CRAFT_VELOCITY`.

On the Ring, the position wraps around: 127 is next to 0.

## Wiring

All Trill sensors use the same 4-pin I2C connector:

| Trill | Board |
|---|---|
| VCC | 3V3 |
| GND | GND |
| SDA | SDA (LOLIN S2 mini: GPIO 33, LOLIN S3 mini: GPIO 35) |
| SCL | SCL (LOLIN S2 mini: GPIO 35, LOLIN S3 mini: GPIO 36) |

Power the sensor from 3.3 V, not 5 V.

**Flex:** connect the flexible sensor to the Flex board. The sketch puts the Flex
into slider mode with Bela's slider settings (`TRILL_FLEX_PRESCALER`,
`TRILL_FLEX_NOISE_THRESHOLD`). With a custom or longer sensor, adjust them.

**Craft:** connect your electrodes (copper tape, conductive thread, fruit, …) to
the 30 pads.

## Setup

1. Install the **Trill** library by Bela (Arduino Library Manager). PlatformIO
   fetches it automatically from `platformio.ini`.
2. Pair with the dongle: run `examples/print_mac` on the dongle and set
   `peerMacAddress` in the sketch.
3. Flash, and open the serial monitor at 115200 baud. You should see e.g.
   `Trill Ring found at 0x38`.

If you see `No Trill sensor found`, check the wiring. The sketch scans again
every 2 s (`TRILL_RETRY_INTERVAL_MS`), so you can connect the sensor after
power-up.

### Sensor detection

With `TRILL_I2C_ADDRESS 0` (the default), the sketch checks I2C addresses
0x20…0x50 and uses the first Trill that answers. That range covers every Trill's
default address (Bar 0x20, Square 0x28, Craft 0x30, Ring 0x38, Hex 0x40,
Flex 0x48) and the addresses you can select with the solder jumpers on the back.

The scan sends Trill's identify command to every device in that range. If other
I2C devices share the bus, or more than one Trill is connected, set
`TRILL_I2C_ADDRESS` to the address of the Trill you want instead.

## Tuning

Set `TRILL_DEBUG 1` in `config.h` to print raw values to the serial monitor, then
adjust:

- `TRILL_SIZE_MIN` / `TRILL_SIZE_MAX`: raw touch size range mapped to 0…127.
- `TRILL_*_LOCATION_MAX`: raw position range per sensor. Change it if a position
  CC doesn't reach 127 at the edge.
- `TRILL_BUTTON_THRESHOLD` (Ring): raw button value above which a button counts
  as pressed.
- `TRILL_CRAFT_THRESHOLD` (Craft): raw pad value above which a pad counts as
  touched. Depends a lot on the electrode size.
- `TRILL_INVERT_X` / `TRILL_INVERT_Y`: flip the axis direction. On the Ring,
  `TRILL_INVERT_X` reverses the direction of rotation.

To turn off the note messages on the touch sensors, set `ENABLE_TRILL_NOTE 0`.
