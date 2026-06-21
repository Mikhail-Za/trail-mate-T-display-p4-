#include "radiolib_idf_hal.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "TypeDef.h" // RADIOLIB_NC

namespace platform::esp::idf_common
{
namespace
{
constexpr const char* kTag = "idf-rlhal";

inline bool is_real_pin(uint32_t pin)
{
    // RADIOLIB_NC (0xFFFFFFFF) marks an unconnected pin; ignore GPIO ops on it.
    return pin != RADIOLIB_NC && pin < static_cast<uint32_t>(GPIO_NUM_MAX);
}
} // namespace

RadioLibIdfHal::RadioLibIdfHal(int host, int sck, int miso, int mosi, int spi_hz, int spi_mode)
    : RadioLibHal(GPIO_MODE_INPUT,
                  GPIO_MODE_OUTPUT,
                  /*low*/ 0,
                  /*high*/ 1,
                  GPIO_INTR_POSEDGE,
                  GPIO_INTR_NEGEDGE),
      host_(host),
      sck_(sck),
      miso_(miso),
      mosi_(mosi),
      spi_hz_(spi_hz),
      spi_mode_(spi_mode)
{
}

void RadioLibIdfHal::pinMode(uint32_t pin, uint32_t mode)
{
    if (!is_real_pin(pin))
    {
        return;
    }
    gpio_config_t cfg = {};
    cfg.pin_bit_mask = 1ULL << pin;
    cfg.mode = static_cast<gpio_mode_t>(mode);
    cfg.pull_up_en = GPIO_PULLUP_DISABLE;
    // BUSY (GPIO6) needs a pulldown so it reads a clean low when the SX1262
    // releases it (matches the proven LilyGo cpp_bus_driver / Meshtastic config:
    // a floating BUSY can latch high and wedge the command handshake).
    cfg.pull_down_en = (mode == GPIO_MODE_INPUT) ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE;
    cfg.intr_type = GPIO_INTR_DISABLE;
    gpio_config(&cfg);
}

void RadioLibIdfHal::digitalWrite(uint32_t pin, uint32_t value)
{
    if (!is_real_pin(pin))
    {
        return;
    }
    gpio_set_level(static_cast<gpio_num_t>(pin), value ? 1 : 0);
}

uint32_t RadioLibIdfHal::digitalRead(uint32_t pin)
{
    if (!is_real_pin(pin))
    {
        return 0;
    }
    return static_cast<uint32_t>(gpio_get_level(static_cast<gpio_num_t>(pin)));
}

void RadioLibIdfHal::attachInterrupt(uint32_t interruptNum, void (*interruptCb)(void), uint32_t mode)
{
    // DIO1 is on the XL9535 I2C expander, not a native GPIO -- no edge interrupt is
    // available. The RX path polls getIrqFlags() over SPI instead, so this is a no-op.
    (void)interruptNum;
    (void)interruptCb;
    (void)mode;
}

void RadioLibIdfHal::detachInterrupt(uint32_t interruptNum)
{
    (void)interruptNum;
}

void RadioLibIdfHal::delay(RadioLibTime_t ms)
{
    if (ms == 0)
    {
        return;
    }
    // Sub-tick waits would round to 0 ticks with vTaskDelay; busy-wait those so a
    // 1 ms RadioLib delay actually waits ~1 ms.
    const TickType_t ticks = pdMS_TO_TICKS(ms);
    if (ticks == 0)
    {
        esp_rom_delay_us(static_cast<uint32_t>(ms) * 1000U);
        return;
    }
    vTaskDelay(ticks);
}

void RadioLibIdfHal::delayMicroseconds(RadioLibTime_t us)
{
    if (us == 0)
    {
        return;
    }
    esp_rom_delay_us(static_cast<uint32_t>(us));
}

RadioLibTime_t RadioLibIdfHal::millis()
{
    return static_cast<RadioLibTime_t>(esp_timer_get_time() / 1000LL);
}

RadioLibTime_t RadioLibIdfHal::micros()
{
    return static_cast<RadioLibTime_t>(esp_timer_get_time());
}

long RadioLibIdfHal::pulseIn(uint32_t pin, uint32_t state, RadioLibTime_t timeout)
{
    if (!is_real_pin(pin))
    {
        return 0;
    }
    const gpio_num_t gp = static_cast<gpio_num_t>(pin);
    const int want = state ? 1 : 0;
    const int64_t start = esp_timer_get_time();
    // wait for the pulse to start
    while (gpio_get_level(gp) != want)
    {
        if ((esp_timer_get_time() - start) > static_cast<int64_t>(timeout))
        {
            return 0;
        }
    }
    const int64_t pulse_start = esp_timer_get_time();
    // wait for the pulse to end
    while (gpio_get_level(gp) == want)
    {
        if ((esp_timer_get_time() - start) > static_cast<int64_t>(timeout))
        {
            return 0;
        }
    }
    return static_cast<long>(esp_timer_get_time() - pulse_start);
}

void RadioLibIdfHal::spiBegin()
{
    // Initialize the SPI bus (idempotent: ESP_ERR_INVALID_STATE means another
    // user already brought the bus up, which is fine -- we still add our device).
    if (!bus_initialized_)
    {
        spi_bus_config_t bus_cfg = {};
        bus_cfg.mosi_io_num = mosi_;
        bus_cfg.miso_io_num = miso_;
        bus_cfg.sclk_io_num = sck_;
        bus_cfg.quadwp_io_num = -1;
        bus_cfg.quadhd_io_num = -1;
        bus_cfg.max_transfer_sz = 4096;
        const esp_err_t err =
            spi_bus_initialize(static_cast<spi_host_device_t>(host_), &bus_cfg, SPI_DMA_CH_AUTO);
        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
        {
            ESP_LOGE(kTag, "spi_bus_initialize failed: %d", static_cast<int>(err));
            return;
        }
        bus_initialized_ = true;
    }

    if (spi_dev_ == nullptr)
    {
        spi_device_interface_config_t dev_cfg = {};
        dev_cfg.clock_speed_hz = spi_hz_;
        dev_cfg.mode = spi_mode_;
        // RadioLib drives NSS itself via digitalWrite(cs, ...): leave CS unmanaged.
        dev_cfg.spics_io_num = -1;
        dev_cfg.queue_size = 1;
        dev_cfg.flags = 0;
        const esp_err_t err =
            spi_bus_add_device(static_cast<spi_host_device_t>(host_), &dev_cfg, &spi_dev_);
        if (err != ESP_OK)
        {
            ESP_LOGE(kTag, "spi_bus_add_device failed: %d", static_cast<int>(err));
            spi_dev_ = nullptr;
        }
    }
}

void RadioLibIdfHal::spiBeginTransaction()
{
    // Nothing to do: clock/mode are fixed in the device config; CS is driven by
    // RadioLib around each Module SPI op.
}

void RadioLibIdfHal::spiTransfer(uint8_t* out, size_t len, uint8_t* in)
{
    if (spi_dev_ == nullptr || len == 0)
    {
        return;
    }
    spi_transaction_t trans = {};
    trans.length = 8 * len; // in bits
    trans.tx_buffer = out;
    trans.rx_buffer = in;
    const esp_err_t err = spi_device_polling_transmit(spi_dev_, &trans);
    if (err != ESP_OK)
    {
        ESP_LOGE(kTag, "spi_device_polling_transmit failed: %d", static_cast<int>(err));
    }
}

void RadioLibIdfHal::spiEndTransaction()
{
    // No-op: see spiBeginTransaction.
}

void RadioLibIdfHal::spiEnd()
{
    if (spi_dev_ != nullptr)
    {
        spi_bus_remove_device(spi_dev_);
        spi_dev_ = nullptr;
    }
    if (bus_initialized_)
    {
        spi_bus_free(static_cast<spi_host_device_t>(host_));
        bus_initialized_ = false;
    }
}

} // namespace platform::esp::idf_common
