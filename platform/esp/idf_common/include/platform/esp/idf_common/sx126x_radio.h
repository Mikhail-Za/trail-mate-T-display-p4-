#pragma once

#include <cstddef>
#include <cstdint>

namespace platform::esp::idf_common
{

class Sx126xRadio
{
  public:
    static Sx126xRadio& instance();

    bool acquire();
    void release();
    bool isOnline() const;

    bool configureLoRaReceive(float freq_mhz,
                              float bw_khz,
                              uint8_t sf,
                              uint8_t cr,
                              int8_t tx_power,
                              uint16_t preamble_len,
                              uint8_t sync_word,
                              uint8_t crc_len);

    bool configureFsk(float freq_mhz,
                      int8_t tx_power,
                      float bit_rate_kbps,
                      float freq_dev_khz,
                      float rx_bw_khz,
                      uint16_t preamble_len,
                      const uint8_t* sync_word,
                      size_t sync_word_len,
                      uint8_t crc_len);

    bool startReceive();
    void standby();
    float readRssi();

    int startTransmit(const uint8_t* data, size_t size);
    uint32_t getIrqFlags();
    void clearIrqFlags(uint32_t flags);
    int getPacketLength(bool update);
    int readPacket(uint8_t* buffer, size_t size);

    const char* lastError() const;

  private:
    Sx126xRadio() = default;

    bool init_locked();
    bool probe_locked();
    void wait_ready_locked() const;
    bool write_command_locked(uint8_t cmd, const uint8_t* data, size_t size, bool wait = true);
    bool read_command_locked(uint8_t cmd,
                             const uint8_t* prefix,
                             size_t prefix_size,
                             uint8_t* data,
                             size_t size,
                             bool wait = true);
    bool write_register_locked(uint16_t addr, const uint8_t* data, size_t size);
    bool read_register_locked(uint16_t addr, uint8_t* data, size_t size);
    bool set_packet_type_locked(uint8_t packet_type);
    bool set_rf_frequency_locked(float freq_mhz);
    bool set_tx_power_locked(int8_t tx_power);
    bool set_dio_irq_params_locked(uint16_t irq_mask, uint16_t dio1_mask);
    bool set_dio3_as_tcxo_ctrl_locked(uint8_t voltage_code, uint32_t startup_time_us);
    bool set_dio2_as_rf_switch_locked(bool enable);
    bool set_rx_boosted_gain_locked(bool enable);
    uint8_t read_chip_status_locked();
    bool clear_irq_locked(uint16_t flags);
    bool set_buffer_base_locked(uint8_t tx_base, uint8_t rx_base);
    bool set_rx_locked(uint32_t timeout_raw);
    bool set_tx_locked(uint32_t timeout_raw);
    bool configure_lora_locked(float freq_mhz,
                               float bw_khz,
                               uint8_t sf,
                               uint8_t cr,
                               int8_t tx_power,
                               uint16_t preamble_len,
                               uint8_t sync_word,
                               uint8_t crc_len);
    bool configure_fsk_locked(float freq_mhz,
                              int8_t tx_power,
                              float bit_rate_kbps,
                              float freq_dev_khz,
                              float rx_bw_khz,
                              uint16_t preamble_len,
                              const uint8_t* sync_word,
                              size_t sync_word_len,
                              uint8_t crc_len);
    void set_error_locked(const char* error);
    bool chip_responsive_locked();
    bool reset_chip_locked();
    bool reestablish_lora_locked();

    void* mutex_ = nullptr;
    void* device_ = nullptr;
    bool initialized_ = false;
    bool online_ = false;
    uint8_t packet_type_ = 0xFF;
    float freq_mhz_ = 0.0f;
    uint8_t last_rx_offset_ = 0;
    uint32_t users_ = 0;
    uint32_t rx_diag_count_ = 0;
    uint32_t tx_diag_count_ = 0;
    // Cached LoRa configuration from the last configureLoRaReceive(), so the TX
    // path can re-establish the radio if the chip has lost its state (the
    // T-Display-P4 SX1262 goes fully dark -- version register reads 0x00 -- after
    // sustained continuous RX, so a transmit must reconfigure it first).
    bool lora_cfg_valid_ = false;
    float lora_bw_khz_ = 0.0f;
    uint8_t lora_sf_ = 0;
    uint8_t lora_cr_ = 0;
    int8_t lora_tx_power_ = 0;
    uint16_t lora_preamble_ = 0;
    uint8_t lora_sync_word_ = 0;
    uint8_t lora_crc_len_ = 0;
    char last_error_[96] = {0};
};

} // namespace platform::esp::idf_common