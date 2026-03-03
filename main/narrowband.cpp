#include "narrowband.h"
#include <RadioLib.h>
#include <queue>
#include <vector>
#include <cstdint>
#include "EspHal.h"
#include "esp_log.h"
#include <cstring>
#include <sys/types.h>
#include <atomic>  // for ISR flag

// flag set in ISR when packet arrives
// atomic operation to avoid data race between interrupt and main thread
static std::atomic<bool> s_received_packet{false};

// radio comms interval
constexpr int RADIO_INTERVAL_MS = 500;

// HAL and radio module pin definitions
constexpr int SCLK_PIN = 14;
constexpr int MISO_PIN = 12;
constexpr int MOSI_PIN = 13;
constexpr int NSS_PIN = 19;
constexpr int DIO1_PIN = 26;
constexpr int NRST_PIN = 18;
constexpr int BUSY_PIN = 21;
constexpr int RXEN_PIN = 16;

class NarrowbandRadio {
private:
    EspHal* hal;
    LLCC68* radio;
    std::queue<std::vector<uint8_t>> cmd_queue;
    std::mutex cmd_queue_mutex;
    
    static NarrowbandRadio* instance;
    
    NarrowbandRadio();
    void handleReceive();
    void enqueueCommand(const std::vector<uint8_t>& cmd);
    
public:
    ~NarrowbandRadio();
    static NarrowbandRadio* getInstance();
    std::vector<uint8_t> dequeueCommand();
    void runThread();
};

NarrowbandRadio* NarrowbandRadio::instance = nullptr;

static const char* TAG = "Narrowband";


// CLASS IMPLEMENTATION

NarrowbandRadio::NarrowbandRadio()
    : hal(nullptr), radio(nullptr) {
    ESP_LOGI(TAG, "[LLCC68] Initializing narrowband radio...");
    
    // create a new instance of the HAL class
    hal = new EspHal(SCLK_PIN, MISO_PIN, MOSI_PIN);

    // now we can create the radio module
    radio = new LLCC68(new Module(hal, NSS_PIN, DIO1_PIN, NRST_PIN, BUSY_PIN));

    // freq 434 Mhz, bitrate 2.4 kHz, frequency deviation 2.4 kHz, receiver bandwidth DSB 11.7 kHz, power 22 dBm, preamble length 32 bit, TCXO voltage 0 V, useRegulatorLDO false
    int state = radio->beginFSK(434, 2.4, 2.4, 11.7, 22, 32, 0, false);
    if (state != RADIOLIB_ERR_NONE) {
        ESP_LOGE(TAG, "beginFSK failed, code %d (fatal)\n", state);
        abort(); // fatal error, cannot continue without radio
    }

    // RXEN pin: 16
    // TXEN pin controlled via dio2
    radio->setRfSwitchPins(RXEN_PIN, RADIOLIB_NC);
    radio->setDio2AsRfSwitch(true);

    ESP_LOGI(TAG, "success!\n");

    // for more details, see LLCC68 datasheet, this is the highest power setting, with 22 dBm set in beginFSK
    state = radio->setPaConfig(0x04, 0x00, 0x07, 0x01);
    if (state != RADIOLIB_ERR_NONE) {
        ESP_LOGE(TAG, "PA config failed, code %d (fatal)\n", state);
        abort();
    }
    ESP_LOGI(TAG, "[LLCC68] PA config configured!\n");

    // configure callback for received packet; must be a free/IRAM-safe function
    radio->setPacketReceivedAction(narrowband_receive_isr);
}

NarrowbandRadio::~NarrowbandRadio() {
    if (hal != nullptr) {
        delete hal;
        hal = nullptr;
    }
    if (radio != nullptr) {
        delete radio;
        radio = nullptr;
    }
}

NarrowbandRadio* NarrowbandRadio::getInstance() {
    if (instance == nullptr) {
        instance = new NarrowbandRadio();
    }
    return instance;
}

void NarrowbandRadio::enqueueCommand(const std::vector<uint8_t>& cmd) {
    std::lock_guard<std::mutex> lock(cmd_queue_mutex);
    cmd_queue.push(cmd);
}

