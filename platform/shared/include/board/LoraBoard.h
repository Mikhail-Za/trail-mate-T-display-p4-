#pragma once

#include <cstddef>
#include <cstdint>

// Shared LoRa board capability contract.
class LoraBoard
{
  public:
    virtual ~LoraBoard() = default;

    virtual bool isRadioOnline() const = 0;

    virtual int transmitRadio(const uint8_t* data, size_t len) = 0;
    virtual int startRadioReceive() = 0;

    // Liveness probe for boards whose radio can silently die on the SPI bus
    // (the T-Display-P4 SX1262 goes dark in sustained continuous RX). Default:
    // alive whenever the radio reports online. Boards with the dead-in-RX quirk
    // override this with a real chip probe.
    virtual bool isRadioChipAlive()
    {
        return isRadioOnline();
    }
    // Heavy recovery when isRadioChipAlive() reports the radio dead: reset +
    // reconfigure + re-arm receive. Default: just re-arm (no quirk to recover).
    // Returns 0 on success (same convention as startRadioReceive()).
    virtual int reviveRadioReceive()
    {
        return startRadioReceive();
    }
    virtual uint32_t getRadioIrqFlags() = 0;

    // RX IRQ-ladder probe (diagnostic). Cumulative counts of how many times the
    // PreambleDetected/HeaderValid/RxDone/CrcErr IRQ bits have latched since boot,
    // plus the current radio chip mode (SX126x GetStatus bits 6:4; 0x5 = RX).
    // Default: report nothing observed and mode 0 (boards without the instrumented
    // SX1262 driver). Returns true if the counts were read from a live radio.
    struct RadioRxLadder
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
    // deep==true permits the (rare, ~1 Hz) diagnostic SPI reads of chip mode / RSSI /
    // device errors. deep==false (the fast 30 ms poll) must do NO chip SPI beyond the
    // already-read IRQ word, so it never disturbs an in-flight LoRa reception.
    virtual bool pollRadioRxLadder(uint32_t irq, RadioRxLadder* out, bool deep)
    {
        (void)irq;
        (void)out;
        (void)deep;
        return false;
    }

    // Post-RxDone localization hook: log what the demodulator actually decoded for
    // the just-received LoRa frame (header coding rate / CRC flag / buffer status).
    // Default no-op for boards without the instrumented SX1262 driver.
    virtual void logRadioRxDecode() {}

    virtual int getRadioPacketLength(bool update) = 0;
    virtual int readRadioData(uint8_t* buf, size_t len) = 0;
    virtual void clearRadioIrqFlags(uint32_t flags) = 0;
    virtual float getRadioRSSI() = 0;
    virtual float getRadioInstantRSSI()
    {
        return getRadioRSSI();
    }
    virtual float getRadioSNR() = 0;

    // Board-specific LoRa configuration without exposing SX126x types.
    virtual void configureLoraRadio(float freq_mhz, float bw_khz, uint8_t sf, uint8_t cr_denom,
                                    int8_t tx_power, uint16_t preamble_len, uint8_t sync_word,
                                    uint8_t crc_len) = 0;
};
