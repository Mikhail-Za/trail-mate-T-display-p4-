#include "boards/t_display_p4/t_display_p4_board.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>

#include "boards/t_display_p4/rtc_runtime.h"
#include "driver/gpio.h"
#include "driver/sdmmc_default_configs.h"
#include "driver/sdmmc_host.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_vfs_fat.h"
#include "platform/esp/idf_common/bsp_runtime.h"
#include "platform/esp/idf_common/gps_runtime.h"
#include "platform/esp/idf_common/sx126x_radio.h"
#include "platform/ui/device_runtime.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"

namespace
{

constexpr const char* kTag = "TDisplayP4Board";
constexpr uint32_t kI2cTimeoutMs = 1000;
constexpr int kSdLdoChannel = 4;
constexpr uint8_t kBatteryRegVoltage = 0x08;
constexpr uint8_t kBatteryRegCurrent = 0x0C;
constexpr uint8_t kBatteryRegStateOfCharge = 0x2C;
// Minimum gap between fuel-gauge probe attempts after a failure, so a dead/absent
// gauge is not re-probed on every accessor call (Power app polls all four every ~2s).
constexpr uint32_t kBatteryGaugeProbeCooldownMs = 5000;

constexpr uint8_t kExpanderRegInput0 = 0x00;
constexpr uint8_t kExpanderRegInput1 = 0x01;
constexpr uint8_t kExpanderRegOutput0 = 0x02;
constexpr uint8_t kExpanderRegOutput1 = 0x03;
constexpr uint8_t kExpanderRegPolarity0 = 0x04;
constexpr uint8_t kExpanderRegPolarity1 = 0x05;
constexpr uint8_t kExpanderRegConfig0 = 0x06;
constexpr uint8_t kExpanderRegConfig1 = 0x07;

struct ExpanderPinLocation
{
    bool valid = false;
    uint8_t port = 0;
    uint8_t bit = 0;
};

ExpanderPinLocation locate_expander_pin(int pin)
{
    if (pin >= 0 && pin <= 7)
    {
        return {true, 0, static_cast<uint8_t>(pin)};
    }
    if (pin >= 10 && pin <= 17)
    {
        return {true, 1, static_cast<uint8_t>(pin - 10)};
    }
    return {};
}

platform::esp::idf_common::Sx126xRadio& radio()
{
    return platform::esp::idf_common::Sx126xRadio::instance();
}

} // namespace

