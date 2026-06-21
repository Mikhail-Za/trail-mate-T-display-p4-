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

    // Hardware RX/TX interrupt-line state (SX126x DIO1), read over a bus that is
    // INDEPENDENT of the radio's own SPI bus so polling it never injects traffic
    // into an in-flight LoRa reception. This is what lets the RX pump service the
    // radio the way RadioLib does -- wait for the DIO1 edge, then read the IRQ over
    // SPI exactly once -- instead of polling GetIrqStatus over the radio SPI every
    // tick (which on the T-Display-P4 SX1262 lands deterministically mid-symbol and
    // corrupts the explicit header: RxDone fires but the length/CR demodulate to
    // garbage and every frame fails CRC). DIO1 is routed to assert on the terminal
    // RX IRQs (RxDone/CrcErr/HeaderErr/Timeout) when the receiver is armed.
    //
    // Returns true only if the board can actually read the line (sets *out_asserted
    // to its level); returns false on boards with no separate-bus IRQ line, in which
    // case the caller MUST fall back to SPI IRQ polling. Default: no line available.
    virtual bool radioIrqLineAsserted(bool* out_asserted)
    {
        (void)out_asserted;
        return false;
    }

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

    // True if the just-latched RX reception decoded NO payload (the FIFO is all-zeros).
    // Used to reject the SX126x post-TX self-reception phantom -- a spurious RxDone on
    // the transmit residual that writes no payload -- before it is counted as a failed
    // (CRC-error) reception. A non-consuming, post-reception SPI peek; the caller only
    // invokes it once a terminal IRQ has latched (never mid-symbol). Default: false
    // (boards without the instrumented driver never report an empty reception).
    virtual bool isRadioRxPayloadEmpty()
    {
        return false;
    }

    // GetStats-driven clean-reception reader. On boards (e.g. T-Display-P4 SX1262)
    // where the modem receives CRC-clean LoRa frames -- its NbPktReceived counter
    // advances with NbPktCrcError == 0 -- yet never raises the RxDone IRQ, the
    // IRQ-gated read path never fires. This hook lets the RX pump deliver such a
    // frame using the modem's own packet counters instead of the dead IRQ: it reads
    // GetStats, and when a NEW clean packet has arrived it reads that packet out of
    // the FIFO (from RxStartBufferPointer) into out_buf and sets *out_len to its
    // length. Returns true and *out_len>0 only when a clean packet was delivered this
    // call, true with *out_len==0 when none is pending, false on read failure. Must be
    // called only when the receiver is idle (no reception in flight) on a throttled
    // cadence. Default: not available (boards without the instrumented driver).
    virtual bool pollRadioCleanRxPacket(uint8_t* out_buf, size_t cap, size_t* out_len)
    {
        (void)out_buf;
        (void)cap;
        if (out_len)
        {
            *out_len = 0;
        }
        return false;
    }
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
