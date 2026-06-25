#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace platform::esp::idf_common
{

// SX1262 radio facade for the ESP-IDF firmware, backed by the vendored RadioLib
// SX126x driver. RadioLib owns the chip for BOTH transmit and receive; this class
// is a thin, mutex-guarded adapter that preserves the EXISTING LoraBoard seam so
// the board layer (TDisplayP4Board) and the MeshCore adapter are unchanged.
//
// Why RadioLib: the previous hand-rolled SX126x command/demod layer corrupted the
// LoRa payload on the LilyGo T-Display-P4 (a virgin listener received a fixed,
// wrong header for every frame while the chip falsely reported CRC-clean), whereas
// RadioLib decodes correctly on this exact hardware. See radiolib_idf_hal.{h,cpp}
// for the ESP-IDF HAL (SPI master + NSS/BUSY GPIO; reset/RF-switch are driven via
// the board's XL9535 expander; DIO1 is polled over SPI, not via an interrupt).
class Sx126xRadio
{
  public:
    static Sx126xRadio& instance();

    bool acquire();
    void release();
    bool isOnline() const;

    // Exclusive-mode hold. An exclusive radio mode (e.g. the walkie-talkie, which
    // reconfigures the SX1262 for FSK) sets this while it owns the chip so the
    // inline mesh/chat radio pump (idf_chat_facade::pumpMeshAndDrainEvents) skips
    // its RX poll + TX drain and never flips the radio back to LoRa receive
    // mid-session. The mutex only guards individual SPI transactions; this flag
    // guards the higher-level mode. Atomic: the holder runs on the walkie path
    // while the pump runs on the main loop task.
    void setExclusiveHold(bool held) { exclusive_hold_.store(held, std::memory_order_release); }
    bool hasExclusiveHold() const { return exclusive_hold_.load(std::memory_order_acquire); }

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

    // Arm RadioLib continuous receive (RF switch -> RX). Returns true on success.
    bool startReceive();
    // Liveness probe. RadioLib owns a healthy chip; reports online state.
    bool isChipResponsive();
    // Heavy recovery hook (re-arm receive). Kept for the LoraBoard contract.
    bool reviveReceive();
    void standby();
    float readRssi();
    float readSnr();

    // Transmit via RadioLib (RF switch -> TX), block until TxDone, then return to
    // RX. Leaves the TxDone IRQ latched so the adapter's TxDone poll observes it.
    // Returns 0 (RADIOLIB_ERR_NONE) on success, -1 on failure.
    int startTransmit(const uint8_t* data, size_t size);

    // Non-blocking transmit launch for the half-duplex walkie path: same modem prep
    // as startTransmit() but issues SetTx and returns immediately (releasing the
    // mutex) instead of busy-polling TxDone through the airtime. The caller observes
    // the latched TxDone via getIrqFlags() on later iterations, so it keeps capturing
    // mic audio during TX (no codec2-frame starvation -> smoother TX). Only safe while
    // the exclusive-hold keeps the mesh pump off the chip. Returns 0 on launch, -1 on
    // failure.
    int startTransmitAsync(const uint8_t* data, size_t size);

    // RX poll surface used by the MeshCore adapter. getIrqFlags() returns the raw
    // SX126x IRQ word (RxDone=0x0002, TxDone=0x0001, CrcErr=0x0040, HeaderErr=0x0020,
    // Timeout=0x0200) read over SPI via RadioLib.
    uint32_t getIrqFlags();
    void clearIrqFlags(uint32_t flags);
    int getPacketLength(bool update);
    // Read the decoded packet out of the FIFO via RadioLib. Returns 0
    // (RADIOLIB_ERR_NONE) on success so the adapter's '== RADIOLIB_ERR_NONE' gate
    // passes; non-zero on failure.
    int readPacket(uint8_t* buffer, size_t size);

    // --- Diagnostics / legacy hooks kept for the LoraBoard interface. With RadioLib
    //     driving the chip the hand-rolled demod workarounds (post-TX self-reception
    //     phantom, GetStats clean-reader, rxladder) are no longer needed; these are
    //     light, SPI-free shims so the unchanged adapter keeps compiling and the RX
    //     delivery flows through the IRQ path (getIrqFlags -> readPacket). ---

    // A RadioLib-decoded frame always carries real payload; never report empty (so
    // the adapter never drops a genuine reception as a post-TX phantom).
    bool isRxPayloadEmpty();

    struct RxLadderCounts
    {
        uint32_t preamble = 0;
        uint32_t header = 0;
        uint32_t rxdone = 0;
        uint32_t crcerr = 0;
        unsigned mode = 0;
        float peak_rssi_dbm = -128.0f;
        uint16_t dev_errors = 0;
        uint32_t polls = 0;
        uint32_t notrx_polls = 0;
        uint16_t irq_seen = 0;
    };
    // Edge-count the RxDone/CrcErr/HeaderErr bits from the caller-supplied IRQ word.
    // Does NO extra SPI (the adapter already read the IRQ), so it never disturbs an
    // in-flight reception. Returns true when the radio is online.
    bool pollRxLadder(uint16_t irq, RxLadderCounts* out, bool deep);

    // The GetStats-driven clean reader was a workaround for the broken demod's dead
    // RxDone IRQ; RadioLib delivers via the normal IRQ path, so this reports
    // "nothing pending" and performs no SPI.
    bool pollCleanRxPacket(uint8_t* out_buf, size_t cap, size_t* out_len);

    // Post-RxDone localization log (no-op with RadioLib).
    void logRxDecodeDiag();

    const char* lastError() const;

  private:
    Sx126xRadio() = default;

    bool init_locked();
    bool begin_lora_locked();
    bool arm_receive_locked();
    void set_error_locked(const char* error);

    void* mutex_ = nullptr;
    bool initialized_ = false;
    bool online_ = false;
    bool lora_configured_ = false;
    uint32_t users_ = 0;
    std::atomic<bool> exclusive_hold_{false};

    // Cached LoRa configuration from the last configureLoRaReceive(), used by
    // begin()/re-arm and the RSSI scale.
    float freq_mhz_ = 0.0f;
    float lora_bw_khz_ = 0.0f;
    uint8_t lora_sf_ = 7;
    uint8_t lora_cr_ = 5;
    int8_t lora_tx_power_ = 20;
    uint16_t lora_preamble_ = 16;
    uint8_t lora_sync_word_ = 0x12;
    uint8_t lora_crc_len_ = 2;

    // RX IRQ-ladder edge state (rising-edge counted), kept so the adapter's
    // diagnostic 'rxladder' line still reports meaningful cumulative counts.
    uint32_t rxladder_rxdone_ = 0;
    uint32_t rxladder_crcerr_ = 0;
    bool rxladder_prev_rxdone_ = false;
    bool rxladder_prev_crcerr_ = false;
    uint16_t rxladder_irq_seen_ = 0;
    uint32_t rxladder_polls_ = 0;

    uint32_t tx_diag_count_ = 0;

    char last_error_[96] = {0};
};

} // namespace platform::esp::idf_common