namespace boards::t_display_p4
{

TDisplayP4Board::ManagedSystemI2cGuard::ManagedSystemI2cGuard(TDisplayP4Board& board,
                                                              const SystemI2cDeviceConfig& config,
                                                              uint32_t timeout_ms)
    : board_(&board)
{
    handle_ = board.getManagedSystemI2cDevice(config, timeout_ms);
    if (handle_ == nullptr)
    {
        return;
    }

    locked_ = board.lockSystemI2c(timeout_ms);
    if (!locked_)
    {
        ESP_LOGW(kTag,
                 "Failed to lock SYS I2C for owner=%s addr=0x%02X",
                 config.owner ? config.owner : "unknown",
                 static_cast<unsigned>(config.address));
        handle_ = nullptr;
    }
}

TDisplayP4Board::ManagedSystemI2cGuard::~ManagedSystemI2cGuard()
{
    if (locked_ && board_ != nullptr)
    {
        board_->unlockSystemI2c();
    }
}

bool TDisplayP4Board::ManagedSystemI2cGuard::ok() const
{
    return locked_ && handle_ != nullptr;
}

TDisplayP4Board::ManagedSystemI2cGuard::operator bool() const
{
    return ok();
}

i2c_master_dev_handle_t TDisplayP4Board::ManagedSystemI2cGuard::handle() const
{
    return handle_;
}

esp_err_t TDisplayP4Board::ManagedSystemI2cGuard::transmit(const uint8_t* data,
                                                           size_t len,
                                                           uint32_t timeout_ms) const
{
    if (!ok() || data == nullptr || len == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }
    return i2c_master_transmit(handle_, data, len, timeout_ms);
}

esp_err_t TDisplayP4Board::ManagedSystemI2cGuard::transmitReceive(const uint8_t* tx_data,
                                                                  size_t tx_len,
                                                                  uint8_t* rx_data,
                                                                  size_t rx_len,
                                                                  uint32_t timeout_ms) const
{
    if (!ok() || tx_data == nullptr || tx_len == 0 || rx_data == nullptr || rx_len == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }
    return i2c_master_transmit_receive(handle_, tx_data, tx_len, rx_data, rx_len, timeout_ms);
}

TDisplayP4Board& TDisplayP4Board::instance()
{
    static TDisplayP4Board board_instance;
    return board_instance;
}

TDisplayP4Board::TDisplayP4Board()
{
    system_i2c_mutex_ = xSemaphoreCreateMutex();
    if (system_i2c_mutex_ == nullptr)
    {
        ESP_LOGE(kTag, "Failed to create SYS I2C mutex");
    }
}

uint32_t TDisplayP4Board::begin(uint32_t disable_hw_init)
{
    (void)disable_hw_init;
    if (started_)
    {
        return 0;
    }

    const auto& panel = activePanel();
    ESP_LOGI(kTag,
             "begin panel=%s size=%dx%d sys_i2c=(%d,%d,%d) ext_i2c=(%d,%d,%d) gps_uart=(%d,%d,%d,%lu) "
             "sdmmc=(%d,%d,%d,%d,%d,%d) lora_spi=(host=%d sck=%d miso=%d mosi=%d) lora_ctrl=(nss=%d busy=%d) "
             "expander=0x%02X backlight=%d",
             configuredPanelType() == DisplayPanelType::Rm69a10 ? "rm69a10" : "hi8561",
             panel.width,
             panel.height,
             systemI2c().port,
             systemI2c().sda,
             systemI2c().scl,
             externalI2c().port,
             externalI2c().sda,
             externalI2c().scl,
             gpsUart().port,
             gpsUart().tx,
             gpsUart().rx,
             static_cast<unsigned long>(gpsUart().baud_rate),
             sdmmcPins().d0,
             sdmmcPins().d1,
             sdmmcPins().d2,
             sdmmcPins().d3,
             sdmmcPins().cmd,
             sdmmcPins().clk,
             loraModulePins().spi.host,
             loraModulePins().spi.sck,
             loraModulePins().spi.miso,
             loraModulePins().spi.mosi,
             loraModulePins().nss,
             loraModulePins().busy,
             profile().i2c.io_expander,
             profile().lcd_backlight);

    const bool buses_ok = initializeI2cBuses();
    const bool expander_ok = buses_ok && initializeExpander();
    const bool power_ok = expander_ok && runColdBootPowerSequence();
    (void)initializeBatteryGauge();
    (void)ensureRtcAccessible();

    started_ = buses_ok && expander_ok && power_ok;
    return started_ ? 0 : 1;
}

void TDisplayP4Board::wakeUp()
{
    (void)platform::esp::idf_common::bsp_runtime::wake_display();
}

void TDisplayP4Board::handlePowerButton() {}

void TDisplayP4Board::softwareShutdown() {}

void TDisplayP4Board::enterScreenSleep()
{
    (void)platform::esp::idf_common::bsp_runtime::sleep_display();
}

void TDisplayP4Board::exitScreenSleep()
{
    (void)platform::esp::idf_common::bsp_runtime::wake_display();
}

void TDisplayP4Board::setBrightness(uint8_t level)
{
    brightness_level_ = level;
    const int percent = (DEVICE_MAX_BRIGHTNESS_LEVEL <= 0)
                            ? 100
                            : static_cast<int>((static_cast<uint32_t>(level) * 100U) /
                                               static_cast<uint32_t>(DEVICE_MAX_BRIGHTNESS_LEVEL));
    (void)platform::esp::idf_common::bsp_runtime::set_display_brightness(percent);
}

uint8_t TDisplayP4Board::getBrightness()
{
    return brightness_level_;
}

bool TDisplayP4Board::hasKeyboard()
{
    return false;
}

void TDisplayP4Board::keyboardSetBrightness(uint8_t level)
{
    (void)level;
}

uint8_t TDisplayP4Board::keyboardGetBrightness()
{
    return 0;
}

bool TDisplayP4Board::isRTCReady() const
{
    return rtc_runtime::is_valid_epoch(std::time(nullptr));
}

bool TDisplayP4Board::isCharging()
{
    if (!battery_gauge_ready_)
    {
        (void)initializeBatteryGauge();
    }

    int16_t current_ma = 0;
    if (!readBatteryGaugeWordSigned(kBatteryRegCurrent, &current_ma))
    {
        return battery_charging_;
    }

    battery_charging_ = current_ma > 0;
    return battery_charging_;
}

int TDisplayP4Board::getBatteryLevel()
{
    if (!battery_gauge_ready_)
    {
        (void)initializeBatteryGauge();
    }

    uint16_t level = 0;
    if (!readBatteryGaugeWord(kBatteryRegStateOfCharge, &level))
    {
        return last_battery_level_;
    }

    last_battery_level_ = std::clamp<int>(static_cast<int>(level), 0, 100);
    return last_battery_level_;
}

int TDisplayP4Board::getBatteryVoltageMv()
{
    if (!battery_gauge_ready_)
    {
        (void)initializeBatteryGauge();
    }

    uint16_t voltage_mv = 0;
    if (!readBatteryGaugeWord(kBatteryRegVoltage, &voltage_mv))
    {
        return -1;
    }

    return static_cast<int>(voltage_mv);
}

int TDisplayP4Board::getBatteryCurrentMa(bool* ok)
{
    if (!battery_gauge_ready_)
    {
        (void)initializeBatteryGauge();
    }

    int16_t current_ma = 0;
    if (!readBatteryGaugeWordSigned(kBatteryRegCurrent, &current_ma))
    {
        if (ok != nullptr)
        {
            *ok = false;
        }
        return 0;
    }

    if (ok != nullptr)
    {
        *ok = true;
    }
    return static_cast<int>(current_ma);
}

bool TDisplayP4Board::isSDReady() const
{
    return sd_ready_;
}

bool TDisplayP4Board::isCardReady()
{
    return sd_ready_;
}

bool TDisplayP4Board::isGPSReady() const
{
    return gps_runtime_prepared_ || platform::esp::idf_common::gps_runtime::is_enabled() ||
           platform::esp::idf_common::gps_runtime::is_powered();
}

void TDisplayP4Board::vibrator() {}

void TDisplayP4Board::stopVibrator() {}

void TDisplayP4Board::setMessageToneVolume(uint8_t volume_percent)
{
    message_tone_volume_ = volume_percent;
}

uint8_t TDisplayP4Board::getMessageToneVolume() const
{
    return message_tone_volume_;
}

bool TDisplayP4Board::lockSystemI2c(uint32_t timeout_ms)
{
    if (system_i2c_mutex_ == nullptr)
    {
        return false;
    }

    TickType_t timeout_ticks = (timeout_ms == 0) ? 0 : pdMS_TO_TICKS(timeout_ms);
    if (timeout_ms == UINT32_MAX)
    {
        timeout_ticks = portMAX_DELAY;
    }
    return xSemaphoreTake(system_i2c_mutex_, timeout_ticks) == pdTRUE;
}

void TDisplayP4Board::unlockSystemI2c()
{
    if (system_i2c_mutex_ != nullptr)
    {
        xSemaphoreGive(system_i2c_mutex_);
    }
}

i2c_master_bus_handle_t TDisplayP4Board::systemI2cHandle() const
{
    return system_i2c_handle_;
}

i2c_master_bus_handle_t TDisplayP4Board::externalI2cHandle() const
{
    return external_i2c_handle_;
}

i2c_master_dev_handle_t TDisplayP4Board::addSystemI2cDevice(uint16_t address, uint32_t speed_hz) const
{
    if (system_i2c_handle_ == nullptr)
    {
        ESP_LOGW(kTag, "SYS I2C handle unavailable when adding device 0x%02X", address);
        return nullptr;
    }

    i2c_master_dev_handle_t handle = nullptr;
    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address = address;
    dev_cfg.scl_speed_hz = speed_hz;
    const esp_err_t err = i2c_master_bus_add_device(system_i2c_handle_, &dev_cfg, &handle);
    if (err != ESP_OK)
    {
        ESP_LOGW(kTag,
                 "Failed to add SYS I2C device addr=0x%02X err=%s",
                 static_cast<unsigned>(address),
                 esp_err_to_name(err));
        return nullptr;
    }
    return handle;
}

void TDisplayP4Board::removeSystemI2cDevice(i2c_master_dev_handle_t handle) const
{
    if (handle == nullptr)
    {
        return;
    }
    const esp_err_t err = i2c_master_bus_rm_device(handle);
    if (err != ESP_OK)
    {
        ESP_LOGW(kTag, "Failed to remove SYS I2C device: %s", esp_err_to_name(err));
    }
}

i2c_master_dev_handle_t TDisplayP4Board::getManagedSystemI2cDevice(const SystemI2cDeviceConfig& config,
                                                                   uint32_t timeout_ms)
{
    if (config.address == 0)
    {
        return nullptr;
    }

    {
        std::lock_guard<std::mutex> lock(resource_mutex_);
        ManagedI2cSlot* existing = findManagedI2cSlot(config.address, config.speed_hz);
        if (existing != nullptr)
        {
            return existing->handle;
        }
    }

    if (!lockSystemI2c(timeout_ms))
    {
        ESP_LOGW(kTag,
                 "Failed to lock SYS I2C while creating managed device owner=%s addr=0x%02X",
                 config.owner ? config.owner : "unknown",
                 static_cast<unsigned>(config.address));
        return nullptr;
    }

    i2c_master_dev_handle_t handle = nullptr;
    {
        std::lock_guard<std::mutex> lock(resource_mutex_);
        ManagedI2cSlot* existing = findManagedI2cSlot(config.address, config.speed_hz);
        if (existing != nullptr)
        {
            unlockSystemI2c();
            return existing->handle;
        }

        ManagedI2cSlot* slot = findFreeManagedI2cSlot();
        if (slot == nullptr)
        {
            unlockSystemI2c();
            ESP_LOGW(kTag,
                     "No free managed SYS I2C slot for owner=%s addr=0x%02X",
                     config.owner ? config.owner : "unknown",
                     static_cast<unsigned>(config.address));
            return nullptr;
        }

        handle = addSystemI2cDevice(config.address, config.speed_hz);
        if (handle != nullptr)
        {
            slot->active = true;
            slot->address = config.address;
            slot->speed_hz = config.speed_hz;
            slot->handle = handle;
            copyOwnerTag(config.owner, slot->owner, sizeof(slot->owner));
        }
    }

    unlockSystemI2c();
    return handle;
}

bool TDisplayP4Board::expanderReady() const
{
    return expander_ready_;
}

bool TDisplayP4Board::expanderPinMode(int pin, bool output)
{
    if (!expander_ready_ && !initializeExpander())
    {
        return false;
    }

    const ExpanderPinLocation location = locate_expander_pin(pin);
    if (!location.valid)
    {
        return false;
    }

    const SystemI2cDeviceConfig config{"xl9535", profile().i2c.io_expander, 400000};
    ManagedSystemI2cGuard guard(*this, config, kI2cTimeoutMs);
    if (!guard)
    {
        return false;
    }

    uint8_t* cfg_cache = (location.port == 0) ? &expander_config_port0_ : &expander_config_port1_;
    if (output)
    {
        *cfg_cache = static_cast<uint8_t>(*cfg_cache & ~static_cast<uint8_t>(1u << location.bit));
    }
    else
    {
        *cfg_cache = static_cast<uint8_t>(*cfg_cache | static_cast<uint8_t>(1u << location.bit));
    }

    const uint8_t payload[2] = {
        static_cast<uint8_t>((location.port == 0) ? kExpanderRegConfig0 : kExpanderRegConfig1),
        *cfg_cache,
    };
    return guard.transmit(payload, sizeof(payload), kI2cTimeoutMs) == ESP_OK;
}

bool TDisplayP4Board::expanderWrite(int pin, bool high)
{
    if (!expander_ready_ && !initializeExpander())
    {
        return false;
    }

    const ExpanderPinLocation location = locate_expander_pin(pin);
    if (!location.valid)
    {
        return false;
    }

    const SystemI2cDeviceConfig config{"xl9535", profile().i2c.io_expander, 400000};
    ManagedSystemI2cGuard guard(*this, config, kI2cTimeoutMs);
    if (!guard)
    {
        return false;
    }

    uint8_t* out_cache = (location.port == 0) ? &expander_output_port0_ : &expander_output_port1_;
    if (high)
    {
        *out_cache = static_cast<uint8_t>(*out_cache | static_cast<uint8_t>(1u << location.bit));
    }
    else
    {
        *out_cache = static_cast<uint8_t>(*out_cache & ~static_cast<uint8_t>(1u << location.bit));
    }

    const uint8_t payload[2] = {
        static_cast<uint8_t>((location.port == 0) ? kExpanderRegOutput0 : kExpanderRegOutput1),
        *out_cache,
    };
    return guard.transmit(payload, sizeof(payload), kI2cTimeoutMs) == ESP_OK;
}

bool TDisplayP4Board::expanderRead(int pin, bool* out_high) const
{
    if (out_high == nullptr || (!expander_ready_ && !const_cast<TDisplayP4Board*>(this)->initializeExpander()))
    {
        return false;
    }

    const ExpanderPinLocation location = locate_expander_pin(pin);
    if (!location.valid)
    {
        return false;
    }

    const SystemI2cDeviceConfig config{"xl9535", profile().i2c.io_expander, 400000};
    ManagedSystemI2cGuard guard(const_cast<TDisplayP4Board&>(*this), config, kI2cTimeoutMs);
    if (!guard)
    {
        return false;
    }

    const uint8_t reg = (location.port == 0) ? kExpanderRegInput0 : kExpanderRegInput1;
    uint8_t value = 0;
    if (guard.transmitReceive(&reg, 1, &value, 1, kI2cTimeoutMs) != ESP_OK)
    {
        return false;
    }

    *out_high = (value & static_cast<uint8_t>(1u << location.bit)) != 0;
    return true;
}

bool TDisplayP4Board::expanderWriteActive(int pin, bool active, bool active_high)
{
    return expanderWrite(pin, active_high ? active : !active);
}

bool TDisplayP4Board::expanderReadActive(int pin, bool* out_active, bool active_high) const
{
    bool high = false;
    if (!expanderRead(pin, &high) || out_active == nullptr)
    {
        return false;
    }
    *out_active = active_high ? high : !high;
    return true;
}

bool TDisplayP4Board::resetTouchController(uint32_t pre_delay_ms,
                                           uint32_t reset_pulse_ms,
                                           uint32_t post_delay_ms)
{
    if (!expander_ready_ && !initializeExpander())
    {
        return false;
    }

    const auto& io = ioExpanderPins();
    if (!expanderPinMode(io.touch_rst, true))
    {
        return false;
    }

    const bool reset_active_high = !profile().touch_reset_active_low;
    if (!expanderWriteActive(io.touch_rst, false, reset_active_high))
    {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(pre_delay_ms));

    if (!expanderWriteActive(io.touch_rst, true, reset_active_high))
    {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(reset_pulse_ms));

    if (!expanderWriteActive(io.touch_rst, false, reset_active_high))
    {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(post_delay_ms));

    ESP_LOGI(kTag,
             "Touch reset complete pre=%lums pulse=%lums post=%lums",
             static_cast<unsigned long>(pre_delay_ms),
             static_cast<unsigned long>(reset_pulse_ms),
             static_cast<unsigned long>(post_delay_ms));
    return true;
}

bool TDisplayP4Board::isTouchInterruptActive() const
{
    bool active = false;
    if (!expanderReadActive(ioExpanderPins().touch_int, &active, false))
    {
        return false;
    }
    return active;
}

bool TDisplayP4Board::prepareGpsRuntime()
{
    if (gps_runtime_prepared_)
    {
        return true;
    }

    if (!expander_ready_ && !initializeExpander())
    {
        return false;
    }

    const auto& io = ioExpanderPins();
    // Mirror LilyGo's official l76k bring-up (T-Display-P4 main/examples/l76k/main.cpp):
    // power the L76K's rails and let them SETTLE before waking/talking to it. Our cold-
    // boot sequence enables these rails once, but the GPS worker starts seconds later and
    // got ZERO bytes at every baud + both pin orderings -- the signature of an UNPOWERED
    // module, not a baud/pin issue. Re-asserting the rails here makes GPS power self-
    // contained, independent of cold-boot state surviving.
    const auto& p = profile();
    (void)expanderPinMode(io.power_5v, true);
    (void)expanderPinMode(io.power_3v3, true);
    (void)expanderWriteActive(io.power_5v, true, p.power_5v_active_high);    // 5V rail ON (HIGH)
    (void)expanderWriteActive(io.power_3v3, true, p.power_3v3_active_high);  // 3V3 rail ON (active-low)
    vTaskDelay(pdMS_TO_TICKS(100));                                          // LilyGo's 100 ms settle
    if (!expanderPinMode(io.gps_wake, true))
    {
        return false;
    }
    if (!expanderWriteActive(io.gps_wake, true, profile().gps_wake_active_high))
    {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(50));  // let the L76K start streaming before the worker reads
    if (!configureGpsUart(gpsUart().baud_rate))
    {
        return false;
    }

    gps_runtime_prepared_ = true;
    ESP_LOGI(kTag, "GNSS runtime prepared");
    return true;
}

void TDisplayP4Board::teardownGpsRuntime()
{
    if (!gps_runtime_prepared_)
    {
        return;
    }

    teardownGpsUart();
    if (expander_ready_)
    {
        (void)expanderWriteActive(ioExpanderPins().gps_wake, false, profile().gps_wake_active_high);
    }
    gps_runtime_prepared_ = false;
    ESP_LOGI(kTag, "GNSS runtime released");
}

bool TDisplayP4Board::mountSdCard(const char* mount_point, size_t max_files)
{
    if (mount_point == nullptr || mount_point[0] == '\0')
    {
        return false;
    }

    if (sd_ready_)
    {
        return true;
    }

    if (!started_ && begin() != 0)
    {
        return false;
    }

    if (!expanderPinMode(ioExpanderPins().sd_enable, true) ||
        !expanderWriteActive(ioExpanderPins().sd_enable, true, !profile().sd_enable_active_low))
    {
        return false;
    }
    sd_enabled_ = true;

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = SDMMC_HOST_SLOT_0;
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;

    static sd_pwr_ctrl_handle_t pwr_ctrl_handle = nullptr;
    if (pwr_ctrl_handle == nullptr)
    {
        sd_pwr_ctrl_ldo_config_t ldo_config{};
        ldo_config.ldo_chan_id = kSdLdoChannel;
        if (sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &pwr_ctrl_handle) != ESP_OK)
        {
            ESP_LOGE(kTag, "Failed to create SD power control handle");
            return false;
        }
    }
    host.pwr_ctrl_handle = pwr_ctrl_handle;

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 4;
    slot_config.clk = static_cast<gpio_num_t>(sdmmcPins().clk);
    slot_config.cmd = static_cast<gpio_num_t>(sdmmcPins().cmd);
    slot_config.d0 = static_cast<gpio_num_t>(sdmmcPins().d0);
    slot_config.d1 = static_cast<gpio_num_t>(sdmmcPins().d1);
    slot_config.d2 = static_cast<gpio_num_t>(sdmmcPins().d2);
    slot_config.d3 = static_cast<gpio_num_t>(sdmmcPins().d3);
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_vfs_fat_sdmmc_mount_config_t mount_config{};
    mount_config.format_if_mount_failed = false;
    mount_config.max_files = max_files;
    mount_config.allocation_unit_size = 16 * 1024;

