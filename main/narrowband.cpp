#include "narrowband.h"
#include <RadioLib.h>
#include "EspHal.h"

// HAL and radio module pin definitions
constexpr int SCLK_PIN = 14;
constexpr int MISO_PIN = 12;
constexpr int MOSI_PIN = 13;

constexpr int NSS_PIN = 19;
constexpr int DIO1_PIN = 26;
constexpr int NRST_PIN = 18;
constexpr int BUSY_PIN = 21;


static EspHal* s_hal = nullptr;
static LLCC68* s_radio = nullptr;

void init_narrowband() {
    
    // create a new instance of the HAL class
    s_hal = new EspHal(SCLK_PIN, MISO_PIN, MOSI_PIN);

    // now we can create the radio module
    s_radio = new LLCC68(new Module(s_hal, NSS_PIN, DIO1_PIN, NRST_PIN, BUSY_PIN));
}

void send_narrowband(const char* data, int len) {

    // TODO: take last element from sensor data queue and send it
    //s_radio->send(data, len);
}

int recv_narrowband(char* buf, int len) {

    // TODO: put received cmd into cmd queue
    //return s_radio->recv(buf, len);
    return 0;
}

void destroy_narrowband() {
    // free hal
    delete s_hal;
    s_hal = nullptr;

    // free radio
    delete s_radio;
    s_radio = nullptr;
}
