#include "platform/esp/idf_common/sx126x_radio.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "boards/t_display_p4/t_display_p4_board.h"
#include "boards/tab5/tab5_board.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace platform::esp::idf_common
{
namespace
{

constexpr const char* kTag = "idf-sx126x";
constexpr uint8_t kCmdSetStandby = 0x80;
constexpr uint8_t kCmdSetRx = 0x82;
constexpr uint8_t kCmdSetTx = 0x83;
constexpr uint8_t kCmdSetPacketType = 0x8A;
constexpr uint8_t kCmdSetRfFrequency = 0x86;
constexpr uint8_t kCmdSetTxParams = 0x8E;
constexpr uint8_t kCmdSetModulationParams = 0x8B;
constexpr uint8_t kCmdSetPacketParams = 0x8C;
constexpr uint8_t kCmdSetBufferBaseAddress = 0x8F;
constexpr uint8_t kCmdSetRegulatorMode = 0x96;
constexpr uint8_t kCmdSetPaConfig = 0x95;
constexpr uint8_t kCmdSetDioIrqParams = 0x08;
constexpr uint8_t kCmdGetIrqStatus = 0x12;
constexpr uint8_t kCmdClearIrqStatus = 0x02;
constexpr uint8_t kCmdSetDio2AsRfSwitchCtrl = 0x9D;
constexpr uint8_t kCmdSetDio3AsTcxoCtrl = 0x97;
constexpr uint8_t kCmdGetStatus = 0xC0;
constexpr uint8_t kCmdGetStats = 0x10;
constexpr uint8_t kCmdGetDeviceErrors = 0x17;
constexpr uint8_t kCmdClearDeviceErrors = 0x07;
constexpr uint8_t kCmdGetRssiInst = 0x15;
constexpr uint8_t kCmdGetRxBufferStatus = 0x13;
constexpr uint8_t kCmdReadBuffer = 0x1E;
constexpr uint8_t kCmdWriteBuffer = 0x0E;
constexpr uint8_t kCmdCalibrateImage = 0x98;
constexpr uint8_t kCmdSetRxTxFallbackMode = 0x93;
constexpr uint8_t kCmdCalibrate = 0x89;
constexpr uint8_t kCmdReadRegister = 0x1D;
constexpr uint8_t kCmdWriteRegister = 0x0D;

constexpr uint8_t kPacketTypeGfsk = 0x00;
constexpr uint8_t kPacketTypeLoRa = 0x01;
constexpr uint8_t kStandbyRc = 0x00;
constexpr uint8_t kRegulatorDcDc = 0x01;
constexpr uint8_t kFallbackStandbyRc = 0x20;
constexpr uint8_t kPaRamp200u = 0x04;
constexpr uint8_t kPaConfigDeviceSelSx1262 = 0x00;
constexpr uint8_t kPaConfigPaLut = 0x01;
constexpr uint8_t kLoRaHeaderExplicit = 0x00;
constexpr uint8_t kLoRaCrcOff = 0x00;
constexpr uint8_t kLoRaCrcOn = 0x01;
constexpr uint8_t kLoRaIqStandard = 0x00;
constexpr uint8_t kFskFilterNone = 0x00;
constexpr uint8_t kFskPacketVariable = 0x01;
constexpr uint8_t kFskAddressFilterOff = 0x00;
constexpr uint8_t kFskWhiteningOff = 0x00;
constexpr uint8_t kFskPreambleDetectOff = 0x00;
constexpr uint8_t kFskPreambleDetect8 = 0x04;
constexpr uint8_t kFskPreambleDetect16 = 0x05;
constexpr uint8_t kFskPreambleDetect24 = 0x06;
constexpr uint8_t kFskPreambleDetect32 = 0x07;
constexpr uint8_t kFskCrcOff = 0x01;
constexpr uint8_t kFskCrc2ByteInv = 0x06;
constexpr uint32_t kRxTimeoutInf = 0xFFFFFF;
constexpr uint16_t kIrqTxDone = 0x0001;
constexpr uint16_t kIrqRxDone = 0x0002;
constexpr uint16_t kIrqPreambleDetected = 0x0004;
constexpr uint16_t kIrqSyncWordValid = 0x0008;
constexpr uint16_t kIrqHeaderValid = 0x0010;
constexpr uint16_t kIrqHeaderErr = 0x0020;
constexpr uint16_t kIrqCrcErr = 0x0040;
constexpr uint16_t kIrqTimeout = 0x0200;
constexpr uint16_t kIrqAll = 0x43FF;
// RX DIO IRQ mask. The terminal flags (RxDone/Timeout/CrcErr/HeaderErr) drive the
// adapter's RX poll; PreambleDetected and HeaderValid are added purely so they
// LATCH in GetIrqStatus for the rxladder instrumentation (they map to no terminal
// action). Widening the IRQ mask does not change the demodulator behaviour -- it
// only makes the progress flags observable so the RX chain can be localized.
constexpr uint16_t kIrqRxLadderMask = kIrqRxDone | kIrqTimeout | kIrqCrcErr |
                                      kIrqHeaderErr | kIrqPreambleDetected |
                                      kIrqHeaderValid;
constexpr uint16_t kRegOcpConfiguration = 0x08E7;
constexpr uint16_t kRegSyncWord0 = 0x06C0;
constexpr uint16_t kRegLoraSyncWordMsb = 0x0740;
constexpr uint16_t kRegCrcInitialMsb = 0x06BC;
constexpr uint16_t kRegCrcPolynomialMsb = 0x06BE;
constexpr uint16_t kRegVersionString = 0x0320;
constexpr uint16_t kRegRxGain = 0x08AC;
constexpr uint16_t kRegRxGainRetention0 = 0x029F;
// SX1262 datasheet §15.1 RX-sensitivity / TxModulation register. Bit 2 must be
// SET for every LoRa bandwidth except 500 kHz (RadioLib fixSensitivity()).
constexpr uint16_t kRegSensitivityConfig = 0x0889;
constexpr uint16_t kRegIqConfig = 0x0736;
// LoRa RX decode-status registers (RadioLib getPacketStatus/getLoRaConfig). After
// RxDone these reflect what the demodulator actually decoded from the received
// explicit header: 0x0749 bits 6:4 = received coding rate; 0x076B bit 4 = received
// CRC-present flag. Reading them post-RxDone localizes a header mis-decode.
constexpr uint16_t kRegLoraRxCodingRate = 0x0749;
constexpr uint16_t kRegFreqErrorRxCrc = 0x076B;
// SX1262 datasheet §15.2 PA-clamping register (RadioLib fixPaClamping). Applied in
// RadioLib's SX1262::begin on every config. Without it the PA can over-clamp and
// DISTORT the transmitted LoRa signal, so a receiver locks the preamble (RxDone)
// but the demodulated symbols carry bit errors -> wrong header length + CRC fail at
// strong RSSI. Read-modify-write: set bits 4:1 (|= 0x1E).
constexpr uint16_t kRegTxClampConfig = 0x08D8;
// Undocumented SX1262 RX-improvement register patch (Meshtastic SX126xInterface:
// "recommended by Heltec/Semtech for improved RX"; sets bit 0 of 0x08B5). The proven
// on-this-board Meshtastic receiver applies it on every config; our exhaustive
// register diff omitted it. It tunes the SX1262 RX front-end/AGC; without it the
// demod can mis-track and produce bit errors. Add it to match the reference.
constexpr uint16_t kRegRxImprovement = 0x08B5;
constexpr uint8_t kRxGainBoosted = 0x96;
constexpr uint8_t kRxGainPowerSaving = 0x94;
// DIO3 TCXO control voltage code 0x00 = 1.6V (datasheet SetDIO3AsTCXOCtrl table).
// RadioLib and the LilyGo cpp_bus_driver both default the SX1262 TCXO to 1.6V
// with a 5ms startup; the board feeds the TCXO from DIO3.
constexpr uint8_t kTcxoVoltage1V6 = 0x00;
constexpr uint32_t kTcxoStartupTimeUs = 5000;
constexpr float kFrequencyStepHz = 0.9536743164f;
constexpr uint32_t kCrystalFreqHz = 32000000UL;
// Upper bound for waiting on the SX1262 TxDone IRQ inside startTransmit. The
// largest MeshCore frame at the slowest supported advert rate (SF7/BW62.5) takes
// roughly 0.7s to complete on this board; 1.5s leaves generous headroom while
// still guaranteeing the call returns if a transmit ever wedges.
constexpr int64_t kTxCompleteTimeoutUs = 1500000;
// SetDIO3AsTCXOCtrl / SetRx timeouts are expressed in 15.625us RTC steps.
uint32_t microseconds_to_rtc_step(uint32_t time_us)
{
    constexpr uint32_t kNumerator = 64;
    constexpr uint32_t kDenominator = 1000;
    const uint64_t steps =
        (static_cast<uint64_t>(time_us) * kNumerator + (kDenominator - 1)) / kDenominator;
    return static_cast<uint32_t>(steps);
}

const char* spi_host_name(int host)
{
    switch (static_cast<spi_host_device_t>(host))
    {
    case SPI2_HOST:
        return "SPI2_HOST";
    case SPI3_HOST:
        return "SPI3_HOST";
    default:
        return "INVALID_SPI_HOST";
    }
}

bool is_supported_spi_host(int host)
{
    return host == static_cast<int>(SPI2_HOST) || host == static_cast<int>(SPI3_HOST);
}

spi_device_handle_t device_handle(void* raw)
{
    return reinterpret_cast<spi_device_handle_t>(raw);
}

SemaphoreHandle_t radio_mutex(void* raw)
{
    return reinterpret_cast<SemaphoreHandle_t>(raw);
}

bool take_mutex(void* raw)
{
    if (!raw)
    {
        return false;
    }
    return xSemaphoreTake(radio_mutex(raw), portMAX_DELAY) == pdTRUE;
}

void give_mutex(void* raw)
{
    if (raw)
    {
        xSemaphoreGive(radio_mutex(raw));
    }
}

uint8_t map_lora_bw(float bw_khz)
{
    const float half = bw_khz / 2.0f;
    const int bw_div2 = static_cast<int>(half + 0.01f);
    switch (bw_div2)
    {
    case 3:
        return 0x00;
    case 5:
        return 0x08;
    case 7:
        return 0x01;
    case 10:
        return 0x09;
    case 15:
        return 0x02;
    case 20:
        return 0x0A;
    case 31:
        return 0x03;
    case 62:
        return 0x04;
    case 125:
        return 0x05;
    case 250:
        return 0x06;
    default:
        return 0x04;
    }
}

uint8_t map_fsk_rx_bw(float rx_bw_khz)
{
    struct Entry
    {
        float bw;
        uint8_t code;
    };
    static constexpr Entry table[] = {
        {4.8f, 0x1F},
        {5.8f, 0x17},
        {7.3f, 0x0F},
        {9.7f, 0x1E},
        {11.7f, 0x16},
        {14.6f, 0x0E},
        {19.5f, 0x1D},
        {23.4f, 0x15},
        {29.3f, 0x0D},
        {39.0f, 0x1C},
        {46.9f, 0x14},
        {58.6f, 0x0C},
        {78.2f, 0x1B},
        {93.8f, 0x13},
        {117.3f, 0x0B},
        {156.2f, 0x1A},
        {187.2f, 0x12},
        {234.3f, 0x0A},
        {312.0f, 0x19},
        {373.6f, 0x11},
        {467.0f, 0x09},
    };

    for (const auto& entry : table)
    {
        if (std::fabs(entry.bw - rx_bw_khz) <= 0.001f)
        {
            return entry.code;
        }
    }
    return 0x1A;
}

uint8_t map_preamble_detect(uint16_t preamble_len, size_t sync_bits)
{
    const size_t max_detect = std::min<size_t>(sync_bits, preamble_len);
    if (max_detect >= 32)
    {
        return kFskPreambleDetect32;
    }
    if (max_detect >= 24)
    {
        return kFskPreambleDetect24;
    }
    if (max_detect >= 16)
    {
        return kFskPreambleDetect16;
    }
    if (max_detect > 0)
    {
        return kFskPreambleDetect8;
    }
    return kFskPreambleDetectOff;
}

uint8_t map_lora_cr(uint8_t cr)
{
    if (cr < 5)
    {
        return 0x01;
    }
    if (cr > 8)
    {
        return 0x04;
    }
    return static_cast<uint8_t>(cr - 4);
}

uint8_t calc_ldro(uint8_t sf, float bw_khz)
{
    const float symbol_ms = (static_cast<float>(1UL << sf) / bw_khz);
    return symbol_ms >= 16.0f ? 0x01 : 0x00;
}

uint32_t fsk_bitrate_raw(float bit_rate_kbps)
{
    return static_cast<uint32_t>((static_cast<double>(kCrystalFreqHz) * 32.0) /
                                 (static_cast<double>(bit_rate_kbps) * 1000.0));
}

uint32_t fsk_freq_dev_raw(float freq_dev_khz)
{
    return static_cast<uint32_t>(((static_cast<double>(freq_dev_khz) * 1000.0) *
                                  static_cast<double>(1UL << 25)) /
                                 static_cast<double>(kCrystalFreqHz));
}

uint32_t rf_frequency_raw(float freq_mhz)
{
    return static_cast<uint32_t>((static_cast<double>(freq_mhz) * 1000000.0) /
                                 static_cast<double>(kFrequencyStepHz));
}

uint8_t ocp_for_60ma()
{
    return static_cast<uint8_t>(60.0f / 2.5f);
}

// SX1262 optimized PA-configuration lookup (RadioLib SX1262.cpp paOptTable, from
// the radiolib power-tests campaign). Indexed by output power + 9 (range -9..+22 ->
// 0..31). Each entry is the {paDutyCycle, hpMax, paVal} the SX1262 PA needs for that
// target power. The previous firmware HARDCODED the +22 dBm config (paDutyCycle=4,
// hpMax=7) for every power while driving SetTxParams at the requested (e.g. +14)
// power -- a mismatched PA operating point that distorts the modulated output and
// makes the receiver demodulate the peer with bit errors. Use the per-power table
// exactly as RadioLib does on this exact board.
struct PaOptEntry
{
    uint8_t pa_duty_cycle;
    uint8_t hp_max;
    int8_t pa_val;
};
constexpr PaOptEntry kPaOptTable[32] = {
    {2, 2, -5}, {2, 1, 0},  {1, 1, 3},  {1, 2, 0},  {1, 1, 6},  {1, 2, 3},
    {2, 2, 2},  {4, 1, 6},  {1, 1, 11}, {2, 1, 11}, {1, 1, 14}, {2, 1, 14},
    {1, 1, 20}, {1, 1, 22}, {2, 2, 11}, {3, 1, 21}, {1, 2, 17}, {4, 2, 13},
    {1, 2, 20}, {1, 2, 22}, {2, 2, 21}, {3, 2, 21}, {1, 4, 19}, {1, 4, 20},
    {3, 3, 20}, {2, 5, 19}, {1, 6, 22}, {2, 5, 22}, {3, 5, 22}, {3, 6, 22},
    {4, 6, 22}, {4, 7, 22},
};

const auto& lora_pins()
{
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    return ::boards::tab5::Tab5Board::loraModulePins();
#elif defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    return ::boards::t_display_p4::TDisplayP4Board::loraModulePins();
#else
    return ::boards::tab5::Tab5Board::loraModulePins();
#endif
}

bool prepare_board_lora_runtime()
{
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    return true;
#elif defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    return ::boards::t_display_p4::TDisplayP4Board::instance().prepareLoraRuntime();
#else
    return false;
#endif
}

bool board_uses_internal_dio2_rf_switch()
{
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    return true;
#elif defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    // ROOT CAUSE of the deaf receiver: DIO2 is NOT broken out on the T-Display-P4,
    // and its SKY13453 antenna switch is driven STATICALLY by XL9535 IO1 (held
    // high = RF1). The proven-on-this-exact-board Meshtastic variant therefore does
    // NOT define SX126X_DIO2_AS_RF_SWITCH, i.e. it calls setDio2AsRfSwitch(FALSE)
    // (only the sibling crowpanel-advanced-p4 board, which DOES wire DIO2 to its
    // switch, enables it). When DIO2-as-RF-switch is ENABLED on a board with no
    // DIO2-driven switch, the SX1262 internally gates its RX/TX front end off a
    // DIO2 line that drives nothing, leaving the LNA/receive path mis-routed: the
    // chip couples enough stray energy to bump RSSI (it even hears a strong peer
    // burst at ~-47 dBm) but the demodulator never sees a clean preamble, so
    // GetStats stays rx=0/crc=0/hdr=0 and PreambleDetected/RxDone never fire. With
    // DIO2-as-RF-switch DISABLED the SX1262 keeps its default internal front-end
    // routing through the statically-asserted SKY13453, which is the configuration
    // the vendor RadioLib/Meshtastic receiver uses on this board.
    return false;
#else
    return false;
#endif
}

bool set_board_lora_reset_asserted(bool asserted)
{
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    if (lora_pins().rst < 0)
    {
        return false;
    }
    gpio_set_level(static_cast<gpio_num_t>(lora_pins().rst), asserted ? 0 : 1);
    return true;
#elif defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    return ::boards::t_display_p4::TDisplayP4Board::instance().setLoraResetAsserted(asserted);
#else
    (void)asserted;
    return false;
#endif
}

void board_prepare_lora_direction(bool transmit)
{
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    (void)::boards::t_display_p4::TDisplayP4Board::instance().setLoraRfSwitchTransmit(transmit);
#else
    (void)transmit;
#endif
}

} // namespace

Sx126xRadio& Sx126xRadio::instance()
{
    static Sx126xRadio radio;
    return radio;
}

bool Sx126xRadio::acquire()
{
#if !defined(TRAIL_MATE_ESP_BOARD_TAB5) && !defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    return false;
#else
    if (!mutex_)
    {
        mutex_ = xSemaphoreCreateMutex();
        if (!mutex_)
        {
            return false;
        }
    }

    if (!take_mutex(mutex_))
    {
        return false;
    }

    const bool ok = init_locked();
    if (ok)
    {
        ++users_;
    }
    give_mutex(mutex_);
    return ok;
#endif
}

void Sx126xRadio::release()
{
    if (!take_mutex(mutex_))
    {
        return;
    }

    if (users_ > 0)
    {
        --users_;
    }
    if (users_ == 0 && online_)
    {
        const uint8_t mode = kStandbyRc;
        write_command_locked(kCmdSetStandby, &mode, 1, true);
    }
    give_mutex(mutex_);
}

bool Sx126xRadio::isOnline() const
{
    return online_;
}

bool Sx126xRadio::init_locked()
{
#if !defined(TRAIL_MATE_ESP_BOARD_TAB5) && !defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    set_error_locked("SX126x radio unavailable on this board");
    return false;
#else
    if (initialized_)
    {
        return online_;
    }

    spi_bus_config_t bus_cfg{};
    bus_cfg.mosi_io_num = lora_pins().spi.mosi;
    bus_cfg.miso_io_num = lora_pins().spi.miso;
    bus_cfg.sclk_io_num = lora_pins().spi.sck;
    bus_cfg.quadwp_io_num = -1;
    bus_cfg.quadhd_io_num = -1;
    bus_cfg.max_transfer_sz = 260;

    if (!is_supported_spi_host(lora_pins().spi.host))
    {
        ESP_LOGE(kTag,
                 "SX126x init rejected invalid SPI host=%d (%s); expected SPI2_HOST=%d or SPI3_HOST=%d",
                 lora_pins().spi.host,
                 spi_host_name(lora_pins().spi.host),
                 static_cast<int>(SPI2_HOST),
                 static_cast<int>(SPI3_HOST));
        set_error_locked("invalid spi host");
        return false;
    }

    const auto host = static_cast<spi_host_device_t>(lora_pins().spi.host);
    ESP_LOGI(kTag, "SX126x init: host=%d(%s) sck=%d miso=%d mosi=%d nss=%d rst=%d irq=%d busy=%d pwr_en=%d",
             lora_pins().spi.host,
             spi_host_name(lora_pins().spi.host),
             lora_pins().spi.sck,
             lora_pins().spi.miso,
             lora_pins().spi.mosi,
             lora_pins().nss,
             lora_pins().rst,
             lora_pins().irq,
             lora_pins().busy,
             lora_pins().pwr_en);
    esp_err_t err = spi_bus_initialize(host, &bus_cfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        set_error_locked("spi_bus_initialize failed");
        return false;
    }

    if (!device_)
    {
        spi_device_interface_config_t dev_cfg{};
        dev_cfg.mode = 0;
        dev_cfg.clock_speed_hz = 4000000;
        dev_cfg.spics_io_num = lora_pins().nss;
        dev_cfg.queue_size = 1;
        dev_cfg.flags = 0;
        spi_device_handle_t handle = nullptr;
        err = spi_bus_add_device(host, &dev_cfg, &handle);
        if (err == ESP_OK)
        {
            device_ = handle;
        }
        if (err != ESP_OK)
        {
            set_error_locked("spi_bus_add_device failed");
            return false;
        }
    }

    if (!prepare_board_lora_runtime())
    {
        set_error_locked("board LoRa runtime setup failed");
        return false;
    }

#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
    gpio_config_t io_cfg{};
    io_cfg.pin_bit_mask = (1ULL << lora_pins().rst);
    io_cfg.mode = GPIO_MODE_OUTPUT;
    io_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_cfg);
#endif

    if (lora_pins().irq >= 0)
    {
        gpio_config_t irq_cfg{};
        irq_cfg.pin_bit_mask = (1ULL << lora_pins().irq);
        irq_cfg.mode = GPIO_MODE_INPUT;
        irq_cfg.pull_up_en = GPIO_PULLUP_DISABLE;
        irq_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
        gpio_config(&irq_cfg);
    }

    if (lora_pins().busy >= 0)
    {
        gpio_config_t busy_cfg{};
        busy_cfg.pin_bit_mask = (1ULL << lora_pins().busy);
        busy_cfg.mode = GPIO_MODE_INPUT;
        busy_cfg.pull_up_en = GPIO_PULLUP_DISABLE;
        // The proven on-this-board receivers (LilyGo cpp_bus_driver RadioLib
        // example -> SetGpioMode(SX1262_BUSY, kPulldown); Meshtastic t-display-p4
        // variant -> pinMode(6, INPUT_PULLDOWN)) configure BUSY with a PULLDOWN.
        // With no pull the BUSY line can read stuck-high in the gap when the SX1262
        // releases it, which wedges wait_ready_locked()'s BUSY handshake: commands
        // then issue into a chip that ignores them (the receiver reports mode=RX but
        // its modem never runs). Match the vendor and pull BUSY down.
        busy_cfg.pull_down_en = GPIO_PULLDOWN_ENABLE;
        gpio_config(&busy_cfg);
    }

    if (!set_board_lora_reset_asserted(true))
    {
        set_error_locked("radio reset assert failed");
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(2));
    if (!set_board_lora_reset_asserted(false))
    {
        set_error_locked("radio reset release failed");
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(10));

    initialized_ = true;
    online_ = probe_locked();
    if (!online_)
    {
        set_error_locked("SX126x probe failed");
        return false;
    }

    const uint8_t standby_rc = kStandbyRc;
    write_command_locked(kCmdSetStandby, &standby_rc, 1, true);

    set_packet_type_locked(kPacketTypeLoRa);
    set_buffer_base_locked(0x00, 0x00);
    const uint8_t regulator = kRegulatorDcDc;
    write_command_locked(kCmdSetRegulatorMode, &regulator, 1, true);

    // The LilyGo T-Display-P4 powers the SX1262 reference oscillator from a TCXO
    // on DIO3 (DIO3 is not broken out for any other use). It must be enabled and
    // allowed to settle BEFORE calibration: empirically, without DIO3-TCXO the
    // chip cannot sustain RX and drops back to standby (mode 2) with a dead
    // -127 dBm floor; with it enabled the chip reaches RX (mode 5) and the LNA
    // sees real RF. (The XOSC_START_ERR latched at cold boot is a benign
    // power-on artifact present either way and is cleared below.)
    set_dio3_as_tcxo_ctrl_locked(kTcxoVoltage1V6, kTcxoStartupTimeUs);

    // Recalibrate everything now that the TCXO is the active reference, then
    // clear the stale cold-boot oscillator/PLL error flags.
    uint8_t calibrate = 0x7F;
    write_command_locked(kCmdCalibrate, &calibrate, 1, true);
    vTaskDelay(pdMS_TO_TICKS(5));
    wait_ready_locked();
    {
        const uint8_t no_data[2] = {0x00, 0x00};
        write_command_locked(kCmdClearDeviceErrors, no_data, sizeof(no_data), true);
    }

    // RF-switch control. On boards where DIO2 is wired to an external antenna
    // switch (e.g. Tab5, crowpanel-advanced-p4) the SX1262 must drive it via
    // DIO2-as-RF-switch. On the T-Display-P4 DIO2 is NOT broken out and the
    // SKY13453 is driven statically by XL9535 IO1, so DIO2-as-RF-switch must be
    // DISABLED (matching the proven Meshtastic t-display-p4 variant, which omits
    // SX126X_DIO2_AS_RF_SWITCH). Enabling it on this board mis-routes the receive
    // front-end and is why the demodulator senses RF energy but never correlates a
    // preamble. Set it EXPLICITLY either way so a stale value left by a previously
    // booted firmware (e.g. MeshOS) cannot leak in.
    set_dio2_as_rf_switch_locked(board_uses_internal_dio2_rf_switch());
    const uint8_t fallback = kFallbackStandbyRc;
    write_command_locked(kCmdSetRxTxFallbackMode, &fallback, 1, true);
    clear_irq_locked(kIrqAll);

    const uint8_t ocp = ocp_for_60ma();
    write_register_locked(kRegOcpConfiguration, &ocp, 1);

    const uint8_t status = read_chip_status_locked();
    uint8_t dev_err[2] = {0};
    (void)read_command_locked(kCmdGetDeviceErrors, nullptr, 0, dev_err, sizeof(dev_err), true);
    const uint16_t errors = (static_cast<uint16_t>(dev_err[0]) << 8) | dev_err[1];
    ESP_LOGI(kTag,
             "SX1262 init complete: status=0x%02X mode=%u tcxo=on dio2_rf_switch=%d dev_errors=0x%04X",
             status,
             static_cast<unsigned>((status >> 4) & 0x07),
             board_uses_internal_dio2_rf_switch() ? 1 : 0,
             static_cast<unsigned>(errors));
    return true;
#endif
}