    const esp_err_t err =
        esp_vfs_fat_sdmmc_mount(mount_point, &host, &slot_config, &mount_config, &sd_card_);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGW(kTag, "SD mount failed: %s", esp_err_to_name(err));
        return false;
    }

    sd_ready_ = true;
    std::snprintf(sd_mount_point_, sizeof(sd_mount_point_), "%s", mount_point);
    if (sd_card_ != nullptr)
    {
        sdmmc_card_print_info(stdout, sd_card_);
    }
    ESP_LOGI(kTag, "SD mounted at %s", sd_mount_point_);
    return true;
}

bool TDisplayP4Board::sdCardMounted() const
{
    return sd_ready_;
}

sdmmc_card_t* TDisplayP4Board::sdCard() const
{
    return sd_card_;
}

bool TDisplayP4Board::ensureRtcAccessible()
{
    if (rtc_accessible_)
    {
        return true;
    }

    const SystemI2cDeviceConfig config{"rtc-probe", profile().i2c.rtc, 400000};
    ManagedSystemI2cGuard guard(*this, config, kI2cTimeoutMs);
    if (!guard)
    {
        return false;
    }

    const uint8_t reg = 0x00;
    uint8_t value = 0;
    if (guard.transmitReceive(&reg, 1, &value, 1, kI2cTimeoutMs) != ESP_OK)
    {
        return false;
    }

    rtc_accessible_ = true;
    return true;
}

