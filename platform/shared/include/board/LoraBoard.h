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
