// Feature toggles — 1 = send this CC / read sensor as needed, 0 = off
#define ENABLE_MPU6500_ACCEL_X 0
#define ENABLE_MPU6500_ACCEL_Y 0
#define ENABLE_MPU6500_ACCEL_Z 0
#define ENABLE_MPU6500_ACCEL_SUM 1  // x + y + z (g)
#define ENABLE_MPU6500_GYRO_X 0
#define ENABLE_MPU6500_GYRO_Y 0
#define ENABLE_MPU6500_GYRO_Z 0
#define ENABLE_MPU6500_TEMP 0

// Derived — used by the sketch to skip I2C reads when a whole group is off
#define MPU6500_ANY_ACCEL_AXIS (ENABLE_MPU6500_ACCEL_X || ENABLE_MPU6500_ACCEL_Y || ENABLE_MPU6500_ACCEL_Z)
#define MPU6500_NEED_ACCEL (MPU6500_ANY_ACCEL_AXIS || ENABLE_MPU6500_ACCEL_SUM)
#define MPU6500_ANY_GYRO (ENABLE_MPU6500_GYRO_X || ENABLE_MPU6500_GYRO_Y || ENABLE_MPU6500_GYRO_Z)

// MIDI CC numbers for sensor axes (adjust to taste / your DAW mapping)
#define CC_MPU6500_ACCEL_X 60
#define CC_MPU6500_ACCEL_Y 61
#define CC_MPU6500_ACCEL_Z 62
#define CC_MPU6500_ACCEL_SUM 67
#define CC_MPU6500_GYRO_X 63
#define CC_MPU6500_GYRO_Y 64
#define CC_MPU6500_GYRO_Z 65
#define CC_MPU6500_TEMP 66

#define MIDI_CHANNEL 1

// Minimum interval between sensor reads / CC sends (ms)
#define MIDI_SEND_INTERVAL_MS 20