bool TDisplayP4Board::prepareLoraRuntime()
{
    if (!expander_ready_ && !initializeExpander())
    {
        return false;
    }

    const auto& io = ioExpanderPins();
    if (!expanderPinMode(io.lora_dio1, false) ||
        !expanderPinMode(io.lora_rst, true) ||
        !expanderPinMode(io.lora_rf_switch, true))
    {
        return false;
    }

    // The vendor reference routes the shared RF path by driving SKY13453 high.
    return expanderWrite(io.lora_rf_switch, true);
}

bool TDisplayP4Board::setLoraResetAsserted(bool asserted)
{
    if (!prepareLoraRuntime())
    {
        return false;
    }
    return expanderWriteActive(ioExpanderPins().lora_rst,
                               asserted,
                               !profile().lora_reset_active_low);
}

bool TDisplayP4Board::readLoraDio1(bool* out_high) const
{
    return expanderRead(ioExpanderPins().lora_dio1, out_high);
}

bool TDisplayP4Board::setLoraRfSwitchTransmit(bool transmit)
{
    (void)transmit;
    if (!prepareLoraRuntime())
    {
        return false;
    }

    // Toggle the SKY13453 VCTL (XL9535 IO1) per direction: HIGH for TX, LOW for RX.
    // ON-HARDWARE FINDING: the prior "hold VCTL HIGH for both directions" assumption
    // was wrong (it was concluded while RX was confounded by the post-TX self-reception
    // phantom and an unobserved RxDone, so the receiver looked dead either way). With
    // VCTL driven LOW for RX the receive antenna path is materially better connected --
    // the peer's advert comes in ~10 dB stronger (peak RSSI -52 -> -42 dBm at the same
    // bench separation) and the SX1262 modem then receives the peer's frames CLEANLY
    // (GetStats NbPktReceived advances with NbPktCrcError == 0). Driving VCTL HIGH for
    // TX is unchanged (that path always worked). So the SKY13453 is a real TX/RX
    // antenna-path switch on this board and must follow the transmit/receive direction.
    return expanderWrite(ioExpanderPins().lora_rf_switch, transmit);
}

