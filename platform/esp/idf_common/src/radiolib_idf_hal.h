#pragma once

// ESP-IDF RadioLibHal: the platform glue that lets the vendored RadioLib SX1262
// driver own the SX1262 over the ESP32-P4's SPI master + a couple of GPIOs. It
// implements the 15 pure-virtual methods of RadioLib's RadioLibHal (src/Hal.h).
//
// Pin / bus facts for the LilyGo T-Display-P4 (target tdisplayp4_tft):
//   SPI  : SPI2_HOST, sck=GPIO2, miso=GPIO4, mosi=GPIO3, 4 MHz, mode 0.
//   NSS  : GPIO24, driven by RadioLib via digitalWrite (spics_io_num = -1).
//   BUSY : GPIO6 (input, pulldown -- matches the proven vendor config).
//   DIO1 : XL9535 IO17 (I2C expander, NOT a GPIO) -> attach/detachInterrupt are
//          NO-OPs; the RX path polls getIrqFlags() over SPI instead.
//   RESET / RF-switch : XL9535 IO16 / IO1 -> driven by the BOARD's expander
//          methods, NOT this HAL (RadioLib is constructed with rst=RADIOLIB_NC).
//
// The shipped examples/NonArduino/ESP-IDF EspHal bit-bangs SPI on the ESP32-classic
// SPI registers, which do not exist on the ESP32-P4. This HAL therefore drives the
// real ESP-IDF SPI master driver for the 5 SPI methods.

#include <cstddef>
#include <cstdint>

#include "driver/spi_master.h"

#include "Hal.h"

namespace platform::esp::idf_common
{

class RadioLibIdfHal : public RadioLibHal
{
  public:
    // host: a spi_host_device_t value (e.g. SPI2_HOST). sck/miso/mosi: ESP GPIO
    // numbers for the SPI bus. spi_hz: SPI clock. spi_mode: 0..3.
    RadioLibIdfHal(int host, int sck, int miso, int mosi, int spi_hz, int spi_mode);
    ~RadioLibIdfHal() override = default;

    // --- GPIO (gpio_* driver). RADIOLIB_NC pins are silently ignored so the
    //     SX126x driver's internal reset/IRQ pin handling (rst/irq = RADIOLIB_NC)
    //     is a no-op on those lines. ---
    void pinMode(uint32_t pin, uint32_t mode) override;
    void digitalWrite(uint32_t pin, uint32_t value) override;
    uint32_t digitalRead(uint32_t pin) override;

    // --- Interrupts: NO-OPs. DIO1 is on the I2C expander; we poll. ---
    void attachInterrupt(uint32_t interruptNum, void (*interruptCb)(void), uint32_t mode) override;
    void detachInterrupt(uint32_t interruptNum) override;

    // --- Timing (esp_timer / vTaskDelay). ---
    void delay(RadioLibTime_t ms) override;
    void delayMicroseconds(RadioLibTime_t us) override;
    RadioLibTime_t millis() override;
    RadioLibTime_t micros() override;
    long pulseIn(uint32_t pin, uint32_t state, RadioLibTime_t timeout) override;

    // --- SPI (ESP-IDF SPI master). ---
    void spiBegin() override;
    void spiBeginTransaction() override;
    void spiTransfer(uint8_t* out, size_t len, uint8_t* in) override;
    void spiEndTransaction() override;
    void spiEnd() override;

  private:
    int host_;
    int sck_;
    int miso_;
    int mosi_;
    int spi_hz_;
    int spi_mode_;
    spi_device_handle_t spi_dev_ = nullptr;
    bool bus_initialized_ = false;
};

} // namespace platform::esp::idf_common
