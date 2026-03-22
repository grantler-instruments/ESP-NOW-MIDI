#!/bin/bash

# sudo snap install arduino-cli
# curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh
# sudo mv bin/arduino-cli /usr/local/bin/
# arduino-cli version

# arduino-cli core update-index
# arduino-cli core install esp32:esp32
# sudo apt install libstdc++6 libgcc-s1


arduino-cli lib install "AceButton"
arduino-cli lib install https://github.com/sensorium/Mozzi.git
arduino-cli lib install "ESP32Servo"


BOARD="esp32:esp32:lolin_s2_mini"   # FQBN for ESP32-S2 Mini
EXAMPLES_DIR="../examples"           # relative to script location
BUILD_DIR="../.arduino-build"        # keep builds inside repo

passed=0
failed=0

# find all sketch directories containing an .ino file
for sketchdir in $(find "$EXAMPLES_DIR" -name '*.ino' -exec dirname {} \; | sort -u); do
    echo "🔧 Compiling: $sketchdir"
    sketch_name=$(basename "$sketchdir")
    build_path="${BUILD_DIR}/${sketch_name}"
    mkdir -p "$build_path"
    if arduino-cli compile --fqbn $BOARD "$sketchdir" --build-path "$build_path"; then
        echo "✅ Success: $sketchdir"
        ((passed++))
    else
        echo "❌ Failed: $sketchdir"
        ((failed++))
    fi
done

echo "========================"
echo "Compilation finished!"
echo "✅ Passed: $passed"
echo "❌ Failed: $failed"