bool TDisplayP4Board::isRadioOnline() const
{
    return ensureRadioReady() && radio().isOnline();
}

int TDisplayP4Board::transmitRadio(const uint8_t* data, size_t len)
{
    if (!ensureRadioReady())
    {
        return -1;
    }
    return radio().startTransmit(data, len);
}

int TDisplayP4Board::startRadioReceive()
{
    if (!ensureRadioReady())
    {
        return -1;
    }
    return radio().startReceive() ? 0 : -1;
}

bool TDisplayP4Board::isRadioChipAlive()
{
    if (!ensureRadioReady())
    {
        return false;
    }
    // RadioLib owns a healthy chip: the hand-rolled "dies in sustained RX" quirk
    // was an artifact of the old command layer and does NOT occur with RadioLib, so
    // isChipResponsive() reports the online flag WITHOUT an SPI probe (it never
    // disturbs an in-flight reception). The adapter's idle revive path keyed on this
    // is consequently dormant, which is correct here (no revive is needed).
    return radio().isChipResponsive();
}

int TDisplayP4Board::reviveRadioReceive()
{
    if (!ensureRadioReady())
    {
        return -1;
    }
    return radio().reviveReceive() ? 0 : -1;
}

uint32_t TDisplayP4Board::getRadioIrqFlags()
{
    if (!ensureRadioReady())
    {
        return 0;
    }
    return radio().getIrqFlags();
}

