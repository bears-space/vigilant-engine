typedef struct {
    uint16_t message_id;
    uint8_t buf_size;
    uint64_t payload;
} Frame;

typedef struct {
    uint16_t message_id;
    uint8_t start_bit; //Depending on device there may be multiple within one frame -> array?
    uint8_t length;    //Depending on device there may be multiple within one frame -> array?
    float factor;
    float offset;
    float min;
    float max;
} SignalDef;

typedef struct {
    uint64_t timestamp_orig;
    uint64_t timestamp_universal;
    float value;
    uint8_t health_bits;
} TelemetryPoint;

// Lock-Free Ring Queue
typedef struct {
    TelemetryPoint data[256];
    _Atomic uint32_t head; // Modified by CAN Interrupt (Producer)
    _Atomic uint32_t tail; // Modified by EKF (Consumer)
} Queue;

const SignalDef dev_lib[8] = {
    [0] = { .start_bit = 0,  .length = 16, .factor = 1.0, .offset = 0 },
    [1] = { .start_bit = 16, .length = 8,  .factor = 0.5, .offset = 0 },
    // ...
};