bool Sx126xRadio::probe_locked()
{
#if !defined(TRAIL_MATE_ESP_BOARD_TAB5) && !defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    return false;
#else
    uint8_t version[6] = {0};
    if (!read_register_locked(kRegVersionString, version, sizeof(version)))
    {
        return false;
    }
    bool all_zero = true;
    bool all_ff = true;
    for (uint8_t value : version)
    {
        all_zero = all_zero && value == 0x00;
        all_ff = all_ff && value == 0xFF;
    }
    if (all_zero || all_ff)
    {
        ESP_LOGW(kTag, "SX126x probe failed: version register returned invalid data (all_zero=%d all_ff=%d)", all_zero ? 1 : 0, all_ff ? 1 : 0);
        return false;
    }
    ESP_LOGI(kTag, "SX126x probe ok: version bytes=%02X %02X %02X %02X %02X %02X",
             version[0], version[1], version[2], version[3], version[4], version[5]);
    return true;
#endif
}

void Sx126xRadio::wait_ready_locked() const
{
#if defined(TRAIL_MATE_ESP_BOARD_TAB5) || defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    if (lora_pins().busy >= 0)
    {
        const TickType_t start = xTaskGetTickCount();
        while (gpio_get_level(static_cast<gpio_num_t>(lora_pins().busy)) != 0)
        {
            if ((xTaskGetTickCount() - start) > pdMS_TO_TICKS(50))
            {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
    else
    {
        esp_rom_delay_us(200);
    }
#endif
}

bool Sx126xRadio::write_command_locked(uint8_t cmd, const uint8_t* data, size_t size, bool wait)
{
#if !defined(TRAIL_MATE_ESP_BOARD_TAB5) && !defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    (void)cmd;
    (void)data;
    (void)size;
    (void)wait;
    return false;
#else
    wait_ready_locked();

    const size_t total = 1 + size;
    // Use the shared member SPI scratch (held under mutex_) instead of a stack
    // buffer to keep the inline dead-chip revive within the app-loop stack.
    uint8_t* tx = spi_tx_scratch_;
    if (total > kSpiScratchSize)
    {
        set_error_locked("command too large");
        return false;
    }
    std::memset(tx, 0, total);
    tx[0] = cmd;
    if (data && size > 0)
    {
        std::memcpy(tx + 1, data, size);
    }

    spi_transaction_t trans{};
    trans.length = total * 8;
    trans.tx_buffer = tx;
    const esp_err_t err = spi_device_transmit(device_handle(device_), &trans);
    if (err != ESP_OK)
    {
        set_error_locked("spi write failed");
        return false;
    }

    if (wait)
    {
        wait_ready_locked();
    }
    return true;
#endif
}

bool Sx126xRadio::read_command_locked(uint8_t cmd,
                                      const uint8_t* prefix,
                                      size_t prefix_size,
                                      uint8_t* data,
                                      size_t size,
                                      bool wait)
{
#if !defined(TRAIL_MATE_ESP_BOARD_TAB5) && !defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    (void)cmd;
    (void)prefix;
    (void)prefix_size;
    (void)data;
    (void)size;
    (void)wait;
    return false;
#else
    wait_ready_locked();

    const size_t total = 1 + prefix_size + 1 + size;
    // Use the shared member SPI scratch (held under mutex_) instead of two
    // 260-byte stack buffers. This is the single largest stack frame on the
    // inline dead-chip revive path (init_locked reads device errors through
    // here), so hoisting it off the stack is the biggest peak-stack saving.
    uint8_t* tx = spi_tx_scratch_;
    uint8_t* rx = spi_rx_scratch_;
    if (total > kSpiScratchSize)
    {
        set_error_locked("command too large");
        return false;
    }
    std::memset(tx, 0, total);
    std::memset(rx, 0, total);

    tx[0] = cmd;
    if (prefix && prefix_size > 0)
    {
        std::memcpy(tx + 1, prefix, prefix_size);
    }

    spi_transaction_t trans{};
    trans.length = total * 8;
    trans.tx_buffer = tx;
    trans.rx_buffer = rx;
    const esp_err_t err = spi_device_transmit(device_handle(device_), &trans);
    if (err != ESP_OK)
    {
        set_error_locked("spi read failed");
        return false;
    }

    if (data && size > 0)
    {
        std::memcpy(data, rx + 1 + prefix_size + 1, size);
    }

    if (wait)
    {
        wait_ready_locked();
    }
    return true;
#endif
}

bool Sx126xRadio::write_register_locked(uint16_t addr, const uint8_t* data, size_t size)
{
    const uint8_t prefix[2] = {
        static_cast<uint8_t>((addr >> 8) & 0xFF),
        static_cast<uint8_t>(addr & 0xFF),
    };

    // Shared member SPI scratch (held under mutex_) rather than a stack buffer,
    // to keep the inline dead-chip revive within the app-loop stack.
    uint8_t* tx = spi_tx_scratch_;
    const size_t total = 1 + sizeof(prefix) + size;
    if (total > kSpiScratchSize)
    {
        set_error_locked("register write too large");
        return false;
    }
    std::memset(tx, 0, total);
    tx[0] = kCmdWriteRegister;
    std::memcpy(tx + 1, prefix, sizeof(prefix));
    if (data && size > 0)
    {
        std::memcpy(tx + 1 + sizeof(prefix), data, size);
    }

    wait_ready_locked();
    spi_transaction_t trans{};
    trans.length = total * 8;
    trans.tx_buffer = tx;
    const esp_err_t err = spi_device_transmit(device_handle(device_), &trans);
    if (err != ESP_OK)
    {
        set_error_locked("register write failed");
        return false;
    }
    wait_ready_locked();
    return true;
}

bool Sx126xRadio::read_register_locked(uint16_t addr, uint8_t* data, size_t size)
{
    const uint8_t prefix[2] = {
        static_cast<uint8_t>((addr >> 8) & 0xFF),
        static_cast<uint8_t>(addr & 0xFF),
    };
    return read_command_locked(kCmdReadRegister, prefix, sizeof(prefix), data, size, true);
}

bool Sx126xRadio::set_packet_type_locked(uint8_t packet_type)
{
    if (packet_type_ == packet_type)
    {
        return true;
    }
    if (!write_command_locked(kCmdSetPacketType, &packet_type, 1, true))
    {
        return false;
    }
    packet_type_ = packet_type;
    return true;
}

bool Sx126xRadio::set_rf_frequency_locked(float freq_mhz)
{
    // Image-reject calibration is band-specific and is NOT covered by the general
    // Calibrate(0x7F) run in init_locked(). It must be (re)done after a chip reset
    // for the active band, otherwise RX image rejection is wrong and the receiver
    // -- though it senses RF energy -- cannot correlate the LoRa preamble (GetStats
    // stays rx=0/crc_err=0/hdr_err=0). The dead-chip revive fully resets the chip
    // but keeps the cached freq, so the >=20MHz delta guard alone would SKIP image
    // cal on the revived chip and leave it deaf. force_image_cal_ (set by
    // reset_chip_locked) makes the first frequency set after a reset always
    // recalibrate the image, regardless of the delta.
    if (force_image_cal_ || std::fabs(freq_mhz_ - freq_mhz) >= 20.0f)
    {
        uint8_t cal[2] = {0xE1, 0xE9};
        if (freq_mhz < 779.0f)
        {
            cal[0] = 0xC1;
            cal[1] = 0xC5;
        }
        else if (freq_mhz < 902.0f)
        {
            cal[0] = 0xD7;
            cal[1] = 0xDB;
        }
        write_command_locked(kCmdCalibrateImage, cal, sizeof(cal), true);
        force_image_cal_ = false;
    }

    const uint32_t raw = rf_frequency_raw(freq_mhz);
    const uint8_t data[4] = {
        static_cast<uint8_t>((raw >> 24) & 0xFF),
        static_cast<uint8_t>((raw >> 16) & 0xFF),
        static_cast<uint8_t>((raw >> 8) & 0xFF),
        static_cast<uint8_t>(raw & 0xFF),
    };
    if (!write_command_locked(kCmdSetRfFrequency, data, sizeof(data), true))
    {
        return false;
    }
    freq_mhz_ = freq_mhz;
    return true;
}

bool Sx126xRadio::apply_standard_iq_workaround_locked()
{
    // SX1262 §15.4 standard-IQ workaround: bit 2 of REG_IQ (0x0736) SET. Done as a
    // read-modify-write to preserve the register's other bits. CONTRACT SUSPECT #1:
    // a read issued while BUSY is high can return garbage (0x00 / 0xFF), and ORing
    // 0x04 onto garbage then writing it back CLOBBERS the IQ config (e.g. 0xFF) ->
    // the demod mis-handles standard-IQ packets and produces bit errors. Guard the
    // base: if the read looks like the BUSY/dead signature, fall back to the known
    // reset base (0x0D on this SX1262, observed stable) before setting bit 2, so the
    // write can never poison the register.
    uint8_t iq_cfg = 0;
    if (!read_register_locked(kRegIqConfig, &iq_cfg, 1))
    {
        return false;
    }
    if (iq_cfg == 0x00 || iq_cfg == 0xFF)
    {
        iq_cfg = 0x0D; // SX1262 REG_IQ reset base; observed stable on this board
    }
    iq_cfg = static_cast<uint8_t>(iq_cfg | 0x04);
    return write_register_locked(kRegIqConfig, &iq_cfg, 1);
}

bool Sx126xRadio::fix_pa_clamping_locked()
{
    // SX1262 datasheet §15.2 (RadioLib fixPaClamping): set bits 4:1 of the TX-clamp
    // register so the PA does not over-clamp and distort the transmitted waveform.
    uint8_t clamp = 0;
    if (!read_register_locked(kRegTxClampConfig, &clamp, 1))
    {
        return false;
    }
    clamp = static_cast<uint8_t>(clamp | 0x1E);
    return write_register_locked(kRegTxClampConfig, &clamp, 1);
}

bool Sx126xRadio::set_tx_power_locked(int8_t tx_power)
{
    const int8_t clipped = std::max<int8_t>(-9, std::min<int8_t>(22, tx_power));
    // §15.2 PA-clamping fix first, exactly as RadioLib's SX1262::begin runs it
    // before setOutputPower. A distorted (over-clamped) TX waveform is a prime cause
    // of "receiver locks the preamble but every frame fails CRC" on this board.
    (void)fix_pa_clamping_locked();

    // Per-power optimized PA config (RadioLib paOptTable), NOT a hardcoded +22 dBm
    // config. Index = power + 9. This sets the PA operating point that actually
    // matches the requested output power so the modulated signal is clean.
    const PaOptEntry& pa = kPaOptTable[static_cast<size_t>(clipped + 9)];

    uint8_t ocp = 0;
    read_register_locked(kRegOcpConfiguration, &ocp, 1);
    const uint8_t pa_config[4] = {pa.pa_duty_cycle, pa.hp_max, kPaConfigDeviceSelSx1262,
                                  kPaConfigPaLut};
    if (!write_command_locked(kCmdSetPaConfig, pa_config, sizeof(pa_config), true))
    {
        return false;
    }
    // SetTxParams power is the table's paVal (the calibrated register value for the
    // target dBm), not the raw requested dBm -- matching RadioLib's setOutputPower.
    const uint8_t tx_params[2] = {static_cast<uint8_t>(pa.pa_val), kPaRamp200u};
    const bool ok = write_command_locked(kCmdSetTxParams, tx_params, sizeof(tx_params), true);
    write_register_locked(kRegOcpConfiguration, &ocp, 1);
    if (rx_diag_count_ < 2)
    {
        printf("idf-mc: pacfg pwr=%d paval=%d dutycycle=%u hpmax=%u\n",
               static_cast<int>(clipped), static_cast<int>(pa.pa_val),
               static_cast<unsigned>(pa.pa_duty_cycle), static_cast<unsigned>(pa.hp_max));
    }
    return ok;
}

bool Sx126xRadio::set_dio_irq_params_locked(uint16_t irq_mask, uint16_t dio1_mask)
{
    const uint8_t data[8] = {
        static_cast<uint8_t>((irq_mask >> 8) & 0xFF),
        static_cast<uint8_t>(irq_mask & 0xFF),
        static_cast<uint8_t>((dio1_mask >> 8) & 0xFF),
        static_cast<uint8_t>(dio1_mask & 0xFF),
        0x00,
        0x00,
        0x00,
        0x00,
    };
    return write_command_locked(kCmdSetDioIrqParams, data, sizeof(data), true);
}

bool Sx126xRadio::set_dio3_as_tcxo_ctrl_locked(uint8_t voltage_code, uint32_t startup_time_us)
{
    const uint32_t timeout = microseconds_to_rtc_step(startup_time_us);
    const uint8_t data[4] = {
        voltage_code,
        static_cast<uint8_t>((timeout >> 16) & 0xFF),
        static_cast<uint8_t>((timeout >> 8) & 0xFF),
        static_cast<uint8_t>(timeout & 0xFF),
    };
    return write_command_locked(kCmdSetDio3AsTcxoCtrl, data, sizeof(data), true);
}

bool Sx126xRadio::set_dio2_as_rf_switch_locked(bool enable)
{
    const uint8_t data = enable ? 0x01 : 0x00;
    return write_command_locked(kCmdSetDio2AsRfSwitchCtrl, &data, 1, true);
}

bool Sx126xRadio::set_rx_boosted_gain_locked(bool enable)
{
    const uint8_t value = enable ? kRxGainBoosted : kRxGainPowerSaving;
    if (!write_register_locked(kRegRxGain, &value, 1))
    {
        return false;
    }
    // Persist the gain across the SX1262's internal RX warm-restarts by adding
    // the RX-gain register to the retention list (datasheet section 9.6;
    // RadioLib does this with persist=true). Otherwise the chip silently
    // reverts to power-saving gain on the first internal restart and loses
    // sensitivity.
    const uint8_t retention[3] = {
        0x01,
        static_cast<uint8_t>((kRegRxGain >> 8) & 0xFF),
        static_cast<uint8_t>(kRegRxGain & 0xFF),
    };
    return write_register_locked(kRegRxGainRetention0, retention, sizeof(retention));
}

uint8_t Sx126xRadio::read_chip_status_locked()
{
    uint8_t status = 0;
    if (!read_command_locked(kCmdGetStatus, nullptr, 0, &status, 1, true))
    {
        return 0;
    }
    return status;
}

bool Sx126xRadio::clear_irq_locked(uint16_t flags)
{
    const uint8_t data[2] = {
        static_cast<uint8_t>((flags >> 8) & 0xFF),
        static_cast<uint8_t>(flags & 0xFF),
    };
    return write_command_locked(kCmdClearIrqStatus, data, sizeof(data), true);
}

bool Sx126xRadio::set_buffer_base_locked(uint8_t tx_base, uint8_t rx_base)
{
    const uint8_t data[2] = {tx_base, rx_base};
    return write_command_locked(kCmdSetBufferBaseAddress, data, sizeof(data), true);
}

bool Sx126xRadio::set_rx_locked(uint32_t timeout_raw)
{
    const uint8_t data[3] = {
        static_cast<uint8_t>((timeout_raw >> 16) & 0xFF),
        static_cast<uint8_t>((timeout_raw >> 8) & 0xFF),
        static_cast<uint8_t>(timeout_raw & 0xFF),
    };
    return write_command_locked(kCmdSetRx, data, sizeof(data), true);
}

bool Sx126xRadio::set_tx_locked(uint32_t timeout_raw)
{
    const uint8_t data[3] = {
        static_cast<uint8_t>((timeout_raw >> 16) & 0xFF),
        static_cast<uint8_t>((timeout_raw >> 8) & 0xFF),
        static_cast<uint8_t>(timeout_raw & 0xFF),
    };
    return write_command_locked(kCmdSetTx, data, sizeof(data), true);
}

bool Sx126xRadio::drain_rx_fifo_locked()
{
    // TX and RX share one 256-byte SX1262 data buffer; the TX path writes the
    // outbound frame at base 0x00 and the RX is armed at base 0x00 too, so after a
    // transmit the FIFO still holds this unit's own TX payload. A spurious post-TX
    // RxDone (rxoff=0) then reads that stale TX frame back -- the proven
    // 'rxbytes == own txbytes' self-reception artifact. Zero the buffer at the RX
    // base before each arm so that, even if a phantom RxDone slips through, the bytes
    // it returns are zeros (rxbytes can never equal txbytes) and the all-zero frame
    // fails the parser instead of polluting it. Drain the leading 128 bytes (covers
    // the ~103-byte advert and every realistic short control frame); a WriteBuffer is
    // a single SPI burst issued only on (re)arm, never in the hot poll.
    uint8_t* tx = spi_tx_scratch_;
    constexpr size_t kDrainBytes = 128;
    const size_t total = 2 + kDrainBytes;
    if (total > kSpiScratchSize)
    {
        return false;
    }
    std::memset(tx, 0, total);
    tx[0] = kCmdWriteBuffer;
    tx[1] = 0x00; // RX base offset (set_buffer_base_locked uses rx_base 0x00)
    wait_ready_locked();
    spi_transaction_t trans{};
    trans.length = total * 8;
    trans.tx_buffer = tx;
    const esp_err_t err = spi_device_transmit(device_handle(device_), &trans);
    if (err != ESP_OK)
    {
        set_error_locked("rx fifo drain failed");
        return false;
    }
    wait_ready_locked();
    return true;
}

bool Sx126xRadio::suppress_post_tx_self_reception_locked()
{
    // Called immediately after SetRx (continuous RX) is issued. On this board the
    // SX1262 can complete ONE bogus reception on the residual of the just-finished
    // transmit, latching RxDone+CrcErr that reads the stale own-TX FIFO. Let that
    // residual self-trigger settle (a few LoRa symbols: SF7/BW62.5 symbol ~2.05ms),
    // then inspect the IRQ. If a terminal RX bit latched and the buffer it points at
    // is byte-identical to the just-transmitted frame, it is the self-reception
    // phantom -- clear its terminal bits so the RX poll/ladder never observes or
    // counts it (the phantom's RxDone+CrcErr otherwise pins crcok = rxdone - crcerr
    // at zero forever). Clearing IRQ does NOT drop continuous RX, so genuine later
    // peer frames still re-latch a clean RxDone and are counted. A genuine frame is
    // never lost here: right after our own jittered transmit the decorrelated peer is
    // not mid-flight, and we only clear when the content equals our own TX.
    if (last_tx_frame_len_ == 0 || !post_tx_pending_)
    {
        return true; // not a post-TX arm: no residual self-reception possible
    }
    // Consume the post-TX flag: the phantom can only occur on this first arm after a
    // transmit, so subsequent continuous-RX re-arms skip the settle+check entirely.
    post_tx_pending_ = false;

    // Settle ~6 ms (~3 symbols at SF7/BW62.5) for the residual false-trigger to
    // surface, so a single deterministic check catches it.
    vTaskDelay(pdMS_TO_TICKS(6));

    uint8_t irqb[2] = {0};
    if (!read_command_locked(kCmdGetIrqStatus, nullptr, 0, irqb, sizeof(irqb), true))
    {
        return false;
    }
    const uint16_t irq = (static_cast<uint16_t>(irqb[0]) << 8) | irqb[1];
    if (irq == 0x0000u || irq == 0xFFFFu)
    {
        return true; // idle (no phantom) or dead chip (handled elsewhere)
    }
    if ((irq & kIrqRxDone) == 0)
    {
        return true; // no completed reception latched -> nothing to suppress
    }

    // A reception latched. Read its length/offset and content and compare to the
    // cached TX frame.
    uint8_t bufstat[2] = {0};
    if (!read_command_locked(kCmdGetRxBufferStatus, nullptr, 0, bufstat, sizeof(bufstat), true))
    {
        return false;
    }
    const uint8_t rx_len = bufstat[0];
    const uint8_t rx_off = bufstat[1];

    // PHANTOM-DETECTION (root cause of crcok=0). The post-TX spurious reception is the
    // SX1262 completing a bogus RxDone on the residual of its just-finished transmit
    // right after SetRx re-arms. The hardware evidence (idf-mc: rxdec) is unambiguous:
    // it fires once per OWN advert (never an extra reception while merely listening to
    // the peer), with a FIXED garbage header length per unit (A=190, B=123; a function
    // of that unit's own modulation residual, hence constant) and -- decisively -- it
    // writes NO payload: the FIFO at the reported offset is the all-zeros we drained
    // before arming. A GENUINE reception always writes payload bytes, so a post-TX
    // RxDone whose payload is still all-zeros decoded no frame and is, by definition,
    // the phantom. (The previous content-match against last_tx_frame_ could never fire
    // because drain_rx_fifo_locked() had already zeroed the FIFO, so the buffer read
    // back zeros instead of the cached TX bytes -- the drain defeated the match and the
    // phantom slipped through as RxDone+CrcErr, pinning crcok = rxdone - crcerr at 0.)
    //
    // This is content-independent of last_tx_frame_ and so survives the drain. It is
    // safe -- it never eats a real peer frame -- because (a) it only runs on the ONE arm
    // after a transmit (post_tx_pending_), (b) only ~6 ms after that arm, far less than
    // the ~349 ms a real advert needs on air, and (c) the peer is jitter-decorrelated,
    // so it cannot have a fully-received frame sitting in our FIFO 6 ms after our own
    // TxDone; and any real frame would have written non-zero payload anyway.
    bool is_self = false;
    bool payload_all_zero = true;
    {
        // Read the leading payload bytes from the reported offset. A real frame writes
        // payload here; the phantom leaves the drained zeros.
        const size_t scan_len = std::min<size_t>(rx_len == 0 ? 16u : rx_len, 64u);
        uint8_t buf[64] = {0};
        if (read_command_locked(kCmdReadBuffer, &rx_off, 1, buf, scan_len, true))
        {
            for (size_t i = 0; i < scan_len; ++i)
            {
                if (buf[i] != 0x00)
                {
                    payload_all_zero = false;
                    break;
                }
            }
            // Belt-and-suspenders: also catch the classic content==own-TX signature in
            // case a future change arms without draining first.
            if (last_tx_frame_len_ > 0)
            {
                const size_t cmp_len = std::min<size_t>(last_tx_frame_len_, scan_len);
                if (std::memcmp(buf, last_tx_frame_, cmp_len) == 0)
                {
                    is_self = true;
                }
            }
        }
    }
    // The phantom decoded no payload (drained-zero FIFO) -> not a real frame. Treat any
    // post-TX RxDone with an all-zero payload as the self-reception phantom.
    if (payload_all_zero)
    {
        is_self = true;
    }

    if (is_self)
    {
        // Wipe the phantom's terminal bits (RxDone/CrcErr/HeaderErr/Timeout). RX stays
        // armed. Drain the FIFO again so a re-fired phantom reads zeros, not the TX.
        clear_irq_locked(kIrqRxDone | kIrqCrcErr | kIrqHeaderErr | kIrqTimeout);
        (void)drain_rx_fifo_locked();
        ++self_rx_suppressed_;
        if (self_rx_suppressed_ <= 4)
        {
            printf("idf-mc: rxself suppressed=%lu rxlen=%u rxoff=%u zero=%d (post-tx phantom)\n",
                   static_cast<unsigned long>(self_rx_suppressed_),
                   static_cast<unsigned>(rx_len), static_cast<unsigned>(rx_off),
                   payload_all_zero ? 1 : 0);
        }
    }
    return true;
}

bool Sx126xRadio::configure_lora_locked(float freq_mhz,
                                        float bw_khz,
                                        uint8_t sf,
                                        uint8_t cr,
                                        int8_t tx_power,
                                        uint16_t preamble_len,
                                        uint8_t sync_word,
                                        uint8_t crc_len)
{
    if (!set_packet_type_locked(kPacketTypeLoRa))
    {
        return false;
    }
    const uint8_t standby_mode = kStandbyRc;
    if (!write_command_locked(kCmdSetStandby, &standby_mode, 1, true))
    {
        return false;
    }

    // ROOT-CAUSE ORDERING FIX (RX demodulation). The proven on-this-board drivers
    // (LilyGo cpp_bus_driver ConfigLoraParams + RadioLib begin) BOTH program the
    // LoRa modulation parameters BEFORE the RF frequency / image calibration, and
    // RadioLib then applies the §15.1 RX-sensitivity register workaround that
    // depends on the active modem+bandwidth. The previous order here was inverted
    // (frequency + CalibrateImage first, modulation second). With image
    // calibration run before SetModulationParams the receiver's band/optimisation
    // state did not match the SF7/BW62.5 modem, so the demodulator sensed RF
    // energy (RSSI bumped) yet never correlated the preamble (GetStats stayed
    // rx=0/crc_err=0/hdr_err=0, no PreambleDetected/RxDone). Program modulation
    // first, then frequency/image-cal, then the sensitivity fix -- the exact order
    // the two reference receivers use on this hardware.
    const uint8_t mod[4] = {sf, map_lora_bw(bw_khz), map_lora_cr(cr), calc_ldro(sf, bw_khz)};
    if (rx_diag_count_ < 2)
    {
        printf("idf-mc: loramod sf=%u bwcode=0x%02X crcode=0x%02X ldro=%u (bw=%.1f cr=%u)\n",
               static_cast<unsigned>(mod[0]), static_cast<unsigned>(mod[1]),
               static_cast<unsigned>(mod[2]), static_cast<unsigned>(mod[3]),
               static_cast<double>(bw_khz), static_cast<unsigned>(cr));
    }
    if (!write_command_locked(kCmdSetModulationParams, mod, sizeof(mod), true))
    {
        return false;
    }
    std::memcpy(last_mod_bytes_, mod, sizeof(mod));
    have_last_mod_ = true;

    const uint8_t packet[6] = {
        static_cast<uint8_t>((preamble_len >> 8) & 0xFF),
        static_cast<uint8_t>(preamble_len & 0xFF),
        kLoRaHeaderExplicit,
        0xFF,
        crc_len ? kLoRaCrcOn : kLoRaCrcOff,
        kLoRaIqStandard,
    };
    if (!write_command_locked(kCmdSetPacketParams, packet, sizeof(packet), true))
    {
        return false;
    }
    std::memcpy(last_pkt_bytes_, packet, sizeof(packet));
    have_last_pkt_ = true;

    // SX1262 datasheet §15.4 "Optimizing the Inverted IQ Operation" workaround.
    // RadioLib applies this (fixInvertedIQ) on every SetPacketParams: for STANDARD
    // IQ (our case, packet[5]=kLoRaIqStandard), bit 2 of REG_IQ (0x0736) must be
    // SET, otherwise the demodulator mis-handles standard-IQ packets -- the receiver
    // senses RF (RSSI bumps) but never correlates the preamble, and the rare frame
    // that gets through decodes a corrupted length. Our driver never touched 0x0736,
    // leaving it at the inverted-IQ default. Read-modify-write bit 2 to match the
    // proven RadioLib receive path that demodulates on this exact board.
    if (!apply_standard_iq_workaround_locked())
    {
        return false;
    }

    const uint8_t sync[2] = {
        static_cast<uint8_t>((sync_word & 0xF0) | 0x04),
        static_cast<uint8_t>(((sync_word & 0x0F) << 4) | 0x04),
    };
    if (!write_register_locked(kRegLoraSyncWordMsb, sync, sizeof(sync)))
    {
        return false;
    }

    if (!set_rf_frequency_locked(freq_mhz) || !set_tx_power_locked(tx_power))
    {
        return false;
    }

    // SX1262 datasheet §15.1 "Modulation Quality" RX-sensitivity workaround. For
    // every LoRa bandwidth other than 500 kHz, bit 2 of REG_SENSITIVITY_CONFIG
    // (0x0889) must be SET; RadioLib applies this on every config (fixSensitivity).
    // The previous firmware never touched 0x0889, leaving the receive path at the
    // wrong sensitivity for the 62.5 kHz modem.
    if (!fix_rx_sensitivity_locked(bw_khz))
    {
        return false;
    }

    // Undocumented SX1262 RX-improvement patch the proven Meshtastic receiver on this
    // exact board applies (set bit 0 of 0x08B5). Read-modify-write so we don't clobber
    // the rest of the register. Our prior register diff missed this entirely.
    {
        uint8_t rximp = 0;
        if (read_register_locked(kRegRxImprovement, &rximp, 1))
        {
            const uint8_t patched = static_cast<uint8_t>(rximp | 0x01);
            (void)write_register_locked(kRegRxImprovement, &patched, 1);
            if (rx_diag_count_ < 2)
            {
                printf("idf-mc: rximp 0x08B5 base=0x%02X -> 0x%02X\n",
                       static_cast<unsigned>(rximp), static_cast<unsigned>(patched));
            }
        }
    }

    return true;
}

bool Sx126xRadio::fix_rx_sensitivity_locked(float bw_khz)
{
    uint8_t sensitivity = 0;
    if (!read_register_locked(kRegSensitivityConfig, &sensitivity, 1))
    {
        return false;
    }
    if (std::fabs(bw_khz - 500.0f) <= 0.001f)
    {
        sensitivity &= 0xFB;
    }
    else
    {
        sensitivity |= 0x04;
    }
    return write_register_locked(kRegSensitivityConfig, &sensitivity, 1);
}

bool Sx126xRadio::configure_fsk_locked(float freq_mhz,
                                       int8_t tx_power,
                                       float bit_rate_kbps,
                                       float freq_dev_khz,
                                       float rx_bw_khz,
                                       uint16_t preamble_len,
                                       const uint8_t* sync_word,
                                       size_t sync_word_len,
                                       uint8_t crc_len)
{
    if (!set_packet_type_locked(kPacketTypeGfsk))
    {
        return false;
    }
    const uint8_t standby_mode = kStandbyRc;
    if (!write_command_locked(kCmdSetStandby, &standby_mode, 1, true))
    {
        return false;
    }
    if (!set_rf_frequency_locked(freq_mhz) || !set_tx_power_locked(tx_power))
    {
        return false;
    }

    const uint32_t br_raw = fsk_bitrate_raw(bit_rate_kbps);
    const uint32_t fd_raw = fsk_freq_dev_raw(freq_dev_khz);
    const uint8_t mod[8] = {
        static_cast<uint8_t>((br_raw >> 16) & 0xFF),
        static_cast<uint8_t>((br_raw >> 8) & 0xFF),
        static_cast<uint8_t>(br_raw & 0xFF),
        kFskFilterNone,
        map_fsk_rx_bw(rx_bw_khz),
        static_cast<uint8_t>((fd_raw >> 16) & 0xFF),
        static_cast<uint8_t>((fd_raw >> 8) & 0xFF),
        static_cast<uint8_t>(fd_raw & 0xFF),
    };
    if (!write_command_locked(kCmdSetModulationParams, mod, sizeof(mod), true))
    {
        return false;
    }

    const uint8_t sync_bits = static_cast<uint8_t>(sync_word_len * 8U);
    const uint8_t packet[9] = {
        static_cast<uint8_t>((preamble_len >> 8) & 0xFF),
        static_cast<uint8_t>(preamble_len & 0xFF),
        map_preamble_detect(preamble_len, sync_bits),
        sync_bits,
        kFskAddressFilterOff,
        kFskPacketVariable,
        0xFF,
        crc_len ? kFskCrc2ByteInv : kFskCrcOff,
        kFskWhiteningOff,
    };
    if (!write_command_locked(kCmdSetPacketParams, packet, sizeof(packet), true))
    {
        return false;
    }

    if (sync_word && sync_word_len > 0)
    {
        if (!write_register_locked(kRegSyncWord0, sync_word, sync_word_len))
        {
            return false;
        }
    }

    if (crc_len)
    {
        const uint8_t init[2] = {0x1D, 0x0F};
        const uint8_t poly[2] = {0x10, 0x21};
        write_register_locked(kRegCrcInitialMsb, init, sizeof(init));
        write_register_locked(kRegCrcPolynomialMsb, poly, sizeof(poly));
    }

    return true;
}

bool Sx126xRadio::configureLoRaReceive(float freq_mhz,
                                       float bw_khz,
                                       uint8_t sf,
                                       uint8_t cr,
                                       int8_t tx_power,
                                       uint16_t preamble_len,
                                       uint8_t sync_word,
                                       uint8_t crc_len)
{
    if (!take_mutex(mutex_))
    {
        return false;
    }
    const bool ok = init_locked() &&
                    configure_lora_locked(freq_mhz, bw_khz, sf, cr, tx_power, preamble_len, sync_word, crc_len) &&
                    set_dio_irq_params_locked(kIrqRxLadderMask, kIrqRxLadderMask) &&
                    clear_irq_locked(kIrqAll) &&
                    set_buffer_base_locked(0x00, 0x00) &&
                    set_rx_locked(kRxTimeoutInf);
    if (ok)
    {
        // Remember the exact LoRa parameters so the TX path can rebuild the radio
        // configuration if the chip has dropped its state while parked in RX.
        freq_mhz_ = freq_mhz;
        lora_bw_khz_ = bw_khz;
        lora_sf_ = sf;
        lora_cr_ = cr;
        lora_tx_power_ = tx_power;
        lora_preamble_ = preamble_len;
        lora_sync_word_ = sync_word;
        lora_crc_len_ = crc_len;
        lora_cfg_valid_ = true;
    }
    give_mutex(mutex_);
    return ok;
}

bool Sx126xRadio::configureFsk(float freq_mhz,
                               int8_t tx_power,
                               float bit_rate_kbps,
                               float freq_dev_khz,
                               float rx_bw_khz,
                               uint16_t preamble_len,
                               const uint8_t* sync_word,
                               size_t sync_word_len,
                               uint8_t crc_len)
{
    if (!take_mutex(mutex_))
    {
        return false;
    }
    const bool ok = init_locked() &&
                    configure_fsk_locked(freq_mhz,
                                         tx_power,
                                         bit_rate_kbps,
                                         freq_dev_khz,
                                         rx_bw_khz,
                                         preamble_len,
                                         sync_word,
                                         sync_word_len,
                                         crc_len);
    give_mutex(mutex_);
    return ok;
}

bool Sx126xRadio::set_rx_packet_params_locked()
{
    // Re-issue the LoRa RX packet parameters exactly as configure_lora_locked()
    // programs them for receive: the cached preamble length (fallback 16, the
    // value the adapter configures and the TX path sends), explicit header,
    // max length 0xFF, CRC on, standard IQ. RadioLib's startReceiveCommon ALWAYS
    // re-issues SetPacketParams immediately before SetRx on every arm, and our
    // previous re-arm path skipped it -- so after the first transmit (which
    // reprograms SetPacketParams with the TX payload length) the receiver was
    // left armed with a stale/TX-shaped packet config. The demodulator then
    // sensed RF but never correlated the preamble.
    // Re-assert the LoRa MODULATION params (SF/BW/CR/LDRO) from the cached config
    // immediately before arming RX. The TX path already re-asserts SetModulationParams
    // before every SetTx, but this RX re-arm path previously did NOT -- an asymmetry:
    // if the chip's modem ever lost or never latched the configured SF/BW/CR/LDRO (a
    // SetModulationParams write dropped under BUSY, or perturbed by a TX/standby/revive
    // cycle), TX would self-correct but RX would arm on a wrong modem. The peer's
    // preamble can still trip RxDone while the data symbols (the explicit header
    // first) demodulate to garbage -> CR reads 0, wrong length, CRC fail -- exactly
    // the observed 'rxdec rawcr=0x00 rxlen=190' signature. Re-issuing it here makes
    // the RX demod provably identical to the TX modem on every arm.
    if (lora_cfg_valid_)
    {
        const uint8_t mod[4] = {lora_sf_, map_lora_bw(lora_bw_khz_), map_lora_cr(lora_cr_),
                                calc_ldro(lora_sf_, lora_bw_khz_)};
        if (!write_command_locked(kCmdSetModulationParams, mod, sizeof(mod), true))
        {
            return false;
        }
        std::memcpy(last_mod_bytes_, mod, sizeof(mod));
        have_last_mod_ = true;
    }

    // SX1262 datasheet §15.4 standard-IQ workaround, applied BEFORE SetPacketParams
    // exactly as RadioLib's setPacketParams -> fixInvertedIQ does. Re-asserting it on
    // every arm guarantees the demod is in the correct IQ state when SetRx follows,
    // even if a prior TX/standby cycle perturbed the register. Guarded read.
    if (!apply_standard_iq_workaround_locked())
    {
        return false;
    }

    const uint16_t preamble = (lora_cfg_valid_ && lora_preamble_ > 0) ? lora_preamble_ : 16;
    const uint8_t packet[6] = {
        static_cast<uint8_t>((preamble >> 8) & 0xFF),
        static_cast<uint8_t>(preamble & 0xFF),
        kLoRaHeaderExplicit,
        0xFF,
        kLoRaCrcOn,
        kLoRaIqStandard,
    };
    const bool ok = write_command_locked(kCmdSetPacketParams, packet, sizeof(packet), true);
    if (ok)
    {
        std::memcpy(last_pkt_bytes_, packet, sizeof(packet));
        have_last_pkt_ = true;
    }
    return ok;
}

bool Sx126xRadio::start_receive_locked()
{
    board_prepare_lora_direction(false);
    // Boosted-gain RX (reg 0x08AC = 0x96) maximizes sensitivity, matching the
    // reference driver before it parks the chip in continuous RX.
    set_rx_boosted_gain_locked(true);

    // ROOT-CAUSE ARM ORDER (RX demodulation). Mirror RadioLib's startReceiveCommon
    // EXACTLY on every arm: standby(STDBY_RC) -> SetDioIrqParams -> SetBufferBase ->
    // ClearIrqStatus -> SetPacketParams(+IQ fix) -> SetRx. Our previous re-arm path
    // jumped straight to SetRx without first returning to standby and re-issuing
    // SetPacketParams. After the first transmit reprograms SetPacketParams with the
    // outbound payload length, the receiver was re-armed on top of that stale
    // (TX-shaped) packet configuration while still in RX state, so the demodulator
    // sensed the peer's RF energy (RSSI bumped) but never correlated the preamble
    // and PreambleDetected/RxDone never fired. Returning to standby and re-issuing
    // the RX packet params before SetRx is the proven-on-this-board sequence.
    //
    // Arm INFINITE continuous RX -- the exact mode under which a genuine foreign
    // packet was previously received and decoded on this board -- and only re-arm
    // AFTER a terminal IRQ (never mid-reception), with the dead-chip revive as the
    // safety net. Pass the widened kIrqRxLadderMask as BOTH the IRQ mask and the
    // DIO1 mask: on this SX1262 the PreambleDetected/HeaderValid bits did not latch
    // in GetIrqStatus when they were only in the IRQ mask (param 1) -- only the
    // bits also routed to a DIO line surfaced -- so routing the ladder bits to DIO1
    // as well makes them latch for the rxladder probe. DIO1 is not wired to the
    // P4 (irq=-1) so this only affects which bits the chip exposes, not any pin.
    const uint8_t standby_mode = kStandbyRc;
    const bool armed = write_command_locked(kCmdSetStandby, &standby_mode, 1, true) &&
                       set_dio_irq_params_locked(kIrqRxLadderMask, kIrqRxLadderMask) &&
                       set_buffer_base_locked(0x00, 0x00) &&
                       // POST-TX SELF-RECEPTION FIX (candidate (d)): zero the shared
                       // FIFO at the RX base BEFORE arming, so the residual own-TX
                       // payload is gone -- a spurious post-TX RxDone can no longer
                       // read 'rxbytes == own txbytes'.
                       drain_rx_fifo_locked() &&
                       clear_irq_locked(kIrqAll) &&
                       set_rx_packet_params_locked() &&
                       set_rx_locked(kRxTimeoutInf);
    if (!armed)
    {
        return false;
    }
    // POST-TX SELF-RECEPTION FIX (candidate (c)): now that RX is armed, catch and
    // discard the one spurious RxDone the SX1262 fires on the just-transmitted
    // residual (its buffer is byte-identical to our own TX), so it never reaches the
    // RX poll/ladder and never pins crcok at zero. Genuine peer frames are untouched.
    (void)suppress_post_tx_self_reception_locked();
    return true;
}

bool Sx126xRadio::startReceive()
{
    if (!take_mutex(mutex_))
    {
        return false;
    }
    const bool ok = start_receive_locked();

    if (ok && rx_diag_count_ < 2)
    {
        // One-shot, non-blocking confirmation that the chip actually entered RX
        // (chipMode 0x5) and that the receive statistics are advancing. Kept
        // lightweight so it never disturbs an in-flight reception.
        const uint8_t status = read_chip_status_locked();
        const unsigned mode = (status >> 4) & 0x07;
        uint8_t rssi_raw = 0;
        (void)read_command_locked(kCmdGetRssiInst, nullptr, 0, &rssi_raw, 1, true);
        uint8_t stats[6] = {0};
        (void)read_command_locked(kCmdGetStats, nullptr, 0, stats, sizeof(stats), true);
        const uint16_t pkt_rx = (static_cast<uint16_t>(stats[0]) << 8) | stats[1];
        const uint16_t crc_err = (static_cast<uint16_t>(stats[2]) << 8) | stats[3];
        const uint16_t hdr_err = (static_cast<uint16_t>(stats[4]) << 8) | stats[5];
        // STEP B(2): read the demod-critical config back over SPI to confirm the
        // writes actually landed (a write dropped under BUSY would leave the modem
        // misconfigured while the values still look right in code). sync MSB/LSB
        // must read 0x14/0x24 for sync 0x12; IQ (0x0736) bit 2 must be SET for
        // standard IQ; sensitivity (0x0889) bit 2 must be SET for BW != 500k.
        uint8_t sync_rb[2] = {0};
        (void)read_register_locked(kRegLoraSyncWordMsb, sync_rb, sizeof(sync_rb));
        uint8_t iq_rb = 0;
        (void)read_register_locked(kRegIqConfig, &iq_rb, 1);
        uint8_t sens_rb = 0;
        (void)read_register_locked(kRegSensitivityConfig, &sens_rb, 1);
        uint8_t gain_rb = 0;
        (void)read_register_locked(kRegRxGain, &gain_rb, 1);
        ESP_LOGI(kTag,
                 "SX1262 RX diag[%lu]: status=0x%02X mode=%u(0x5=RX) rssi=%.1fdBm stats(rx=%u crc_err=%u hdr_err=%u) sync=%02X%02X iq=0x%02X sens=0x%02X gain=0x%02X preamble=%u",
                 static_cast<unsigned long>(rx_diag_count_),
                 status,
                 mode,
                 static_cast<double>(rssi_raw) / -2.0,
                 static_cast<unsigned>(pkt_rx),
                 static_cast<unsigned>(crc_err),
                 static_cast<unsigned>(hdr_err),
                 sync_rb[0], sync_rb[1],
                 static_cast<unsigned>(iq_rb),
                 static_cast<unsigned>(sens_rb),
                 static_cast<unsigned>(gain_rb),
                 static_cast<unsigned>((lora_cfg_valid_ && lora_preamble_ > 0) ? lora_preamble_ : 16));
        // Mirror the same readback to stdout so it shows in the check capture
        // (which greps the USB-serial console, not the ESP_LOG UART).
        printf("idf-mc: rxcfg sync=%02X%02X iq=0x%02X sens=0x%02X gain=0x%02X mode=%u rssi=%.0f\n",
               sync_rb[0], sync_rb[1], static_cast<unsigned>(iq_rb),
               static_cast<unsigned>(sens_rb), static_cast<unsigned>(gain_rb),
               mode, static_cast<double>(rssi_raw) / -2.0);
        // MANDATORY DIAGNOSTIC: the demod-critical on-air state, read back/echoed
        // right after the RX is armed, so it can be compared (a) to the intended
        // values, (b) against the TX-path 'rdbk' line (they MUST match on-air), and
        // (c) against RadioLib. SetModulationParams/SetPacketParams have no readback
        // command, so the EXACT bytes last written are echoed; the IQ/sensitivity/
        // sync REGISTERS are read back over SPI to prove the writes actually landed
        // (a write dropped/garbled under BUSY would show here).
        printf("idf-mc: rdbk side=RX mod=%02X%02X%02X%02X pkt=%02X%02X%02X%02X%02X%02X "
               "iq=0x%02X sens=0x%02X sync=%02X%02X gain=0x%02X\n",
               static_cast<unsigned>(last_mod_bytes_[0]), static_cast<unsigned>(last_mod_bytes_[1]),
               static_cast<unsigned>(last_mod_bytes_[2]), static_cast<unsigned>(last_mod_bytes_[3]),
               static_cast<unsigned>(last_pkt_bytes_[0]), static_cast<unsigned>(last_pkt_bytes_[1]),
               static_cast<unsigned>(last_pkt_bytes_[2]), static_cast<unsigned>(last_pkt_bytes_[3]),
               static_cast<unsigned>(last_pkt_bytes_[4]), static_cast<unsigned>(last_pkt_bytes_[5]),
               static_cast<unsigned>(iq_rb), static_cast<unsigned>(sens_rb),
               sync_rb[0], sync_rb[1], static_cast<unsigned>(gain_rb));
        ++rx_diag_count_;
    }
    give_mutex(mutex_);
    return ok;
}

bool Sx126xRadio::isChipResponsive()
{
    if (!take_mutex(mutex_))
    {
        return false;
    }
    const bool alive = chip_responsive_locked();
    give_mutex(mutex_);
    return alive;
}

bool Sx126xRadio::reviveReceive()
{
    if (!take_mutex(mutex_))
    {
        return false;
    }
    // Heavy recovery used by the RX pump when the chip is detected dead (version
    // register reads neither a valid ASCII nor 0xFF): fully reset + reconfigure
    // the LoRa stack (the exact sequence the TX path uses), then re-arm RX. This
    // is the same revive that brings the chip back before a transmit, applied
    // here so a chip that died in an RX gap (no transmit to trigger it) is
    // resurrected and put back to listening. Runs INLINE on the app-loop task;
    // the boot-stability stack bump (16384) and off-stack SPI scratch keep it
    // within budget. Throttled by the caller so it runs at most once a few sec.
    bool ok = reestablish_lora_locked();
    if (ok)
    {
        ok = start_receive_locked();
    }
    if (ok)
    {
        ESP_LOGW(kTag, "SX1262 was dead in RX; reset + reconfigured + re-armed receive");
    }
    give_mutex(mutex_);
    return ok;
}

void Sx126xRadio::standby()
{
    if (!take_mutex(mutex_))
    {
        return;
    }
    const uint8_t mode = kStandbyRc;
    write_command_locked(kCmdSetStandby, &mode, 1, true);
    give_mutex(mutex_);
}

float Sx126xRadio::readRssi()
{
    if (!take_mutex(mutex_))
    {
        return NAN;
    }
    uint8_t raw = 0;
    const bool ok = read_command_locked(kCmdGetRssiInst, nullptr, 0, &raw, 1, true);
    give_mutex(mutex_);
    return ok ? (static_cast<float>(raw) / -2.0f) : NAN;
}

bool Sx126xRadio::chip_responsive_locked()
{
    // The SX1262 version string register reads "SX1261"/"SX1262" when the chip is
    // alive and configured. After the T-Display-P4 radio drops its state in
    // sustained RX it reads all-0x00 (and all SPI status reads return 0x00 too).
    // Use the version register as the liveness probe (same data the boot probe
    // validates), so we only pay a reset+reconfigure when the chip is truly dead.
    uint8_t version[6] = {0};
    if (!read_register_locked(kRegVersionString, version, sizeof(version)))
    {
        return false;
    }
    for (uint8_t value : version)
    {
        if (value != 0x00 && value != 0xFF)
        {
            return true;
        }
    }
    return false;
}

bool Sx126xRadio::reset_chip_locked()
{
#if defined(TRAIL_MATE_ESP_BOARD_TAB5) || defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    // The NRESET-via-expander pulse alone does NOT revive the T-Display-P4 SX1262
    // once it has gone dark in RX (verified: the expander write succeeds but the
    // version register still reads 0x00). Fully tear the SPI device + bus down and
    // re-run init_locked(), i.e. the exact power-on sequence that brought the chip
    // up cleanly at boot, so a re-established chip is byte-for-byte like a freshly
    // booted one. The SPI bus is owned solely by this radio, so freeing it here is
    // safe.
    const auto host = static_cast<spi_host_device_t>(lora_pins().spi.host);
    if (device_)
    {
        (void)spi_bus_remove_device(device_handle(device_));
        device_ = nullptr;
    }
    (void)spi_bus_free(host);
    initialized_ = false;
    online_ = false;
    // The chip is about to be re-initialized from scratch; its band image
    // calibration is back at default. Force the next set_rf_frequency_locked()
    // to recalibrate the image for the active band even though the cached freq is
    // unchanged, or the revived receiver stays deaf (senses RF but never
    // correlates a preamble).
    force_image_cal_ = true;
    return init_locked();
#else
    return false;
#endif
}

bool Sx126xRadio::reestablish_lora_locked()
{
    if (!lora_cfg_valid_)
    {
        set_error_locked("re-establish: no cached LoRa config");
        return false;
    }
    if (!reset_chip_locked())
    {
        return false;
    }
    // Re-apply the exact modulation/packet/sync configuration the radio was last
    // told to use; leave the chip in STDBY_RC (configure_lora_locked ends in
    // standby) ready for the TX setup that follows.
    if (!configure_lora_locked(freq_mhz_, lora_bw_khz_, lora_sf_, lora_cr_, lora_tx_power_,
                               lora_preamble_, lora_sync_word_, lora_crc_len_))
    {
        set_error_locked("re-establish: LoRa reconfigure failed");
        return false;
    }
    ESP_LOGW(kTag,
             "SX1262 was unresponsive before TX; reset + reconfigured (freq=%.3f sf=%u bw=%.1f)",
             static_cast<double>(freq_mhz_),
             static_cast<unsigned>(lora_sf_),
             static_cast<double>(lora_bw_khz_));
    return true;
}

int Sx126xRadio::startTransmit(const uint8_t* data, size_t size)
{
    if (!take_mutex(mutex_))
    {
        return -1;
    }

    // Cache the exact outbound payload so the post-TX RX arm can recognise (and
    // discard) the SX1262's spurious self-reception of this very frame off the shared
    // FIFO. Stored before any early return below so it always reflects the last real
    // transmit attempt's bytes.
    if (data && size > 0 && size <= sizeof(last_tx_frame_))
    {
        std::memcpy(last_tx_frame_, data, size);
        last_tx_frame_len_ = size;
        post_tx_pending_ = true;
    }

    // ROOT CAUSE: on the T-Display-P4 the SX1262 loses its entire state while
    // parked in continuous RX -- by the time the first advert is sent the chip is
    // fully unresponsive on SPI (its version register and every status read return
    // 0x00), so SetTx is issued into a dead chip and TxDone never fires. If the
    // chip is dark, hardware-reset it and re-apply the cached LoRa configuration
    // so there is a live, configured radio to transmit with.
    if (!chip_responsive_locked())
    {
        if (!reestablish_lora_locked())
        {
            give_mutex(mutex_);
            return -1;
        }
    }

    // The adapter parks the SX1262 in CONTINUOUS RX (startReceive ->
    // set_rx_locked(kRxTimeoutInf)) after configuring the radio, and the inline
    // RX poll keeps re-arming it. Out of continuous RX the chip will not reliably
    // accept buffer/packet-param writes nor transition RX->TX, so the SetTx that
    // follows issues but TxDone never fires. Command SetStandby (STDBY_RC) FIRST,
    // before any TX setup, so the chip is in a known state for the buffer/packet
    // writes and the RX->STDBY->TX transition (the exact pattern used after init
    // at ~L547 and in configure_lora_locked at ~L971).
    const uint8_t standby_mode = kStandbyRc;
    bool ok = write_command_locked(kCmdSetStandby, &standby_mode, 1, true);

    uint8_t packet[9] = {0};
    ok = ok && set_buffer_base_locked(0x00, 0x00);
    board_prepare_lora_direction(true);
    if (packet_type_ == kPacketTypeLoRa)
    {
        // Transmit with the SAME LoRa preamble length the receiver is configured
        // for (cached from configure_lora_locked). A mismatch here was why two
        // units never decoded each other: RX was armed for a 16-symbol preamble
        // (the adapter configures preamble=16) while TX hardcoded 8, so the
        // receiver sensed the RF energy (RSSI bumped) but its preamble detector
        // never validated -- GetStats stayed rx=0/crc_err=0/hdr_err=0 and RxDone
        // never fired. Use the cached preamble (fallback to 16 if unset) so the
        // on-air preamble matches what the peer is listening for.
        // Re-issue the LoRa MODULATION params (SF/BW/CR/LDRO) from the cached
        // configuration immediately before transmit. The chip persists modulation
        // across standby, but re-asserting it here makes the TX modem provably
        // identical to what the RX demod was configured for -- closing the only
        // demod parameter that is not register-verifiable (SetModulationParams has
        // no read-back command). If TX and RX ever drifted on SF/BW/CR/LDRO the
        // peer would hear RF energy (RSSI bumps) but never correlate the LoRa
        // preamble, which is exactly the observed failure.
        if (lora_cfg_valid_)
        {
            const uint8_t mod[4] = {lora_sf_, map_lora_bw(lora_bw_khz_), map_lora_cr(lora_cr_),
                                    calc_ldro(lora_sf_, lora_bw_khz_)};
            ok = ok && write_command_locked(kCmdSetModulationParams, mod, sizeof(mod), true);
            std::memcpy(last_mod_bytes_, mod, sizeof(mod));
            have_last_mod_ = true;
        }
        // SX1262 §15.4 standard-IQ workaround on the TX path too. RadioLib applies
        // fixInvertedIQ on EVERY setPacketParams (TX and RX). Our TX previously wrote
        // SetPacketParams WITHOUT first re-asserting REG_IQ bit 2, so the on-air IQ
        // state of a transmit depended on whatever the last RX arm left in 0x0736.
        // Assert it here (guarded read) so TX and RX are provably IQ-identical on-air.
        (void)apply_standard_iq_workaround_locked();
        // SX1262 §15.1 "Modulation Quality" register (0x0889): RadioLib's staged-mode
        // config re-applies fixSensitivity before EVERY transmit, not just at config.
        // The readback diagnostic PROVED this register diverges TX-vs-RX on this board
        // (RX reads 0x0E, TX read 0x04 -- bits 1,3 cleared by TX time), and 0x0889 is
        // the TX MODULATION-QUALITY register, so a wrong value here distorts the
        // transmitted LoRa symbols -> the peer demodulates bit errors and every frame
        // fails CRC. Re-assert it before SetTx so the transmitted waveform carries the
        // same §15.1 fix the RX side has, closing the runtime TX-vs-RX divergence.
        if (lora_cfg_valid_)
        {
            (void)fix_rx_sensitivity_locked(lora_bw_khz_);
        }
        const uint16_t tx_preamble = lora_cfg_valid_ && lora_preamble_ > 0 ? lora_preamble_ : 16;
        const uint8_t lo[6] = {
            static_cast<uint8_t>((tx_preamble >> 8) & 0xFF),
            static_cast<uint8_t>(tx_preamble & 0xFF),
            kLoRaHeaderExplicit,
            static_cast<uint8_t>(size),
            kLoRaCrcOn,
            kLoRaIqStandard,
        };
        ok = ok && write_command_locked(kCmdSetPacketParams, lo, sizeof(lo), true);
        std::memcpy(last_pkt_bytes_, lo, sizeof(lo));
        have_last_pkt_ = true;
        if (ok && tx_rdbk_count_ < 3)
        {
            // MANDATORY DIAGNOSTIC (TX side): same fields as the RX 'rdbk' line so
            // the two can be diffed for an on-air TX-vs-RX divergence. Read the
            // IQ/sensitivity/sync registers back over SPI right before SetTx.
            uint8_t iq_rb = 0, sens_rb = 0, sync_rb[2] = {0};
            (void)read_register_locked(kRegIqConfig, &iq_rb, 1);
            (void)read_register_locked(kRegSensitivityConfig, &sens_rb, 1);
            (void)read_register_locked(kRegLoraSyncWordMsb, sync_rb, sizeof(sync_rb));
            printf("idf-mc: rdbk side=TX mod=%02X%02X%02X%02X pkt=%02X%02X%02X%02X%02X%02X "
                   "iq=0x%02X sens=0x%02X sync=%02X%02X len=%u\n",
                   static_cast<unsigned>(last_mod_bytes_[0]), static_cast<unsigned>(last_mod_bytes_[1]),
                   static_cast<unsigned>(last_mod_bytes_[2]), static_cast<unsigned>(last_mod_bytes_[3]),
                   static_cast<unsigned>(lo[0]), static_cast<unsigned>(lo[1]), static_cast<unsigned>(lo[2]),
                   static_cast<unsigned>(lo[3]), static_cast<unsigned>(lo[4]), static_cast<unsigned>(lo[5]),
                   static_cast<unsigned>(iq_rb), static_cast<unsigned>(sens_rb),
                   sync_rb[0], sync_rb[1], static_cast<unsigned>(size));
            // First TX payload bytes, to compare on-air against the peer's rxbytes.
            if (data && size >= 16)
            {
                printf("idf-mc: txbytes %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X "
                       "%02X %02X %02X %02X\n",
                       data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7],
                       data[8], data[9], data[10], data[11], data[12], data[13], data[14], data[15]);
            }
            ++tx_rdbk_count_;
        }
    }
    else
    {
        packet[0] = 0x00;
        packet[1] = 0x10;
        packet[2] = kFskPreambleDetect16;
        packet[3] = 16;
        packet[4] = kFskAddressFilterOff;
        packet[5] = kFskPacketVariable;
        packet[6] = static_cast<uint8_t>(size);
        packet[7] = kFskCrc2ByteInv;
        packet[8] = kFskWhiteningOff;
        ok = ok && write_command_locked(kCmdSetPacketParams, packet, sizeof(packet), true);
    }

    if (ok)
    {
        // Shared member SPI scratch (held under mutex_); the fill+transmit below
        // is atomic with no nested *_locked() scratch use, so reuse is safe and
        // keeps this 260-byte frame off the (small) app-loop task stack.
        uint8_t* tx = spi_tx_scratch_;
        if (size + 2 > kSpiScratchSize)
        {
            ok = false;
            set_error_locked("payload too large");
        }
        else
        {
            tx[0] = kCmdWriteBuffer;
            tx[1] = 0x00;
            std::memcpy(tx + 2, data, size);
            spi_transaction_t trans{};
            trans.length = (size + 2) * 8;
            trans.tx_buffer = tx;
            const esp_err_t err = spi_device_transmit(device_handle(device_), &trans);
            ok = err == ESP_OK;
            if (!ok)
            {
                set_error_locked("write buffer failed");
            }
        }
    }
    ok = ok && set_dio_irq_params_locked(kIrqTxDone | kIrqTimeout, kIrqTxDone) && clear_irq_locked(kIrqAll) &&
         set_tx_locked(0x000000);

    if (ok)
    {
        // Block until the SX1262's own TxDone IRQ latches (the real chip-level
        // transmit-complete signal), then return WITHOUT clearing it so the
        // caller's TxDone poll reads the genuine flag. DIO1 is not wired to the
        // ESP32-P4 on this board (irq=-1), so TxDone is only observable by reading
        // GetIrqStatus over SPI; and the measured completion latency for a
        // SF7/BW62.5 advert is ~720 ms -- longer than the caller's airtime-based
        // wait window -- so the radio driver owns the wait here. The mutex is held
        // for the duration: a transmit is a single-owner critical section (RX
        // cannot be re-armed until TX completes anyway), and the poll yields each
        // iteration so the rest of the system keeps running.
        const int64_t t0 = esp_timer_get_time();
        bool tx_done = false;
        uint16_t irq = 0;
        while ((esp_timer_get_time() - t0) < kTxCompleteTimeoutUs)
        {
            uint8_t irqb[2] = {0};
            if (read_command_locked(kCmdGetIrqStatus, nullptr, 0, irqb, sizeof(irqb), true))
            {
                irq = (static_cast<uint16_t>(irqb[0]) << 8) | irqb[1];
                if ((irq & kIrqTxDone) != 0)
                {
                    tx_done = true;
                    break;
                }
                if ((irq & kIrqTimeout) != 0)
                {
                    set_error_locked("tx hardware timeout");
                    break;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(2));
        }
        if (tx_diag_count_ < 3)
        {
            const int64_t took_us = esp_timer_get_time() - t0;
            ESP_LOGI(kTag,
                     "SX1262 TX diag[%lu]: txdone=%d took_ms=%lld irq=0x%04X len=%u",
                     static_cast<unsigned long>(tx_diag_count_),
                     tx_done ? 1 : 0,
                     took_us / 1000,
                     static_cast<unsigned>(irq),
                     static_cast<unsigned>(size));
            ++tx_diag_count_;
        }
        // The TxDone bit is intentionally left latched for the caller to observe.
        ok = tx_done;
    }

    // Clean TX->RX hand-off. After TxDone the chip auto-falls back to STDBY_RC, but
    // command an explicit standby and let the PA/antenna path settle before the
    // adapter re-arms RX. Without the settle, SetRx fires while this unit's OWN
    // just-transmitted burst is still decaying on the shared antenna, and the demod
    // completes a bogus self-reception (RxDone+CrcErr) reading the stale TX FIFO --
    // observed as 'rxbytes == own txbytes' immediately after every advert, polluting
    // the RX path with phantom frames instead of the peer's. A brief standby+settle
    // lets the residual die so the next RX arm starts clean and only real peer frames
    // trigger RxDone. NOTE: TxDone is intentionally LEFT LATCHED (only RX-terminal
    // bits are cleared) so the adapter's airtime-bounded TxDone poll still observes a
    // genuine completion -- clearing TxDone here would make it log done=0.
    {
        const uint8_t standby_mode = kStandbyRc;
        (void)write_command_locked(kCmdSetStandby, &standby_mode, 1, true);
        clear_irq_locked(kIrqRxDone | kIrqCrcErr | kIrqHeaderErr | kIrqTimeout);
        vTaskDelay(pdMS_TO_TICKS(8));
    }

    give_mutex(mutex_);
    return ok ? 0 : -1;
}

uint32_t Sx126xRadio::getIrqFlags()
{
    if (!take_mutex(mutex_))
    {
        return 0;
    }
    uint8_t irq[2] = {0};
    const bool ok = read_command_locked(kCmdGetIrqStatus, nullptr, 0, irq, sizeof(irq), true);
    give_mutex(mutex_);
    if (!ok)
    {
        return 0;
    }
    return (static_cast<uint32_t>(irq[0]) << 8) | irq[1];
}

void Sx126xRadio::clearIrqFlags(uint32_t flags)
{
    if (!take_mutex(mutex_))
    {
        return;
    }
    clear_irq_locked(static_cast<uint16_t>(flags));
    give_mutex(mutex_);
}

bool Sx126xRadio::pollRxLadder(uint16_t irq, RxLadderCounts* out, bool deep)
{
    if (!take_mutex(mutex_))
    {
        return false;
    }

    // RACE-FREE COUNTING. The IRQ word is read ONCE by the caller (the adapter's RX
    // poll) and passed in here, so the ladder accounting and the adapter's terminal-
    // flag handling/clear operate on the SAME snapshot. We do NOT issue our own
    // GetIrqStatus and do NOT clear any IRQ bits here (the adapter clears the
    // terminal bits, and the preamble/header bits naturally clear when it clears on
    // RxDone) -- this is why a previous design under-counted: its separate read +
    // mid-packet clear raced the adapter's clear. We never issue SetRx/SetStandby,
    // so this cannot disturb an in-flight reception.
    const bool dead = (irq == 0x0000u) || (irq == 0xFFFFu);
    if (!dead)
    {
        // Cumulative OR of every non-dead IRQ word, so the throttled line shows
        // which IRQ bits actually appear in GetIrqStatus during reception (decisive
        // for whether PreambleDetected/HeaderValid latch at all on this chip).
        rxladder_irq_seen_ |= irq;
    }

    // ROOT-CAUSE FIX: only the throttled (deep) poll touches the chip over SPI. The
    // fast 30 ms poll does ZERO SPI here so it cannot disturb an in-flight reception
    // (deterministic 30 ms-spaced GetStatus/GetRssiInst/GetDeviceErrors bursts were
    // corrupting the LoRa data symbols -> RxDone with a garbage explicit header).
    unsigned mode = rxladder_last_mode_;
    ++rxladder_polls_;
    if (deep)
    {
        const uint8_t status = read_chip_status_locked();
        mode = (status >> 4) & 0x07;
        rxladder_last_mode_ = mode;

        // Count how many deep polls observed the chip NOT in RX (mode != 0x5).
        if (mode != 0x5)
        {
            ++rxladder_notrx_polls_;
        }

        // Device-error register (GetDeviceErrors): a chip in mode=RX that cannot
        // demodulate usually has a latched PLL/XOSC/IMG error. OR into the cumulative
        // value so a transient latch is not missed.
        uint8_t dev_err[2] = {0};
        if (read_command_locked(kCmdGetDeviceErrors, nullptr, 0, dev_err, sizeof(dev_err), true))
        {
            const uint16_t errs = (static_cast<uint16_t>(dev_err[0]) << 8) | dev_err[1];
            if (errs != 0xFFFFu)
            {
                rxladder_dev_errors_ |= errs;
            }
        }

        // Peak VALID instantaneous RSSI (mode-5 only, ignoring raw==0 glitch reads).
        if (mode == 0x5)
        {
            uint8_t rssi_raw = 0;
            if (read_command_locked(kCmdGetRssiInst, nullptr, 0, &rssi_raw, 1, true) && rssi_raw != 0)
            {
                const float rssi_dbm = static_cast<float>(rssi_raw) / -2.0f;
                if (rssi_dbm > rxladder_peak_rssi_)
                {
                    rxladder_peak_rssi_ = rssi_dbm;
                }
            }
        }

        // AUTHORITATIVE RECEPTION COUNT via GetStats (0x10). DECISIVE on-hardware
        // finding: on this board the SX1262 receives the peer's adverts CLEANLY -- the
        // modem's NbPktReceived counter advances with NbPktCrcError == 0 -- yet the
        // RxDone IRQ never surfaces in GetIrqStatus (irqseen stays 0x0000) and DIO1
        // never latches it for the pump. So the IRQ/DIO path is unreliable for clean
        // LoRa RX here, but the GetStats packet counters are not: they are the modem's
        // own ground truth. Fold the NbPktReceived / NbPktCrcError deltas into the
        // ladder's rxdone / crcerr counters so crcok = rxdone - crcerr reflects the
        // chip's real clean-reception count instead of an IRQ that this silicon does not
        // raise. (The IRQ rising-edge counting below still runs and still catches the
        // post-TX phantom / any IRQ that does fire; the GetStats fold is additive and
        // only ever increases rxdone/crcerr by genuine modem receptions.)
        {
            uint8_t stats[6] = {0};
            if (read_command_locked(kCmdGetStats, nullptr, 0, stats, sizeof(stats), true))
            {
                const uint16_t pkt_rx = (static_cast<uint16_t>(stats[0]) << 8) | stats[1];
                const uint16_t crc_err = (static_cast<uint16_t>(stats[2]) << 8) | stats[3];
                const uint16_t hdr_err = (static_cast<uint16_t>(stats[4]) << 8) | stats[5];
                // GetStats counters are free-running 16-bit modem counters reset only by
                // a reconfigure. Track them as a baseline and add only forward deltas.
                if (!rxstats_have_baseline_)
                {
                    rxstats_have_baseline_ = true;
                }
                else
                {
                    if (pkt_rx != rxstats_last_pkt_rx_)
                    {
                        const uint16_t d = static_cast<uint16_t>(pkt_rx - rxstats_last_pkt_rx_);
                        // A clean reception = total received minus those that CRC-failed.
                        rxladder_rxdone_ += d;
                    }
                    if (crc_err != rxstats_last_crc_err_)
                    {
                        const uint16_t d = static_cast<uint16_t>(crc_err - rxstats_last_crc_err_);
                        rxladder_crcerr_ += d;
                    }
                }
                rxstats_last_pkt_rx_ = pkt_rx;
                rxstats_last_crc_err_ = crc_err;
                if (rxstats_diag_count_ < 40)
                {
                    printf("idf-mc: rxstats rx=%u crcerr=%u hdrerr=%u\n",
                           static_cast<unsigned>(pkt_rx), static_cast<unsigned>(crc_err),
                           static_cast<unsigned>(hdr_err));
                    ++rxstats_diag_count_;
                }
            }
        }
    }

    if (!dead)
    {
        // Rising-edge count each ladder bit from the caller's single IRQ snapshot.
        // The adapter clears the terminal bits (RxDone/CrcErr/HeaderErr/Timeout) on
        // each reception, which also clears PreambleDetected/HeaderValid, so every
        // bit re-latches per packet and is counted exactly once per reception.
        const bool pre = (irq & kIrqPreambleDetected) != 0;
        const bool hdr = (irq & kIrqHeaderValid) != 0;
        const bool rxd = (irq & kIrqRxDone) != 0;
        const bool crc = (irq & kIrqCrcErr) != 0;
        if (pre && !rxladder_prev_preamble_)
        {
            ++rxladder_preamble_;
        }
        if (hdr && !rxladder_prev_header_)
        {
            ++rxladder_header_;
        }
        if (rxd && !rxladder_prev_rxdone_)
        {
            ++rxladder_rxdone_;
        }
        if (crc && !rxladder_prev_crcerr_)
        {
            ++rxladder_crcerr_;
        }
        rxladder_prev_preamble_ = pre;
        rxladder_prev_header_ = hdr;
        rxladder_prev_rxdone_ = rxd;
        rxladder_prev_crcerr_ = crc;
    }

    if (out)
    {
        out->preamble = rxladder_preamble_;
        out->header = rxladder_header_;
        out->rxdone = rxladder_rxdone_;
        out->crcerr = rxladder_crcerr_;
        out->mode = mode;
        out->peak_rssi_dbm = rxladder_peak_rssi_;
        out->dev_errors = rxladder_dev_errors_;
        out->polls = rxladder_polls_;
        out->notrx_polls = rxladder_notrx_polls_;
        out->irq_seen = rxladder_irq_seen_;
    }
    give_mutex(mutex_);
    return true;
}

void Sx126xRadio::logRxDecodeDiag()
{
    if (rxdec_count_ >= 6)
    {
        return;
    }
    if (!take_mutex(mutex_))
    {
        return;
    }
    uint8_t cr_reg = 0;
    (void)read_register_locked(kRegLoraRxCodingRate, &cr_reg, 1);
    // Frequency-error registers 0x076B..0x076D (RadioLib getFrequencyError): the
    // measured carrier offset of the just-received frame. A large CFO would explain
    // a header that demodulates to garbage while the preamble still locks (RxDone).
    uint8_t efe[3] = {0};
    (void)read_register_locked(kRegFreqErrorRxCrc, &efe[0], 1);
    (void)read_register_locked(static_cast<uint16_t>(kRegFreqErrorRxCrc + 1), &efe[1], 1);
    (void)read_register_locked(static_cast<uint16_t>(kRegFreqErrorRxCrc + 2), &efe[2], 1);
    const uint8_t crc_reg = efe[0];
    uint32_t efe_raw = (static_cast<uint32_t>(efe[0]) << 16) |
                       (static_cast<uint32_t>(efe[1]) << 8) | efe[2];
    efe_raw &= 0x0FFFFFu;
    float cfo_hz = 0.0f;
    const float bwk = (lora_bw_khz_ > 0.1f) ? lora_bw_khz_ : 62.5f;
    if (efe_raw & 0x80000u)
    {
        efe_raw |= 0xFFF00000u;
        const uint32_t mag = ~efe_raw + 1u;
        cfo_hz = 1.55f * static_cast<float>(mag) / (1600.0f / bwk) * -1.0f * 1000.0f;
    }
    else
    {
        cfo_hz = 1.55f * static_cast<float>(efe_raw) / (1600.0f / bwk) * 1000.0f;
    }
    uint8_t bufstat[2] = {0};
    (void)read_command_locked(kCmdGetRxBufferStatus, nullptr, 0, bufstat, sizeof(bufstat), true);
    uint16_t irq = 0;
    {
        uint8_t irqb[2] = {0};
        if (read_command_locked(kCmdGetIrqStatus, nullptr, 0, irqb, sizeof(irqb), true))
        {
            irq = (static_cast<uint16_t>(irqb[0]) << 8) | irqb[1];
        }
    }
    // Decoded coding rate = bits 6:4 of 0x0749 (1=4/5,2=4/6,3=4/7,4=4/8). Received
    // CRC-present = bit 4 of 0x076B. These come straight from the demodulated
    // header, so they reveal whether the explicit header decoded sanely.
    const unsigned decoded_cr = static_cast<unsigned>((cr_reg >> 4) & 0x07);
    const unsigned hdr_crc_on = static_cast<unsigned>((crc_reg >> 4) & 0x01);
    printf("idf-mc: rxdec rawcr=0x%02X cr=%u rawcrc=0x%02X crcon=%u rxlen=%u rxoff=%u cfo=%.0fHz irq=0x%04X\n",
           static_cast<unsigned>(cr_reg), decoded_cr,
           static_cast<unsigned>(crc_reg), hdr_crc_on,
           static_cast<unsigned>(bufstat[0]), static_cast<unsigned>(bufstat[1]),
           static_cast<double>(cfo_hz),
           static_cast<unsigned>(irq));
    // Dump the first bytes of what actually landed in the RX buffer. A real MeshCore
    // advert starts with a route/header byte then a 0x04 (advert) payload type and a
    // 32-byte pubkey; if these bytes resemble that (with bit errors) it is a PHY/demod
    // corruption, if they are pure noise the receiver locked onto something else.
    {
        const uint8_t offset = bufstat[1];
        uint8_t buf[24] = {0};
        if (read_command_locked(kCmdReadBuffer, &offset, 1, buf, sizeof(buf), true))
        {
            printf("idf-mc: rxbytes %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X "
                   "%02X %02X %02X %02X\n",
                   buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7],
                   buf[8], buf[9], buf[10], buf[11], buf[12], buf[13], buf[14], buf[15]);
        }
    }
    ++rxdec_count_;
    give_mutex(mutex_);
}

int Sx126xRadio::getPacketLength(bool update)
{
    (void)update;
    if (!take_mutex(mutex_))
    {
        return -1;
    }
    uint8_t status[2] = {0};
    const bool ok = read_command_locked(kCmdGetRxBufferStatus, nullptr, 0, status, sizeof(status), true);
    if (ok)
    {
        last_rx_offset_ = status[1];
    }
    give_mutex(mutex_);
    return ok ? static_cast<int>(status[0]) : -1;
}

int Sx126xRadio::readPacket(uint8_t* buffer, size_t size)
{
    if (!take_mutex(mutex_))
    {
        return -1;
    }

    uint8_t status[2] = {0};
    bool ok = read_command_locked(kCmdGetRxBufferStatus, nullptr, 0, status, sizeof(status), true);
    if (!ok)
    {
        give_mutex(mutex_);
        return -1;
    }
    last_rx_offset_ = status[1];
    const size_t packet_length = std::min<size_t>(status[0], size);
    ok = read_command_locked(kCmdReadBuffer, &last_rx_offset_, 1, buffer, packet_length, true);
    give_mutex(mutex_);
    return ok ? 0 : -1;
}

bool Sx126xRadio::isRxPayloadEmpty()
{
    if (!take_mutex(mutex_))
    {
        return false;
    }
    uint8_t status[2] = {0};
    if (!read_command_locked(kCmdGetRxBufferStatus, nullptr, 0, status, sizeof(status), true))
    {
        give_mutex(mutex_);
        return false; // could not read -> treat as a real frame (do not suppress)
    }
    const uint8_t rx_len = status[0];
    const uint8_t rx_off = status[1];
    // Scan the leading payload bytes. A genuine reception writes payload here; the
    // post-TX phantom leaves the drained zeros. Bound the scan so it stays a single
    // short SPI burst.
    const size_t scan_len = std::min<size_t>(rx_len == 0 ? 16u : rx_len, 64u);
    uint8_t buf[64] = {0};
    bool all_zero = true;
    if (read_command_locked(kCmdReadBuffer, &rx_off, 1, buf, scan_len, true))
    {
        for (size_t i = 0; i < scan_len; ++i)
        {
            if (buf[i] != 0x00)
            {
                all_zero = false;
                break;
            }
        }
    }
    else
    {
        all_zero = false; // read failed -> do not suppress
    }
    give_mutex(mutex_);
    return all_zero;
}

const char* Sx126xRadio::lastError() const
{
    return last_error_;
}

void Sx126xRadio::set_error_locked(const char* error)
{
    if (!error)
    {
        last_error_[0] = '\0';
        return;
    }
    std::snprintf(last_error_, sizeof(last_error_), "%s", error);
    ESP_LOGW(kTag, "%s", last_error_);
}

} // namespace platform::esp::idf_common