bool TDisplayP4Board::radioIrqLineAsserted(bool* out_asserted)
{
    // RadioLib now owns the SX1262 and the RX path polls the IRQ over SPI
    // (radio.getIrqFlags()), per the proven driver's model -- it does NOT wait on a
    // separate DIO1 edge. DIO1 is on the XL9535 I2C expander and is not used as the
    // RX-completion trigger anymore. Return false so the MeshCore adapter's RX poll
    // always reads the IRQ word over SPI every cycle (read_irq_over_spi = true) and a
    // RadioLib-decoded RxDone is never gated behind an unread expander line.
    (void)out_asserted;
    return false;
}

bool TDisplayP4Board::pollRadioRxLadder(uint32_t irq, RadioRxLadder* out, bool deep)
{
    if (!ensureRadioReady())
    {
        return false;
    }
    platform::esp::idf_common::Sx126xRadio::RxLadderCounts counts;
    if (!radio().pollRxLadder(static_cast<uint16_t>(irq), &counts, deep))
    {
        return false;
    }
    if (out)
    {
        out->preamble = counts.preamble;
        out->header = counts.header;
        out->rxdone = counts.rxdone;
        out->crcerr = counts.crcerr;
        out->mode = counts.mode;
        out->peak_rssi_dbm = counts.peak_rssi_dbm;
        out->dev_errors = counts.dev_errors;
        out->polls = counts.polls;
        out->notrx_polls = counts.notrx_polls;
        out->irq_seen = counts.irq_seen;
    }
    return true;
}

void TDisplayP4Board::logRadioRxDecode()
{
    if (!ensureRadioReady())
    {
        return;
    }
    radio().logRxDecodeDiag();
}

int TDisplayP4Board::getRadioPacketLength(bool update)
{
    if (!ensureRadioReady())
    {
        return -1;
    }
    return radio().getPacketLength(update);
}

int TDisplayP4Board::readRadioData(uint8_t* buf, size_t len)
{
    if (!ensureRadioReady())
    {
        return -1;
    }
    return radio().readPacket(buf, len);
}

bool TDisplayP4Board::isRadioRxPayloadEmpty()
{
    if (!ensureRadioReady())
    {
        return false;
    }
    return radio().isRxPayloadEmpty();
}

bool TDisplayP4Board::pollRadioCleanRxPacket(uint8_t* out_buf, size_t cap, size_t* out_len)
{
    if (out_len)
    {
        *out_len = 0;
    }
    if (!ensureRadioReady())
    {
        return false;
    }
    return radio().pollCleanRxPacket(out_buf, cap, out_len);
}

void TDisplayP4Board::clearRadioIrqFlags(uint32_t flags)
{
    if (!ensureRadioReady())
    {
        return;
    }
    radio().clearIrqFlags(flags);
}

float TDisplayP4Board::getRadioRSSI()
{
    if (!ensureRadioReady())
    {
        return 0.0f;
    }
    return radio().readRssi();
}

float TDisplayP4Board::getRadioSNR()
{
    if (!ensureRadioReady())
    {
        return 0.0f;
    }
    // Real last-packet SNR from RadioLib (was hardwired 0.0f). The adapter feeds
    // this into path-quality scoring / reported node-info SNR.
    return radio().readSnr();
}

void TDisplayP4Board::configureLoraRadio(float freq_mhz,
                                         float bw_khz,
                                         uint8_t sf,
                                         uint8_t cr_denom,
                                         int8_t tx_power,
                                         uint16_t preamble_len,
                                         uint8_t sync_word,
                                         uint8_t crc_len)
{
    if (!ensureRadioReady())
    {
        return;
    }

    (void)radio().configureLoRaReceive(freq_mhz,
                                       bw_khz,
                                       sf,
                                       cr_denom,
                                       tx_power,
                                       preamble_len,
                                       sync_word,
                                       crc_len);
}

bool TDisplayP4Board::initializeI2cBuses()
{
    if (system_i2c_handle_ != nullptr && external_i2c_handle_ != nullptr)
    {
        return true;
    }

    if (system_i2c_handle_ == nullptr)
    {
        i2c_master_bus_config_t sys_cfg = {};
        sys_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
        sys_cfg.sda_io_num = static_cast<gpio_num_t>(systemI2c().sda);
        sys_cfg.scl_io_num = static_cast<gpio_num_t>(systemI2c().scl);
        sys_cfg.i2c_port = systemI2c().port;
        sys_cfg.flags.enable_internal_pullup = false;
        if (i2c_new_master_bus(&sys_cfg, &system_i2c_handle_) != ESP_OK)
        {
            ESP_LOGE(kTag, "Failed to initialize SYS I2C bus");
            return false;
        }
    }

    if (external_i2c_handle_ == nullptr)
    {
        i2c_master_bus_config_t ext_cfg = {};
        ext_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
        ext_cfg.sda_io_num = static_cast<gpio_num_t>(externalI2c().sda);
        ext_cfg.scl_io_num = static_cast<gpio_num_t>(externalI2c().scl);
        ext_cfg.i2c_port = externalI2c().port;
        ext_cfg.flags.enable_internal_pullup = false;
        if (i2c_new_master_bus(&ext_cfg, &external_i2c_handle_) != ESP_OK)
        {
            ESP_LOGE(kTag, "Failed to initialize EXT I2C bus");
            return false;
        }
    }

    return true;
}

