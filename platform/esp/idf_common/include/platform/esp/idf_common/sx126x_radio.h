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
    // Liveness probe (mutexed wrapper over chip_responsive_locked): false when
    // the SX1262 has gone dark on SPI (version register reads all-0x00/0xFF).
    bool isChipResponsive();
    // Heavy recovery for a chip that died while parked in RX: reset +
    // reconfigure the LoRa stack, then re-arm receive. Returns true on success.
    bool reviveReceive();
    void standby();
    float readRssi();

    int startTransmit(const uint8_t* data, size_t size);
    uint32_t getIrqFlags();
    void clearIrqFlags(uint32_t flags);
    int getPacketLength(bool update);
    int readPacket(uint8_t* buffer, size_t size);

    // RX IRQ ladder instrumentation. Reads the raw IRQ status + the SX126x chip
    // mode (GetStatus bits 6:4) once, then maintains CUMULATIVE counts of how many
    // times the PreambleDetected/HeaderValid/RxDone/CrcErr IRQ bits have latched
    // since boot (rising-edge counted, so a bit that stays set across polls is
    // counted once). It clears ONLY the non-terminal progress bits it owns
    // (PreambleDetected, HeaderValid) so they re-latch for the next packet; the
    // terminal bits (RxDone/CrcErr/HeaderErr/Timeout) are left intact for the
    // adapter's RX poll to consume and clear. Never issues SetRx/SetStandby, so it
    // cannot disturb an in-flight reception. Returns false only if the SPI read
    // failed (e.g. chip dead).
    struct RxLadderCounts
    {
        uint32_t preamble = 0;
        uint32_t header = 0;
        uint32_t rxdone = 0;
        uint32_t crcerr = 0;
        unsigned mode = 0;             // SX126x chip mode (GetStatus bits 6:4); 0x5 = RX
        float peak_rssi_dbm = -128.0f; // peak instantaneous RSSI seen since boot
        uint16_t dev_errors = 0;       // cumulative OR of GetDeviceErrors since boot
        uint32_t polls = 0;            // total ladder polls since boot
        uint32_t notrx_polls = 0;      // polls that observed mode != RX
        uint16_t irq_seen = 0;         // cumulative OR of all IRQ words observed
    };
    // Accumulate the RX IRQ ladder from a caller-provided IRQ snapshot (read once
    // by the adapter's RX poll, shared with its terminal-flag handling so the
    // counting never races a separate read/clear).
    //
    // ROOT-CAUSE-RELEVANT: when deep==false this does NO extra SPI at all -- only the
    // IRQ-edge accounting on the passed-in word -- so the fast (30 ms) poll never
    // injects SPI traffic into an in-flight LoRa reception. The SX1262's demod is
    // disturbed by SPI activity during symbol reception on this board (GetStatus/
    // GetRssiInst/GetDeviceErrors every 30 ms hit the same relative symbol positions
    // each frame and corrupt them deterministically -> RxDone fires but the explicit
    // header demodulates to garbage, CR reads 0, CRC fails every time). Only the
    // throttled (~1 Hz) caller passes deep==true to read chip mode/RSSI/device errors
    // for the diagnostic line; that read cadence is rare enough not to break demod.
    bool pollRxLadder(uint16_t irq, RxLadderCounts* out, bool deep);

    // Post-RxDone localization: read back, over SPI, what the chip ACTUALLY decoded
    // for this just-received LoRa frame -- the header coding rate (REG 0x0749 bits
    // 6:4) and the header CRC-present bit (REG 0x076B bit 4), plus the RxBufferStatus
    // length/offset. Logged as 'idf-mc: rxdec ...'. A sane decoded CR (=1 for 4/5)
    // means the explicit header demodulated correctly and the failure is in the
    // payload; a garbage CR means the header itself mis-decoded. Bounded to a few
    // emissions so it never spams. mutexed.
    void logRxDecodeDiag();

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
    // SX1262 §15.2 PA-clamping workaround (REG 0x08D8 |= 0x1E). Mirrors RadioLib
    // fixPaClamping(); prevents a distorted TX waveform.
    bool fix_pa_clamping_locked();
    // SX1262 §15.4 standard-IQ workaround (REG 0x0736 bit 2 SET), guarded against a
    // garbage read under BUSY so the read-modify-write can never poison the register.
    bool apply_standard_iq_workaround_locked();
    bool set_dio_irq_params_locked(uint16_t irq_mask, uint16_t dio1_mask);
    bool set_dio3_as_tcxo_ctrl_locked(uint8_t voltage_code, uint32_t startup_time_us);
    bool set_dio2_as_rf_switch_locked(bool enable);
    bool set_rx_boosted_gain_locked(bool enable);
    // SX1262 datasheet §15.1 RX-sensitivity workaround (REG 0x0889 bit 2): SET for
    // every LoRa bandwidth except 500 kHz. Mirrors RadioLib fixSensitivity().
    bool fix_rx_sensitivity_locked(float bw_khz);
    uint8_t read_chip_status_locked();
    bool clear_irq_locked(uint16_t flags);
    bool set_buffer_base_locked(uint8_t tx_base, uint8_t rx_base);
    bool set_rx_locked(uint32_t timeout_raw);
    bool set_tx_locked(uint32_t timeout_raw);
    // Arm the radio for LoRa receive (boosted gain + RX IRQs + finite-timeout
    // SetRx). Assumes mutex_ is held. Shared by startReceive() and reviveReceive().
    bool start_receive_locked();
    // Re-issue the LoRa RX packet params (+ the 0x0736 standard-IQ workaround)
    // immediately before SetRx, mirroring RadioLib's startReceiveCommon. Assumes
    // mutex_ is held.
    bool set_rx_packet_params_locked();
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
    // Set by reset_chip_locked() so the next set_rf_frequency_locked() re-runs the
    // band image calibration after a full chip reset (the cached freq is unchanged
    // across the reset, so the >=20MHz delta guard would otherwise skip it and
    // leave the revived receiver unable to correlate preambles).
    bool force_image_cal_ = false;
    uint8_t last_rx_offset_ = 0;
    uint32_t users_ = 0;
    uint32_t rx_diag_count_ = 0;
    uint32_t tx_diag_count_ = 0;
    // Last LoRa SetModulationParams / SetPacketParams bytes actually written, cached
    // so the RX-arm and TX paths can log them back ('idf-mc: rdbk ...') for an
    // RX-vs-TX on-air comparison. SetModulationParams/SetPacketParams are commands
    // with no read-back, so echoing the exact bytes written is the only way to prove
    // the TX modem and the RX demod are programmed identically.
    uint8_t last_mod_bytes_[4] = {0};
    uint8_t last_pkt_bytes_[6] = {0};
    bool have_last_mod_ = false;
    bool have_last_pkt_ = false;
    // One-shot readback emitters (RX arm + TX), gated like the *_diag counters.
    uint32_t rx_rdbk_count_ = 0;
    uint32_t tx_rdbk_count_ = 0;
    uint32_t rxdec_count_ = 0;
    // Cumulative RX IRQ-ladder counters (see pollRxLadder). rxladder_prev_* hold
    // the previous-poll bit state so a latched bit is counted on its rising edge
    // only, never re-counted while it stays set between polls.
    uint32_t rxladder_preamble_ = 0;
    uint32_t rxladder_header_ = 0;
    uint32_t rxladder_rxdone_ = 0;
    uint32_t rxladder_crcerr_ = 0;
    bool rxladder_prev_preamble_ = false;
    bool rxladder_prev_header_ = false;
    bool rxladder_prev_rxdone_ = false;
    bool rxladder_prev_crcerr_ = false;
    float rxladder_peak_rssi_ = -128.0f;
    uint16_t rxladder_dev_errors_ = 0;
    uint32_t rxladder_polls_ = 0;
    uint32_t rxladder_notrx_polls_ = 0;
    uint16_t rxladder_irq_seen_ = 0;
    // Last chip mode read by a deep poll, reported on the cheap (non-deep) polls so
    // the ladder line still carries a mode without an SPI read on the fast path.
    unsigned rxladder_last_mode_ = 0;
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

    // SPI scratch buffers kept OFF the call stack. The dead-chip revive
    // (startTransmit -> reestablish_lora_locked -> reset_chip_locked ->
    // init_locked + configure_lora_locked) runs INLINE on the small LVGL
    // app-loop task stack and nests through write_command_locked /
    // read_command_locked / write_register_locked, each of which otherwise
    // puts a 260-byte (read: two 260-byte) SPI frame on the stack. Hoisting
    // those frames into members shrinks the revive's peak stack footprint
    // substantially. Every *_locked() user of these holds mutex_, and none of
    // them nest a second SPI transfer while a buffer is live, so a single
    // shared tx/rx pair is safe.
    static constexpr size_t kSpiScratchSize = 260;
    uint8_t spi_tx_scratch_[kSpiScratchSize] = {0};
    uint8_t spi_rx_scratch_[kSpiScratchSize] = {0};
};

} // namespace platform::esp::idf_common