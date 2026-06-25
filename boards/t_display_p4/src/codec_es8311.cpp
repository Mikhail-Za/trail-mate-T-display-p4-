#include "boards/t_display_p4/codec_es8311.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "boards/t_display_p4/t_display_p4_board.h"

#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_err.h"
#include "esp_log.h"

extern "C"
{
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_codec_dev_types.h"
#include "es8311_codec.h"
}

namespace boards::t_display_p4
{
namespace
{
constexpr const char* kTag = "TDisplayP4Codec";

// I2S link parameters for the ES8311 path. The application stream the walkie
// service runs is mono; the I2S STD link is a 2-slot (stereo) link, so read()/
// write() fold/duplicate between the two (mirrors Tab5 CodecCompat).
constexpr i2s_port_t kI2sPort = I2S_NUM_0;
constexpr uint32_t kDefaultSampleRate = 8000;

// Map a CodecEs8311 board profile audio pin (which may be -1) to a gpio_num_t.
gpio_num_t to_gpio(int pin)
{
    return (pin >= 0) ? static_cast<gpio_num_t>(pin) : GPIO_NUM_NC;
}

} // namespace

CodecEs8311::~CodecEs8311()
{
    close();
    if (scratch_)
    {
        std::free(scratch_);
        scratch_ = nullptr;
        scratch_size_ = 0;
    }
}

bool CodecEs8311::enableAudioPower()
{
    TDisplayP4Board& board = TDisplayP4Board::instance();
    const auto& io = TDisplayP4Board::ioExpanderPins();
    const bool active_high = TDisplayP4Board::profile().p4_vcca_active_high;
    if (!board.expanderPinMode(io.p4_vcca, true))
    {
        ESP_LOGW(kTag, "VCCA pin mode set failed (p4_vcca=%d)", io.p4_vcca);
        return false;
    }
    if (!board.expanderWriteActive(io.p4_vcca, true, active_high))
    {
        ESP_LOGW(kTag, "VCCA enable failed (p4_vcca=%d active_high=%d)", io.p4_vcca, active_high ? 1 : 0);
        return false;
    }
    return true;
}

bool CodecEs8311::probeChipId(uint8_t* out_id1, uint8_t* out_id2)
{
    if (out_id1 == nullptr || out_id2 == nullptr)
    {
        return false;
    }
    *out_id1 = 0;
    *out_id2 = 0;

    // The ES8311 analog supply must be powered before it answers on I2C.
    (void)enableAudioPower();

    TDisplayP4Board& board = TDisplayP4Board::instance();
    i2c_master_bus_handle_t bus = board.externalI2cHandle();
    if (bus == nullptr)
    {
        ESP_LOGW(kTag, "External I2C bus unavailable for ES8311 probe");
        return false;
    }

    // Raw-read the chip-ID registers at the TRUE 7-bit address (0x18) directly
    // over the external bus, independent of esp_codec_dev's 8-bit/`>>1` address
    // convention. Add a temporary device, read 0xFD then 0xFE, then remove it so
    // the probe leaves no driver state behind (it is run inside the boot
    // self-test's enter()/exit()).
    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = kI2c7BitAddr;
    dev_cfg.scl_speed_hz = 100000;

    i2c_master_dev_handle_t dev = nullptr;
    if (i2c_master_bus_add_device(bus, &dev_cfg, &dev) != ESP_OK || dev == nullptr)
    {
        ESP_LOGW(kTag, "Failed to add ES8311 probe device at 0x%02X", kI2c7BitAddr);
        return false;
    }

    bool ok = true;
    uint8_t reg = kRegChipId1;
    uint8_t id1 = 0;
    if (i2c_master_transmit_receive(dev, &reg, 1, &id1, 1, 100) != ESP_OK)
    {
        ok = false;
    }
    uint8_t id2 = 0;
    if (ok)
    {
        reg = kRegChipId2;
        if (i2c_master_transmit_receive(dev, &reg, 1, &id2, 1, 100) != ESP_OK)
        {
            ok = false;
        }
    }

    (void)i2c_master_bus_rm_device(dev);

    if (ok)
    {
        *out_id1 = id1;
        *out_id2 = id2;
    }
    return ok;
}

bool CodecEs8311::ensureI2s()
{
    if (i2s_tx_ != nullptr && i2s_rx_ != nullptr)
    {
        return true;
    }

    // One I2S STD channel pair (TX + RX) on a single port, ESP as master. The
    // codec_dev I2S data interface drives enable/disable of these channels when
    // the OUT/IN devices are opened/closed, so they are created (REGISTERED) but
    // left disabled here.
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(kI2sPort, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    if (i2s_new_channel(&chan_cfg, &i2s_tx_, &i2s_rx_) != ESP_OK)
    {
        ESP_LOGE(kTag, "i2s_new_channel failed");
        i2s_tx_ = nullptr;
        i2s_rx_ = nullptr;
        return false;
    }

    const auto& pins = TDisplayP4Board::profile().audio_i2s;
    const i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(requested_sample_rate_);
    const i2s_std_slot_config_t slot_cfg =
        I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
    i2s_std_config_t std_cfg = {};
    std_cfg.clk_cfg = clk_cfg;
    std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;  // MCLK = 256 * fs
    std_cfg.slot_cfg = slot_cfg;
    std_cfg.gpio_cfg.mclk = to_gpio(pins.mclk);
    std_cfg.gpio_cfg.bclk = to_gpio(pins.bclk);
    std_cfg.gpio_cfg.ws = to_gpio(pins.ws);
    std_cfg.gpio_cfg.dout = to_gpio(pins.dout);
    std_cfg.gpio_cfg.din = to_gpio(pins.din);
    std_cfg.gpio_cfg.invert_flags.mclk_inv = 0;
    std_cfg.gpio_cfg.invert_flags.bclk_inv = 0;
    std_cfg.gpio_cfg.invert_flags.ws_inv = 0;

    if (i2s_channel_init_std_mode(i2s_tx_, &std_cfg) != ESP_OK ||
        i2s_channel_init_std_mode(i2s_rx_, &std_cfg) != ESP_OK)
    {
        ESP_LOGE(kTag, "i2s_channel_init_std_mode failed");
        teardownI2s();
        return false;
    }

    hardware_channels_ = 2;  // STD link runs 2 slots; mono folds in read/write.
    return true;
}

void CodecEs8311::teardownI2s()
{
    if (i2s_tx_ != nullptr)
    {
        (void)i2s_channel_disable(i2s_tx_);
        (void)i2s_del_channel(i2s_tx_);
        i2s_tx_ = nullptr;
    }
    if (i2s_rx_ != nullptr)
    {
        (void)i2s_channel_disable(i2s_rx_);
        (void)i2s_del_channel(i2s_rx_);
        i2s_rx_ = nullptr;
    }
}

bool CodecEs8311::ensureCodec()
{
    if (codec_ready_)
    {
        return true;
    }
    if (!enableAudioPower())
    {
        return false;
    }
    if (!ensureI2s())
    {
        return false;
    }

    TDisplayP4Board& board = TDisplayP4Board::instance();
    i2c_master_bus_handle_t bus = board.externalI2cHandle();
    if (bus == nullptr)
    {
        ESP_LOGE(kTag, "External I2C bus unavailable for ES8311 codec");
        teardownI2s();
        return false;
    }

    // I2C control interface. esp_codec_dev expects the 8-bit address and applies
    // `device_address = (addr >> 1)`, so kCodecDevAddr (0x30) targets 0x18.
    audio_codec_i2c_cfg_t i2c_cfg = {};
    i2c_cfg.port = static_cast<uint8_t>(TDisplayP4Board::externalI2c().port);
    i2c_cfg.addr = kCodecDevAddr;
    i2c_cfg.bus_handle = bus;
    ctrl_if_ = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (ctrl_if_ == nullptr)
    {
        ESP_LOGE(kTag, "audio_codec_new_i2c_ctrl failed");
        teardownI2s();
        return false;
    }

    gpio_if_ = audio_codec_new_gpio();

    // I2S data interface bound to the channels created above.
    audio_codec_i2s_cfg_t i2s_data_cfg = {};
    i2s_data_cfg.port = static_cast<uint8_t>(kI2sPort);
    i2s_data_cfg.tx_handle = i2s_tx_;
    i2s_data_cfg.rx_handle = i2s_rx_;
    data_if_ = audio_codec_new_i2s_data(&i2s_data_cfg);
    if (data_if_ == nullptr)
    {
        ESP_LOGE(kTag, "audio_codec_new_i2s_data failed");
        teardownCodec();
        teardownI2s();
        return false;
    }

    // ES8311 codec interface: ESP is I2S master (codec slave), external MCLK fed
    // at 256x. No PA pin here (pa_pin = -1): the board's speaker supply/VCCA is
    // gated separately through the XL9535 expander, not an ES8311 GPIO.
    es8311_codec_cfg_t es_cfg = {};
    es_cfg.ctrl_if = static_cast<const audio_codec_ctrl_if_t*>(ctrl_if_);
    es_cfg.gpio_if = static_cast<const audio_codec_gpio_if_t*>(gpio_if_);
    es_cfg.codec_mode = ESP_CODEC_DEV_WORK_MODE_BOTH;
    es_cfg.pa_pin = -1;
    es_cfg.pa_reverted = false;
    es_cfg.master_mode = false;  // ESP master / codec slave
    es_cfg.use_mclk = true;      // external MCLK on pin 13
    es_cfg.digital_mic = false;
    es_cfg.invert_mclk = false;
    es_cfg.invert_sclk = false;
    es_cfg.hw_gain.pa_voltage = 5.0f;
    es_cfg.hw_gain.codec_dac_voltage = 3.3f;
    es_cfg.hw_gain.pa_gain = 0.0f;
    es_cfg.mclk_div = 256;
    codec_if_ = es8311_codec_new(&es_cfg);
    if (codec_if_ == nullptr)
    {
        ESP_LOGE(kTag, "es8311_codec_new failed (ES8311 not responding?)");
        teardownCodec();
        teardownI2s();
        return false;
    }

    // Two esp_codec_dev handles sharing the one ES8311 codec_if + I2S data_if:
    // OUT (speaker / DAC) and IN (microphone / ADC).
    esp_codec_dev_cfg_t out_cfg = {};
    out_cfg.dev_type = ESP_CODEC_DEV_TYPE_OUT;
    out_cfg.codec_if = static_cast<const audio_codec_if_t*>(codec_if_);
    out_cfg.data_if = static_cast<const audio_codec_data_if_t*>(data_if_);
    out_dev_ = esp_codec_dev_new(&out_cfg);

    esp_codec_dev_cfg_t in_cfg = {};
    in_cfg.dev_type = ESP_CODEC_DEV_TYPE_IN;
    in_cfg.codec_if = static_cast<const audio_codec_if_t*>(codec_if_);
    in_cfg.data_if = static_cast<const audio_codec_data_if_t*>(data_if_);
    in_dev_ = esp_codec_dev_new(&in_cfg);

    if (out_dev_ == nullptr || in_dev_ == nullptr)
    {
        ESP_LOGE(kTag, "esp_codec_dev_new failed");
        teardownCodec();
        teardownI2s();
        return false;
    }

    codec_ready_ = true;
    ESP_LOGI(kTag, "ES8311 codec ready (i2s port=%d mclk=%d bclk=%d ws=%d dout=%d din=%d)",
             static_cast<int>(kI2sPort), TDisplayP4Board::profile().audio_i2s.mclk,
             TDisplayP4Board::profile().audio_i2s.bclk, TDisplayP4Board::profile().audio_i2s.ws,
             TDisplayP4Board::profile().audio_i2s.dout, TDisplayP4Board::profile().audio_i2s.din);
    return true;
}

void CodecEs8311::teardownCodec()
{
    if (out_dev_ != nullptr)
    {
        esp_codec_dev_delete(static_cast<esp_codec_dev_handle_t>(out_dev_));
        out_dev_ = nullptr;
    }
    if (in_dev_ != nullptr)
    {
        esp_codec_dev_delete(static_cast<esp_codec_dev_handle_t>(in_dev_));
        in_dev_ = nullptr;
    }
    if (codec_if_ != nullptr)
    {
        audio_codec_delete_codec_if(static_cast<const audio_codec_if_t*>(codec_if_));
        codec_if_ = nullptr;
    }
    if (data_if_ != nullptr)
    {
        audio_codec_delete_data_if(static_cast<const audio_codec_data_if_t*>(data_if_));
        data_if_ = nullptr;
    }
    if (ctrl_if_ != nullptr)
    {
        audio_codec_delete_ctrl_if(static_cast<const audio_codec_ctrl_if_t*>(ctrl_if_));
        ctrl_if_ = nullptr;
    }
    if (gpio_if_ != nullptr)
    {
        audio_codec_delete_gpio_if(static_cast<const audio_codec_gpio_if_t*>(gpio_if_));
        gpio_if_ = nullptr;
    }
    codec_ready_ = false;
    out_open_ = false;
    in_open_ = false;
}

void CodecEs8311::applyRuntimeSettings()
{
    if (out_dev_ != nullptr)
    {
        (void)esp_codec_dev_set_out_vol(static_cast<esp_codec_dev_handle_t>(out_dev_), volume_);
        (void)esp_codec_dev_set_out_mute(static_cast<esp_codec_dev_handle_t>(out_dev_),
                                         mute_ || out_mute_);
    }
    if (in_dev_ != nullptr)
    {
        (void)esp_codec_dev_set_in_gain(static_cast<esp_codec_dev_handle_t>(in_dev_), gain_db_);
    }
}

int CodecEs8311::open(uint8_t bits_per_sample, uint8_t channel, uint32_t sample_rate)
{
    requested_bits_ = bits_per_sample != 0 ? bits_per_sample : 16;
    requested_channels_ = channel != 0 ? channel : 1;
    requested_sample_rate_ = sample_rate != 0 ? sample_rate : kDefaultSampleRate;

    if (!ensureCodec())
    {
        return -1;
    }

    // The I2S STD link runs a fixed 16-bit stereo frame; the application stream
    // may be mono. Open both esp_codec_dev handles with the hardware frame and
    // fold/duplicate mono<->stereo in read()/write().
    esp_codec_dev_sample_info_t fs = {};
    fs.bits_per_sample = 16;
    fs.channel = hardware_channels_;
    fs.channel_mask = 0;
    fs.sample_rate = requested_sample_rate_;
    fs.mclk_multiple = 0;  // default 256x

    int rc = esp_codec_dev_open(static_cast<esp_codec_dev_handle_t>(out_dev_), &fs);
    if (rc != ESP_CODEC_DEV_OK)
    {
        ESP_LOGW(kTag, "open OUT failed rc=%d", rc);
        return rc;
    }
    out_open_ = true;

    rc = esp_codec_dev_open(static_cast<esp_codec_dev_handle_t>(in_dev_), &fs);
    if (rc != ESP_CODEC_DEV_OK)
    {
        ESP_LOGW(kTag, "open IN failed rc=%d", rc);
        esp_codec_dev_close(static_cast<esp_codec_dev_handle_t>(out_dev_));
        out_open_ = false;
        return rc;
    }
    in_open_ = true;

    applyRuntimeSettings();
    return 0;
}

void CodecEs8311::close()
{
    if (out_open_ && out_dev_ != nullptr)
    {
        (void)esp_codec_dev_set_out_mute(static_cast<esp_codec_dev_handle_t>(out_dev_), true);
        (void)esp_codec_dev_close(static_cast<esp_codec_dev_handle_t>(out_dev_));
        out_open_ = false;
    }
    if (in_open_ && in_dev_ != nullptr)
    {
        (void)esp_codec_dev_close(static_cast<esp_codec_dev_handle_t>(in_dev_));
        in_open_ = false;
    }
    teardownCodec();
    teardownI2s();
}

bool CodecEs8311::ensure_scratch(size_t size)
{
    if (scratch_size_ >= size)
    {
        return true;
    }
    void* resized = std::realloc(scratch_, size);
    if (!resized)
    {
        return false;
    }
    scratch_ = resized;
    scratch_size_ = size;
    return true;
}

int CodecEs8311::write(uint8_t* buffer, size_t size)
{
    if (!out_open_ || out_dev_ == nullptr || buffer == nullptr || size == 0)
    {
        return -1;
    }

    // Mono application stream into a stereo hardware link: duplicate each 16-bit
    // sample into the L and R slots (mirrors Tab5 CodecCompat duplication).
    if (requested_channels_ <= 1 && hardware_channels_ > 1)
    {
        const size_t frames = size / sizeof(int16_t);
        const size_t stereo_bytes = frames * hardware_channels_ * sizeof(int16_t);
        if (!ensure_scratch(stereo_bytes))
        {
            return -1;
        }
        const auto* in = static_cast<const int16_t*>(static_cast<const void*>(buffer));
        auto* out = static_cast<int16_t*>(scratch_);
        for (size_t i = 0; i < frames; ++i)
        {
            const int16_t s = in[i];
            for (uint8_t ch = 0; ch < hardware_channels_; ++ch)
            {
                out[i * hardware_channels_ + ch] = s;
            }
        }
        return esp_codec_dev_write(static_cast<esp_codec_dev_handle_t>(out_dev_), out,
                                   static_cast<int>(stereo_bytes));
    }

    return esp_codec_dev_write(static_cast<esp_codec_dev_handle_t>(out_dev_), buffer,
                               static_cast<int>(size));
}

int CodecEs8311::read(uint8_t* buffer, size_t size)
{
    if (!in_open_ || in_dev_ == nullptr || buffer == nullptr || size == 0)
    {
        return -1;
    }

    // Stereo hardware capture folded down to a mono application stream: read the
    // wider frame into scratch, then average the hardware channels per frame
    // (mirrors Tab5 CodecCompat folding).
    if (requested_channels_ <= 1 && hardware_channels_ > 1)
    {
        const size_t requested_samples = size / sizeof(int16_t);
        const size_t capture_bytes = requested_samples * hardware_channels_ * sizeof(int16_t);
        if (!ensure_scratch(capture_bytes))
        {
            return -1;
        }
        const int rc = esp_codec_dev_read(static_cast<esp_codec_dev_handle_t>(in_dev_), scratch_,
                                          static_cast<int>(capture_bytes));
        if (rc != ESP_CODEC_DEV_OK)
        {
            return rc;
        }
        auto* out = static_cast<int16_t*>(static_cast<void*>(buffer));
        const auto* in = static_cast<const int16_t*>(scratch_);
        for (size_t i = 0; i < requested_samples; ++i)
        {
            int32_t mixed = 0;
            for (uint8_t ch = 0; ch < hardware_channels_; ++ch)
            {
                mixed += in[i * hardware_channels_ + ch];
            }
            out[i] = static_cast<int16_t>(mixed / static_cast<int32_t>(hardware_channels_));
        }
        return 0;
    }

    return esp_codec_dev_read(static_cast<esp_codec_dev_handle_t>(in_dev_), buffer,
                              static_cast<int>(size));
}

void CodecEs8311::setMute(bool enable)
{
    mute_ = enable;
    if (out_dev_ != nullptr)
    {
        (void)esp_codec_dev_set_out_mute(static_cast<esp_codec_dev_handle_t>(out_dev_),
                                         mute_ || out_mute_);
    }
}

bool CodecEs8311::getMute() const
{
    return mute_;
}

void CodecEs8311::setOutMute(bool enable)
{
    out_mute_ = enable;
    if (out_dev_ != nullptr)
    {
        (void)esp_codec_dev_set_out_mute(static_cast<esp_codec_dev_handle_t>(out_dev_),
                                         mute_ || out_mute_);
    }
}

bool CodecEs8311::getOutMute() const
{
    return out_mute_;
}

void CodecEs8311::setVolume(uint8_t level)
{
    volume_ = level;
    if (out_dev_ != nullptr)
    {
        (void)esp_codec_dev_set_out_vol(static_cast<esp_codec_dev_handle_t>(out_dev_), volume_);
    }
}

int CodecEs8311::getVolume() const
{
    return volume_;
}

void CodecEs8311::setGain(float db_value)
{
    gain_db_ = db_value;
    if (in_dev_ != nullptr)
    {
        (void)esp_codec_dev_set_in_gain(static_cast<esp_codec_dev_handle_t>(in_dev_), gain_db_);
    }
}

float CodecEs8311::getGain() const
{
    return gain_db_;
}

bool CodecEs8311::ready() const
{
    return codec_ready_ && out_open_ && in_open_;
}

} // namespace boards::t_display_p4