bool TDisplayP4Board::initializeExpander()
{
    if (expander_ready_)
    {
        return true;
    }
    if (!initializeI2cBuses())
    {
        return false;
    }

    const SystemI2cDeviceConfig config{"xl9535", profile().i2c.io_expander, 400000};
    ManagedSystemI2cGuard guard(*this, config, kI2cTimeoutMs);
    if (!guard)
    {
        ESP_LOGE(kTag, "Failed to access XL9535 expander");
        return false;
    }

    auto read_reg = [&](uint8_t reg, uint8_t* out) -> bool
    {
        return guard.transmitReceive(&reg, 1, out, 1, kI2cTimeoutMs) == ESP_OK;
    };
    auto write_reg = [&](uint8_t reg, uint8_t value) -> bool
    {
        const uint8_t payload[2] = {reg, value};
        return guard.transmit(payload, sizeof(payload), kI2cTimeoutMs) == ESP_OK;
    };

    if (!write_reg(kExpanderRegPolarity0, 0x00) || !write_reg(kExpanderRegPolarity1, 0x00))
    {
        ESP_LOGE(kTag, "Failed to configure XL9535 polarity registers");
        return false;
    }

    if (!read_reg(kExpanderRegOutput0, &expander_output_port0_))
    {
        expander_output_port0_ = 0x00;
    }
    if (!read_reg(kExpanderRegOutput1, &expander_output_port1_))
    {
        expander_output_port1_ = 0x00;
    }
    if (!read_reg(kExpanderRegConfig0, &expander_config_port0_))
    {
        expander_config_port0_ = 0xFF;
    }
    if (!read_reg(kExpanderRegConfig1, &expander_config_port1_))
    {
        expander_config_port1_ = 0xFF;
    }

    expander_ready_ = true;
    ESP_LOGI(kTag,
             "XL9535 ready out=(0x%02X,0x%02X) cfg=(0x%02X,0x%02X)",
             expander_output_port0_,
             expander_output_port1_,
             expander_config_port0_,
             expander_config_port1_);
    return true;
}

bool TDisplayP4Board::runColdBootPowerSequence()
{
    const auto& io = ioExpanderPins();
    const auto& p = profile();

    if (!expanderPinMode(io.screen_rst, true) ||
        !expanderPinMode(io.touch_rst, true) ||
        !expanderPinMode(io.touch_int, false) ||
        !expanderPinMode(io.p4_vcca, true) ||
        !expanderPinMode(io.power_5v, true) ||
        !expanderPinMode(io.power_3v3, true) ||
        !expanderPinMode(io.gps_wake, true) ||
        !expanderPinMode(io.c6_enable, true))
    {
        return false;
    }

    (void)expanderWriteActive(io.screen_rst, true, !p.screen_reset_active_low);
    (void)expanderWriteActive(io.touch_rst, true, !p.touch_reset_active_low);
    (void)expanderWriteActive(io.gps_wake, true, p.gps_wake_active_high);  // keep GPS AWAKE from power-up; cold-boot previously drove it to SLEEP (unlike LilyGo), and the module did not reliably wake from the later HIGH
    (void)expanderWriteActive(io.c6_enable, false, p.c6_enable_active_high);
    (void)expanderWriteActive(io.p4_vcca, true, p.p4_vcca_active_high);

    (void)expanderWriteActive(io.power_5v, true, p.power_5v_active_high);
    (void)expanderWriteActive(io.power_3v3, true, p.power_3v3_active_high);
    vTaskDelay(pdMS_TO_TICKS(200));
    (void)expanderWriteActive(io.power_5v, false, p.power_5v_active_high);
    (void)expanderWriteActive(io.power_3v3, false, p.power_3v3_active_high);
    vTaskDelay(pdMS_TO_TICKS(200));
    (void)expanderWriteActive(io.power_5v, true, p.power_5v_active_high);
    (void)expanderWriteActive(io.power_3v3, true, p.power_3v3_active_high);
    vTaskDelay(pdMS_TO_TICKS(200));

    (void)expanderWriteActive(io.screen_rst, false, !p.screen_reset_active_low);
    (void)expanderWriteActive(io.touch_rst, false, !p.touch_reset_active_low);
    vTaskDelay(pdMS_TO_TICKS(200));
    (void)expanderWriteActive(io.screen_rst, true, !p.screen_reset_active_low);
    (void)expanderWriteActive(io.touch_rst, true, !p.touch_reset_active_low);
    vTaskDelay(pdMS_TO_TICKS(200));
    (void)expanderWriteActive(io.screen_rst, false, !p.screen_reset_active_low);
    (void)expanderWriteActive(io.touch_rst, false, !p.touch_reset_active_low);
    vTaskDelay(pdMS_TO_TICKS(200));

    (void)expanderPinMode(io.ethernet_rst, true);
    (void)expanderWrite(io.ethernet_rst, true);
    return true;
}

