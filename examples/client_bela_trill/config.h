// Which Trill sensor is connected — pick one (see README.md)
#define TRILL_TYPE_SQUARE 1
#define TRILL_TYPE_RING 2
#define TRILL_TYPE TRILL_TYPE_SQUARE

// MIDI CC numbers (adjust to taste / your DAW mapping)
#define CC_TRILL_X 70     // Square: horizontal position, Ring: position around the ring
#define CC_TRILL_Y 71     // Square: vertical position (unused on Ring)
#define CC_TRILL_SIZE 72  // touch size (~ pressure)

// Ring only: the two capacitive buttons in the middle send 127 (pressed) / 0
#define CC_TRILL_BUTTON_0 73
#define CC_TRILL_BUTTON_1 74
// raw button reading above which a button counts as pressed — tune with TRILL_DEBUG
#define TRILL_BUTTON_THRESHOLD 400

// Note On while touched, Note Off on release (0 = disabled)
#define ENABLE_TRILL_NOTE 1
#define TRILL_NOTE 60

#define MIDI_CHANNEL 1

// Raw position range mapped to 0…127: Square 0…1792, Ring 0…3584 (28 pads × 128)
#if TRILL_TYPE == TRILL_TYPE_RING
#define TRILL_LOCATION_MAX 3584
#else
#define TRILL_LOCATION_MAX 1792
#endif

// Raw touch size range mapped to 0…127. Depends on finger and sensor settings —
// tune with TRILL_DEBUG enabled.
#define TRILL_SIZE_MIN 0
#define TRILL_SIZE_MAX 4000

// 1 = flip axis direction
#define TRILL_INVERT_X 0
#define TRILL_INVERT_Y 1  // Square: "up" on the sensor = higher CC value

// Print raw touch / button values to Serial
#define TRILL_DEBUG 0

// Minimum interval between sensor reads / MIDI sends (ms)
#define MIDI_SEND_INTERVAL_MS 10