std::vector<uint8_t> NarrowbandRadio::dequeueCommand() {
    std::lock_guard<std::mutex> lock(cmd_queue_mutex);
    if (!cmd_queue.empty()) {
        std::vector<uint8_t> cmd = std::move(cmd_queue.front());
        cmd_queue.pop();
        return cmd;
    }
    return {};
}

// ISR callback stored in IRAM; just sets the atomic flag
extern "C" void IRAM_ATTR narrowband_receive_isr(void) {
    s_received_packet.store(true, std::memory_order_relaxed);
}

void NarrowbandRadio::handleReceive() {

    // check and clear ISR flag
    if (!s_received_packet.load(std::memory_order_relaxed)) {
        return;
    }
    s_received_packet.store(false, std::memory_order_relaxed);

    size_t len = radio->getPacketLength();
    std::vector<uint8_t> buf(len);
    int state = radio->readData(buf.data(), buf.size());

    if (state == RADIOLIB_ERR_NONE) {
        ESP_LOGI(TAG, "Received packet: %s\n", buf.data());
        // maybe parse command here, currently we just enqueue the raw packet data for processing in the main thread
        enqueueCommand(buf);
    } else if (state == RADIOLIB_ERR_CRC_MISMATCH) {
        ESP_LOGI(TAG, "Received packet with CRC mismatch!\n");
    } else {
        ESP_LOGI(TAG, "Failed to read received packet, code %d\n", state);
    }
}

// TODO: add a way to break out of this loop and clean up resources, currently it will run indefinitely
// maybe consider a yield?
void NarrowbandRadio::runThread() {
    if (hal == nullptr || radio == nullptr) {
        ESP_LOGI(TAG, "HAL or radio not initialized!\n");
        return;
    }

    ESP_LOGI(TAG, "[LLCC68] Started narrowband thread!\n");

    int state = RADIOLIB_ERR_NONE;
    while (true) {
        ESP_LOGI(TAG, "[LLCC68] Sending packet...");

        // TODO: replace with sensor data from sensor queue
        state = radio->transmit("Hello world!");
        if (state == RADIOLIB_ERR_NONE) {
            // the packet was successfully transmitted
            ESP_LOGI(TAG, "success!\n");
        } else {
            ESP_LOGI(TAG, "failed, code %d\n", state);
        }
        ESP_LOGI(TAG, "Datarate measured: %f\n", radio->getDataRate());

        state = radio->startReceive();
        if (state == RADIOLIB_ERR_NONE) {
            // the module is now in receive mode, waiting for a packet
            ESP_LOGI(TAG, "Waiting for a packet...\n");
        } else {
            ESP_LOGI(TAG, "failed to start receiver, code %d\n", state);
        }

        // wait 0.5 s
        hal->delay(RADIO_INTERVAL_MS);
        
        // if no packet was received, we just start sending again
        handleReceive();
    }
}

// C COMPATIBILITY WRAPPERS

extern "C" {
    void init_narrowband() {
        NarrowbandRadio::getInstance();
    }

    // Dequeue into caller-provided buffer; returns number of bytes written,
    // 0 if no data, -1 on error or if buffer too small
    ssize_t dequeue_command(uint8_t* buf, size_t max_len) {
        std::vector<uint8_t> v = NarrowbandRadio::getInstance()->dequeueCommand();
        if (v.empty()) {
            return 0;
        }
        if (buf == nullptr || max_len == 0) {
            return -1;
        }
        size_t needed = v.size();
        if (needed > max_len) {
            // Do not truncate silently; signal error
            return -1;
        }
        std::memcpy(buf, v.data(), needed);
        return static_cast<ssize_t>(needed);
    }

    void narrowband_thread() {
        NarrowbandRadio::getInstance()->runThread();
    }

    void destroy_narrowband() {
        if (NarrowbandRadio::instance != nullptr) {
            delete NarrowbandRadio::instance;
            NarrowbandRadio::instance = nullptr;
        }
    }
}
