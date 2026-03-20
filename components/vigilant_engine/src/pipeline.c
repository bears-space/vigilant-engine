/*
Concept:
- Frames from sensors include: id, timestamp of data, timestamp of sending

Flow:
- Hardware interrupt for captured package
- Offset calculated for universal time, time corrected, health bit attached, index calculated
- Path A: Pushed to websocket queue based on index (downsampled)
- Path B: Pushed to EKF for processing and afterwards action

Interface to Interrupt:
- I need: (preferably converted) buf_size (from DLC)

Open questions:
- Are node IDs included in message ID (I am assuming yes)
*/

#include <stdint.h>
#include <stdatomic.h>
#include <stdbool.h>
#include "pipeline.h"


//onFrameReceived callback (parses into TelemetryPoint):
//1. Use DBC file or similar convention to convert bits into data
//   Identify start bit (shift to it), read x bits for data, 
//2. Modify data with offsets (int to float, add offset)
//3. Calculate and apply universal timestamp
//4. Attach health info & validate

uint64_t unpack_raw(const uint64_t* data, uint8_t start, uint8_t len, uint8_t buf_size) {
    uint64_t raw_data = 0;

    uint8_t byte_idx = start / 8;
    uint8_t bit_shift = start % 8;

    //Defines where payload to copy is
    uint8_t bytes_to_copy = (buf_size - byte_idx > 8) ? 8 : (buf_size - byte_idx);

    //Copies to payload raw_data
    memcpy(&raw_data, &data[byte_idx], sizeof(uint64_t));

//TODO: Add edge case handling for len=64 for mask
    uint64_t mask = (1ULL << len) - 1;
    uint64_t signal = (raw_data >> bit_shift) & mask;

    return signal;
}

float scale_value(uint64_t raw, float factor, float offset) {
//TODO: Check against min, max, etc.
    return ((float)raw * factor) + offset;
}

//Select SignalDef based on Device ID extracted from Message ID
const SignalDef* get_definition(uint16_t can_id) {
    //Device ID in MSB
    uint8_t device_idx = (can_id >> 8) & 0x07;
    
    // Safety check
    if (device_idx < 8) {
        return &dev_lib[device_idx];
    }

//TODO: How to error handling?
    return 1; 
}

void onFrameCallback(const Frame frame) {
    SignalDef* device = get_definition(frame.message_id);

    uint64_t signal = unpack_raw(frame.payload, device->start_bit, device->length, frame.buf_size);

    scale_value(signal, device->factor, device->offset);

//3. Calculate and apply universal timestamp
//4. Attach health info & validate
}

//EKF Thread
//Pops data
//Does stuff
//Sends action because of data