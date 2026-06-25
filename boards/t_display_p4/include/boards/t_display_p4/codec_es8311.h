#pragma once

#include <cstddef>
#include <cstdint>

#include "driver/i2c_master.h"
#include "driver/i2s_types.h"

// ES8311 audio codec wrapper for the T-Display P4 (ESP32-P4) IDF build.
//
// This class exposes the SAME method surface the idf_common walkie codec
// wrappers call on the Tab5 `CodecCompat` (open/close/read/write/setVolume/
// setGain/setMute/setOutMute/ready, plus the matching getters), so the shared
// walkie service can drive P4 audio through an identical interface. It is
// backed by Espressif's esp_codec_dev ES8311 driver (es8311_codec_new +
// esp_codec_dev_*), an I2S STD master link, and the board's EXTERNAL I2C bus.
//
// Hardware facts (T-Display P4):
//   - ES8311 sits on the EXTERNAL I2C bus (port 1, SDA 20 / SCL 21).
//   - 7-bit I2C address is 0x18. esp_codec_dev's I2C control interface takes the
//     address in 8-BIT form and internally right-shifts it (`addr >> 1`), so the
//     codec ctrl is created with 0x30 (0x30 >> 1 == 0x18). The raw presence
//     probe instead talks the 7-bit 0x18 directly over the bus.
//   - I2S STD pins: BCLK 12, MCLK 13, WS 9, DOUT 10, DIN 11; MCLK = 256 * fs,
//     16-bit, ESP is the I2S/clock master (codec is slave), external MCLK fed.
//   - Audio analog supply VCCA is gated by the XL9535 expander pin `p4_vcca`
//     (logical pin 10), active-LOW. It must be enabled before the codec responds.

namespace boards::t_display_p4
{

class CodecEs8311
{
  public:
    // ES8311 7-bit I2C address on the external bus.
    static constexpr uint8_t kI2c7BitAddr = 0x18;
    // 8-bit form handed to esp_codec_dev's I2C control interface, which performs
    // `device_address = (addr >> 1)`; (0x30 >> 1) == 0x18.
    static constexpr uint8_t kCodecDevAddr = 0x30;
    // Chip-ID registers (read-only): 0xFD == 0x83, 0xFE == 0x11 on a real ES8311.
    static constexpr uint8_t kRegChipId1 = 0xFD;
    static constexpr uint8_t kRegChipId2 = 0xFE;

    CodecEs8311() = default;
    ~CodecEs8311();

    CodecEs8311(const CodecEs8311&) = delete;
    CodecEs8311& operator=(const CodecEs8311&) = delete;

    // Open/close the full codec (I2C ctrl + I2S link + OUT/IN esp_codec_dev
    // handles). Mirrors CodecCompat::open semantics: bits_per_sample/channel/
    // sample_rate describe the application stream; mono<->stereo folding is
    // handled in read()/write(). Returns 0 on success, non-zero esp_codec_dev
    // error otherwise.
    int open(uint8_t bits_per_sample, uint8_t channel, uint32_t sample_rate);
    void close();

    // Write playback samples to the speaker (DAC) path. If the application
    // stream is mono but the hardware link is stereo, each sample is duplicated
    // into both slots (exactly like CodecCompat folds/duplicates).
    int write(uint8_t* buffer, size_t size);
    // Read capture samples from the microphone (ADC) path. If the application
    // stream is mono but the hardware link captures more channels, the extra
    // channels are averaged down to mono.
    int read(uint8_t* buffer, size_t size);

    void setMute(bool enable);
    bool getMute() const;

    void setOutMute(bool enable);
    bool getOutMute() const;

    void setVolume(uint8_t level);
    int getVolume() const;

    void setGain(float db_value);
    float getGain() const;

    bool ready() const;

    // Enable the analog audio supply (VCCA) through the XL9535 expander
    // (active-low). Safe to call repeatedly. Returns true on success.
    bool enableAudioPower();

    // Boot-time presence probe: enable VCCA, then raw-read the ES8311 chip-ID
    // registers (0xFD, 0xFE) directly at 7-bit address 0x18 over the external
    // I2C bus and tear the temporary device down. Returns true and fills both
    // out bytes on success; false if the chip does not respond. Crash-free and
    // leaves no driver state behind (suitable for the systest enter() probe).
    bool probeChipId(uint8_t* out_id1, uint8_t* out_id2);

  private:
    bool ensureI2s();
    void teardownI2s();
    bool ensureCodec();
    void teardownCodec();
    bool ensure_scratch(size_t size);
    void applyRuntimeSettings();

    // esp_codec_dev handles (opaque void* to keep this header free of the C
    // driver's internal types).
    void* out_dev_ = nullptr;  // esp_codec_dev_handle_t (DAC / speaker)
    void* in_dev_ = nullptr;   // esp_codec_dev_handle_t (ADC / microphone)
    const void* codec_if_ = nullptr;  // audio_codec_if_t* (shared es8311)
    const void* ctrl_if_ = nullptr;   // audio_codec_ctrl_if_t* (I2C)
    const void* data_if_ = nullptr;   // audio_codec_data_if_t* (I2S)
    const void* gpio_if_ = nullptr;   // audio_codec_gpio_if_t*

    i2s_chan_handle_t i2s_tx_ = nullptr;
    i2s_chan_handle_t i2s_rx_ = nullptr;

    void* scratch_ = nullptr;
    size_t scratch_size_ = 0;

    bool codec_ready_ = false;
    bool out_open_ = false;
    bool in_open_ = false;

    bool mute_ = false;
    bool out_mute_ = false;
    int volume_ = 70;
    float gain_db_ = 24.0f;
    uint8_t requested_channels_ = 1;
    uint8_t requested_bits_ = 16;
    uint32_t requested_sample_rate_ = 8000;
    uint8_t hardware_channels_ = 2;
};

} // namespace boards::t_display_p4