bool TDisplayP4Board::initializeBatteryGauge()
{
    if (battery_gauge_ready_)
    {
        return true;
    }

    // Throttle re-probing of an absent/failed gauge: the four battery accessors each
    // land here while !ready_, and the Power app polls all four every ~2s. Without a
    // gate that is a lock+I2C probe every call, contending with the touch hot path on
    // system_i2c_mutex_. After a failed probe, return the cached false fast until the
    // cooldown elapses. Same ms clock the rest of the codebase uses (esp_timer).
    const uint32_t now_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
    if (last_gauge_probe_ms_ != 0 &&
        (now_ms - last_gauge_probe_ms_) < kBatteryGaugeProbeCooldownMs)
    {
        return false;
    }
    last_gauge_probe_ms_ = now_ms;

    uint16_t voltage_mv = 0;
    if (!readBatteryGaugeWord(kBatteryRegVoltage, &voltage_mv))
    {
        ESP_LOGW(kTag, "Battery gauge probe failed");
        return false;
    }

    battery_gauge_ready_ = true;
    ESP_LOGI(kTag, "Battery gauge ready voltage=%umV", static_cast<unsigned>(voltage_mv));
    return true;
}

bool TDisplayP4Board::configureGpsUart(uint32_t baud_rate)
{
    if (gps_uart_configured_)
    {
        return true;
    }

    const auto& uart = gpsUart();
    if (uart.port < 0 || uart.tx < 0 || uart.rx < 0 || baud_rate == 0)
    {
        return false;
    }

    uart_config_t config = {};
    config.baud_rate = static_cast<int>(baud_rate);
    config.data_bits = UART_DATA_8_BITS;
    config.parity = UART_PARITY_DISABLE;
    config.stop_bits = UART_STOP_BITS_1;
    config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    config.source_clk = UART_SCLK_DEFAULT;

    const uart_port_t port = static_cast<uart_port_t>(uart.port);
    (void)uart_driver_delete(port);
    if (uart_param_config(port, &config) != ESP_OK ||
        uart_set_pin(port, uart.tx, uart.rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK ||
        uart_driver_install(port, 4096, 0, 0, nullptr, 0) != ESP_OK)
    {
        ESP_LOGE(kTag, "Failed to configure GNSS UART");
        return false;
    }

    gps_uart_configured_ = true;
    ESP_LOGI(kTag,
             "GNSS UART configured port=%d tx=%d rx=%d baud=%lu",
             uart.port,
             uart.tx,
             uart.rx,
             static_cast<unsigned long>(baud_rate));
    return true;
}

void TDisplayP4Board::teardownGpsUart()
{
    if (!gps_uart_configured_)
    {
        return;
    }

    if (gpsUart().port >= 0)
    {
        (void)uart_driver_delete(static_cast<uart_port_t>(gpsUart().port));
    }
    gps_uart_configured_ = false;
}

bool TDisplayP4Board::readBatteryGaugeWord(uint8_t reg, uint16_t* out_word) const
{
    if (out_word == nullptr)
    {
        return false;
    }

    const SystemI2cDeviceConfig config{"battery", profile().i2c.battery_gauge, 400000};
    ManagedSystemI2cGuard guard(const_cast<TDisplayP4Board&>(*this), config, kI2cTimeoutMs);
    if (!guard)
    {
        return false;
    }

    uint8_t buffer[2] = {};
    if (guard.transmitReceive(&reg, 1, buffer, sizeof(buffer), kI2cTimeoutMs) != ESP_OK)
    {
        return false;
    }

    *out_word = static_cast<uint16_t>(buffer[0] | (static_cast<uint16_t>(buffer[1]) << 8));
    return true;
}

bool TDisplayP4Board::readBatteryGaugeWordSigned(uint8_t reg, int16_t* out_word) const
{
    uint16_t raw = 0;
    if (out_word == nullptr || !readBatteryGaugeWord(reg, &raw))
    {
        return false;
    }
    *out_word = static_cast<int16_t>(raw);
    return true;
}

bool TDisplayP4Board::loraReady() const
{
    return expander_ready_;
}

bool TDisplayP4Board::ensureRadioReady() const
{
    static bool acquired = false;
    if (!acquired)
    {
        acquired = radio().acquire();
        ESP_LOGI(kTag, "SX1262 radio acquire=%d", acquired ? 1 : 0);
    }
    return acquired;
}

void TDisplayP4Board::copyOwnerTag(const char* src, char* dst, size_t dst_len)
{
    if (dst == nullptr || dst_len == 0)
    {
        return;
    }
    if (src == nullptr)
    {
        dst[0] = '\0';
        return;
    }

    const size_t copy_len = std::min(std::strlen(src), dst_len - 1);
    std::memcpy(dst, src, copy_len);
    dst[copy_len] = '\0';
}

TDisplayP4Board::ManagedI2cSlot* TDisplayP4Board::findManagedI2cSlot(uint16_t address,
                                                                     uint32_t speed_hz)
{
    for (auto& slot : managed_system_i2c_)
    {
        if (slot.active && slot.address == address && slot.speed_hz == speed_hz && slot.handle != nullptr)
        {
            return &slot;
        }
    }
    return nullptr;
}

const TDisplayP4Board::ManagedI2cSlot* TDisplayP4Board::findManagedI2cSlot(uint16_t address,
                                                                           uint32_t speed_hz) const
{
    for (const auto& slot : managed_system_i2c_)
    {
        if (slot.active && slot.address == address && slot.speed_hz == speed_hz && slot.handle != nullptr)
        {
            return &slot;
        }
    }
    return nullptr;
}

TDisplayP4Board::ManagedI2cSlot* TDisplayP4Board::findFreeManagedI2cSlot()
{
    for (auto& slot : managed_system_i2c_)
    {
        if (!slot.active)
        {
            return &slot;
        }
    }
    return nullptr;
}

} // namespace boards::t_display_p4

BoardBase& board = ::boards::t_display_p4::TDisplayP4Board::instance();
