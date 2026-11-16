// The sensor type is detected automatically (Bar, Square, Ring, Hex, Flex,
// Craft). 0 = scan the I2C bus (0x20…0x50) and use the first Trill found;
// set an address (e.g. 0x28) to only use the sensor there.
#define TRILL_I2C_ADDRESS 0

// While no sensor is found, scan again this often (ms)
#define TRILL_RETRY_INTERVAL_MS 2000

#define MIDI_CHANNEL 1

// --- Bar, Square, Ring, Hex, Flex: touch position / size -------------------

// MIDI CC numbers (adjust to taste / your DAW mapping)
#define CC_TRILL_X 70     // Bar / Ring / Flex: position, Square / Hex: horizontal position
#define CC_TRILL_Y 71     // Square / Hex: vertical position
#define CC_TRILL_SIZE 72  // touch size (~ pressure), 0 on release

// Ring only: the two capacitive buttons in the middle send 127 (pressed) / 0
#define CC_TRILL_BUTTON_0 73
#define CC_TRILL_BUTTON_1 74
// raw button reading above which a button counts as pressed — tune with TRILL_DEBUG
#define TRILL_BUTTON_THRESHOLD 400

// Note On while touched, Note Off on release (0 = disabled)
#define ENABLE_TRILL_NOTE 1
#define TRILL_NOTE 60

// Raw touch size range mapped to 0…127. Depends on finger and sensor —
// tune with TRILL_DEBUG enabled.
#define TRILL_SIZE_MIN 0
#define TRILL_SIZE_MAX 4000

// Raw position range per sensor, mapped to 0…127. Change if a position CC
// doesn't reach 0 or 127 at the edge.
#define TRILL_BAR_LOCATION_MAX 3200
#define TRILL_RING_LOCATION_MAX 3584
#define TRILL_FLEX_LOCATION_MAX 3712
#define TRILL_SQUARE_LOCATION_MAX 1792
#define TRILL_HEX_X_LOCATION_MAX 1664
#define TRILL_HEX_Y_LOCATION_MAX 1920

// 1 = flip axis direction
#define TRILL_INVERT_X 0
#define TRILL_INVERT_Y 1  // Square / Hex: "up" on the sensor = higher CC value

// Flex in slider mode (centroid). Values from Bela's flex-print-slider example.
#define TRILL_FLEX_PRESCALER 3
#define TRILL_FLEX_NOISE_THRESHOLD 200

// --- Craft: 30 pads, each one a note ---------------------------------------

// Pad n sends note TRILL_CRAFT_NOTE_BASE + n (pad 0 = note 36, C2)
#define TRILL_CRAFT_NOTE_BASE 36
#define TRILL_CRAFT_VELOCITY 100
// raw pad reading above which a pad counts as touched — tune with TRILL_DEBUG
#define TRILL_CRAFT_THRESHOLD 300

// ----------------------------------------------------------------------------

// Print detected sensor and raw values to Serial
#define TRILL_DEBUG 0

// Minimum interval between sensor reads / MIDI sends (ms)
#define MIDI_SEND_INTERVAL_MS 10
