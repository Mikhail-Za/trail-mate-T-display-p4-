#include "platform/esp/idf_common/sx126x_radio.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "radiolib_idf_hal.h"

// RadioLib: SX1262 only (non-Arduino, custom HAL). Include the specific module +
// Module, NOT RadioLib.h (which would pull in every excluded driver's header).
#include "Module.h"
#include "TypeDef.h"
#include "modules/SX126x/SX126x_commands.h"
#include "modules/SX126x/SX1262.h"

#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
#include "boards/t_display_p4/t_display_p4_board.h"
#endif
#if defined(TRAIL_MATE_ESP_BOARD_TAB5)
#include "boards/tab5/tab5_board.h"
#endif

namespace platform::esp::idf_common
{
namespace
{

constexpr const char* kTag = "idf-sx126x";

// SX126x IRQ bit positions used by the MeshCore adapter's RX poll. These are the
// raw GetIrqStatus bits; RadioLib::getIrqFlags() returns exactly this word.
constexpr uint16_t kIrqTxDone = RADIOLIB_SX126X_IRQ_TX_DONE;     // 0x0001
constexpr uint16_t kIrqRxDone = RADIOLIB_SX126X_IRQ_RX_DONE;     // 0x0002
constexpr uint16_t kIrqCrcErr = RADIOLIB_SX126X_IRQ_CRC_ERR;     // 0x0040
constexpr uint16_t kIrqHeaderErr = RADIOLIB_SX126X_IRQ_HEADER_ERR; // 0x0020

bool take_mutex(void* raw)
{
    if (raw == nullptr)
    {
        return false;
    }
    return xSemaphoreTake(static_cast<SemaphoreHandle_t>(raw), portMAX_DELAY) == pdTRUE;
}

void give_mutex(void* raw)
{
    if (raw == nullptr)
    {
        return;
    }
    xSemaphoreGive(static_cast<SemaphoreHandle_t>(raw));
}

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

// Bring up the board's LoRa runtime (expander pin modes + default RF-switch state).
bool prepare_board_lora_runtime()
{
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    return ::boards::t_display_p4::TDisplayP4Board::instance().prepareLoraRuntime();
#else
    return true;
#endif
}

// Hardware reset the SX1262 via the XL9535 expander (RESET is IO16, active-low --
// NOT a native GPIO). asserted=true drives RESET low.
bool set_board_lora_reset_asserted(bool asserted)
{
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    return ::boards::t_display_p4::TDisplayP4Board::instance().setLoraResetAsserted(asserted);
#else
    (void)asserted;
    return true;
#endif
}

// Drive the SKY13453 RF switch (XL9535 IO1): HIGH=TX, LOW=RX.
void board_prepare_lora_direction(bool transmit)
{
#if defined(TRAIL_MATE_ESP_BOARD_T_DISPLAY_P4)
    (void)::boards::t_display_p4::TDisplayP4Board::instance().setLoraRfSwitchTransmit(transmit);
#else
    (void)transmit;
#endif
}

// --- RadioLib singletons. The HAL + Module + SX1262 live for the whole program;
//     they are constructed once on first init and never torn down (the radio is a
//     process-lifetime resource). Pointers (not statics-with-ctor) so construction
//     order is explicit and tied to init_locked(). ---
RadioLibIdfHal* g_hal = nullptr;
Module* g_module = nullptr;
SX1262* g_radio = nullptr;

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
    if (g_radio != nullptr)
    {
        board_prepare_lora_direction(false);
        g_radio->standby();
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

    const auto& pins = lora_pins();
    ESP_LOGI(kTag,
             "SX1262(RadioLib) init: host=%d sck=%d miso=%d mosi=%d nss=%d busy=%d",
             pins.spi.host, pins.spi.sck, pins.spi.miso, pins.spi.mosi, pins.nss, pins.busy);

    // Bring up the board's LoRa runtime (expander pin modes + RF-switch default).
    if (!prepare_board_lora_runtime())
    {
        set_error_locked("board LoRa runtime setup failed");
        return false;
    }

    // Construct the RadioLib HAL + Module + SX1262 once.
    if (g_hal == nullptr)
    {
        g_hal = new RadioLibIdfHal(pins.spi.host, pins.spi.sck, pins.spi.miso, pins.spi.mosi,
                                   /*spi_hz*/ 4000000, /*spi_mode*/ 0);
    }
    if (g_module == nullptr)
    {
        // cs=NSS(GPIO24, HAL-driven), irq=RADIOLIB_NC (DIO1 on the I2C expander ->
        // polled over SPI), rst=RADIOLIB_NC (RESET on the expander -> we drive it
        // before begin), gpio=BUSY(GPIO6).
        g_module = new Module(g_hal,
                              static_cast<uint32_t>(pins.nss),
                              RADIOLIB_NC,
                              RADIOLIB_NC,
                              static_cast<uint32_t>(pins.busy));
    }
    if (g_radio == nullptr)
    {
        g_radio = new SX1262(g_module);
    }

    // Initialize the SPI bus + device before any chip access (RadioLib also calls
    // hal->init()/spiBegin() inside begin(), but doing it here makes the explicit
    // hardware reset's later SPI 'standby' verification work on the first try).
    g_hal->spiBegin();

    // Hardware-reset the SX1262 via the expander BEFORE radio.begin(). The board
    // already powered the rails on cold-boot; a clean reset puts the chip in a known
    // state so RadioLib's findChip() (which issues GET_VERSION over SPI) succeeds.
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
    online_ = true; // confirmed once begin() succeeds in configureLoRaReceive()
    return true;
#endif
}

bool Sx126xRadio::begin_lora_locked()
{
    if (g_radio == nullptr)
    {
        set_error_locked("radio not constructed");
        return false;
    }

    // Hardware-reset via the expander before begin so a stale chip state (e.g. left
    // by a previously booted firmware) cannot leak in.
    (void)set_board_lora_reset_asserted(true);
    vTaskDelay(pdMS_TO_TICKS(2));
    (void)set_board_lora_reset_asserted(false);
    vTaskDelay(pdMS_TO_TICKS(10));

    // RF switch to RX while we configure (begin leaves the chip in standby).
    board_prepare_lora_direction(false);

    // LoRa bring-up with the active MeshCore config. RadioLib begin() resets the
    // chip (no-op on rst=NC), sets TCXO on DIO3, programs modulation/packet params,
    // CRC, and -- by default -- DIO2-as-RF-switch ON. On this board DIO2 is NOT
    // broken out (the SKY13453 is the antenna switch, driven by us via IO1), so we
    // turn DIO2-as-RF-switch OFF right after begin to match the proven config.
    const int st = g_radio->begin(freq_mhz_,
                                  lora_bw_khz_,
                                  lora_sf_,
                                  lora_cr_,
                                  lora_sync_word_,
                                  lora_tx_power_,
                                  lora_preamble_,
                                  /*tcxoVoltage*/ 1.6f);
    printf("idf-mc: radiolib begin state=%d\n", st);
    if (st != RADIOLIB_ERR_NONE)
    {
        set_error_locked("radiolib begin failed");
        online_ = false;
        return false;
    }

    // DIO2-as-RF-switch OFF (this board steers the antenna via the XL9535 IO1
    // SKY13453, not via the SX1262 DIO2 line).
    (void)g_radio->setDio2AsRfSwitch(false);
    // Explicit CRC on (MeshCore frames carry a LoRa CRC); begin() already sets it,
    // re-assert defensively.
    (void)g_radio->setCRC(2);

    online_ = true;
    lora_configured_ = true;
    ESP_LOGI(kTag,
             "SX1262(RadioLib) begin ok: freq=%.3f bw=%.1f sf=%u cr=%u pwr=%d preamble=%u sync=0x%02X",
             static_cast<double>(freq_mhz_), static_cast<double>(lora_bw_khz_),
             static_cast<unsigned>(lora_sf_), static_cast<unsigned>(lora_cr_),
             static_cast<int>(lora_tx_power_), static_cast<unsigned>(lora_preamble_),
             static_cast<unsigned>(lora_sync_word_));
    return true;
}

bool Sx126xRadio::arm_receive_locked()
{
    if (g_radio == nullptr || !lora_configured_)
    {
        return false;
    }
    // RF switch to RX, then continuous receive (infinite timeout). RadioLib maps the
    // RX IRQs (RxDone/CrcErr/HeaderErr/Timeout) to DIO1 internally; we poll
    // getIrqFlags() over SPI rather than wait on DIO1 (DIO1 is on the I2C expander).
    board_prepare_lora_direction(false);
    const int st = g_radio->startReceive();
    if (st != RADIOLIB_ERR_NONE)
    {
        set_error_locked("radiolib startReceive failed");
        return false;
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
    freq_mhz_ = freq_mhz;
    lora_bw_khz_ = bw_khz;
    lora_sf_ = sf;
    // RadioLib's setCodingRate wants the 4/x denominator (5..8); the adapter passes
    // exactly that (meshcore_cr = 5 -> 4/5).
    lora_cr_ = (cr >= 5 && cr <= 8) ? cr : 5;
    lora_tx_power_ = tx_power;
    lora_preamble_ = preamble_len > 0 ? preamble_len : 16;
    lora_sync_word_ = sync_word;
    lora_crc_len_ = crc_len;

    bool ok = begin_lora_locked();
    if (ok)
    {
        ok = arm_receive_locked();
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
    // MeshCore on this board runs LoRa, not FSK; FSK is unused by the rx path. Keep
    // the seam but bring the chip up in FSK via RadioLib so the API stays honest.
    if (!take_mutex(mutex_))
    {
        return false;
    }
    bool ok = false;
    if (g_radio != nullptr)
    {
        (void)set_board_lora_reset_asserted(true);
        vTaskDelay(pdMS_TO_TICKS(2));
        (void)set_board_lora_reset_asserted(false);
        vTaskDelay(pdMS_TO_TICKS(10));
        board_prepare_lora_direction(false);
        freq_mhz_ = freq_mhz;
        lora_tx_power_ = tx_power;
        const int st = g_radio->beginFSK(freq_mhz, bit_rate_kbps, freq_dev_khz, rx_bw_khz,
                                         tx_power, preamble_len, 1.6f);
        if (st == RADIOLIB_ERR_NONE)
        {
            (void)g_radio->setDio2AsRfSwitch(false);
            if (sync_word != nullptr && sync_word_len > 0)
            {
                (void)g_radio->setSyncWord(const_cast<uint8_t*>(sync_word),
                                           static_cast<uint8_t>(sync_word_len));
            }
            (void)g_radio->setCRC(crc_len > 0 ? 2 : 0);
            online_ = true;
            lora_configured_ = true;
            ok = arm_receive_locked();
        }
        else
        {
            set_error_locked("radiolib beginFSK failed");
        }
    }
    give_mutex(mutex_);
    return ok;
}

bool Sx126xRadio::startReceive()
{
    if (!take_mutex(mutex_))
    {
        return false;
    }
    const bool ok = arm_receive_locked();
    give_mutex(mutex_);
    return ok;
}

bool Sx126xRadio::isChipResponsive()
{
    // RadioLib owns a healthy chip. The hand-rolled "dies in sustained RX" quirk was
    // an artifact of the broken command layer; with RadioLib the revive path is not
    // needed, so report responsive whenever we are online (no SPI probe -> never
    // disturbs an in-flight reception).
    return online_;
}

bool Sx126xRadio::reviveReceive()
{
    // Heavy recovery is unnecessary with RadioLib; just re-arm RX.
    return startReceive();
}

void Sx126xRadio::standby()
{
    if (!take_mutex(mutex_))
    {
        return;
    }
    if (g_radio != nullptr)
    {
        g_radio->standby();
    }
    give_mutex(mutex_);
}

float Sx126xRadio::readRssi()
{
    if (!take_mutex(mutex_))
    {
        return -128.0f;
    }
    float rssi = -128.0f;
    if (g_radio != nullptr)
    {
        // Last-packet RSSI (the adapter calls this right after a reception).
        rssi = g_radio->getRSSI();
    }
    give_mutex(mutex_);
    return rssi;
}

int Sx126xRadio::startTransmit(const uint8_t* data, size_t size)
{
    if (!take_mutex(mutex_))
    {
        return -1;
    }
    if (g_radio == nullptr || data == nullptr || size == 0)
    {
        give_mutex(mutex_);
        return -1;
    }

    // Drive the RF switch HIGH (TX) and transmit via RadioLib. We do NOT use
    // radio.transmit(): it polls the IRQ pin (DIO1) to detect TxDone, but DIO1 is on
    // the I2C expander (irq=RADIOLIB_NC), so digitalRead() never goes high and it
    // would always time out AND clear the IRQ. Instead drive RadioLib's
    // stage/launch directly (startTransmit issues SetTx non-blocking after BUSY
    // settles), then poll TxDone over SPI ourselves and LEAVE it latched so the
    // adapter's TxDone poll ('idf-mc: phy tx complete done=1') sees a real completion.
    board_prepare_lora_direction(true);

    // Known-state for the modem writes + a clean TxDone edge: standby, clear any
    // stale IRQ, then arm + launch the transmit. On this board the SX1262 command
    // processor can WEDGE after sustained continuous RX (SetStandby is rejected with
    // CMD_FAILED even though status reads still respond). A clean hardware reset +
    // re-begin un-wedges it, so if standby fails we recover the chip before TX --
    // mirroring the proven pre-TX re-establish the hand-rolled driver used.
    int sb_st = g_radio->standby();
    if (sb_st != RADIOLIB_ERR_NONE)
    {
        (void)set_board_lora_reset_asserted(true);
        vTaskDelay(pdMS_TO_TICKS(2));
        (void)set_board_lora_reset_asserted(false);
        vTaskDelay(pdMS_TO_TICKS(10));
        board_prepare_lora_direction(true);
        const bool re = begin_lora_locked();
        sb_st = g_radio->standby();
        if (tx_diag_count_ < 4)
        {
            printf("idf-mc: txrecover rebegin=%d standby=%d\n", re ? 1 : 0, sb_st);
        }
    }
    (void)g_radio->clearIrqFlags(RADIOLIB_SX126X_IRQ_ALL);

    const int64_t t0 = esp_timer_get_time();
    const int st = g_radio->startTransmit(data, size);
    bool tx_done = false;
    uint32_t irq = 0;
    if (st == RADIOLIB_ERR_NONE)
    {
        // SF7/BW62.5 advert airtime is a few hundred ms; allow generous margin.
        constexpr int64_t kTxTimeoutUs = 3000000; // 3 s hard cap
        while ((esp_timer_get_time() - t0) < kTxTimeoutUs)
        {
            irq = g_radio->getIrqFlags();
            if ((irq & kIrqTxDone) != 0)
            {
                tx_done = true;
                break;
            }
            if ((irq & RADIOLIB_SX126X_IRQ_TIMEOUT) != 0)
            {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(2));
        }
    }
    const int64_t took_us = esp_timer_get_time() - t0;

    if (tx_diag_count_ < 4)
    {
        ESP_LOGI(kTag, "SX1262(RadioLib) TX[%lu]: start_state=%d txdone=%d took_ms=%lld irq=0x%04X len=%u",
                 static_cast<unsigned long>(tx_diag_count_), st, tx_done ? 1 : 0, took_us / 1000,
                 static_cast<unsigned>(irq), static_cast<unsigned>(size));
        ++tx_diag_count_;
    }

    // Return the chip to standby (it auto-falls there after TxDone anyway). The
    // TxDone IRQ is intentionally LEFT LATCHED for the adapter to observe; the
    // adapter clears it and then calls startRadioReceive() to re-arm RX, so we do NOT
    // re-arm RX here (doing so would not clear TxDone, but leaving the standby keeps
    // the TX->RX hand-off the adapter expects).
    (void)g_radio->standby();

    give_mutex(mutex_);
    return tx_done ? 0 : -1;
}

uint32_t Sx126xRadio::getIrqFlags()
{
    if (!take_mutex(mutex_))
    {
        return 0;
    }
    uint32_t irq = 0;
    if (g_radio != nullptr)
    {
        irq = g_radio->getIrqFlags();
    }
    give_mutex(mutex_);
    return irq;
}

void Sx126xRadio::clearIrqFlags(uint32_t flags)
{
    if (!take_mutex(mutex_))
    {
        return;
    }
    if (g_radio != nullptr)
    {
        (void)g_radio->clearIrqFlags(flags);
    }
    give_mutex(mutex_);
}

int Sx126xRadio::getPacketLength(bool update)
{
    if (!take_mutex(mutex_))
    {
        return -1;
    }
    int len = -1;
    if (g_radio != nullptr)
    {
        len = static_cast<int>(g_radio->getPacketLength(update));
    }
    give_mutex(mutex_);
    return len;
}

int Sx126xRadio::readPacket(uint8_t* buffer, size_t size)
{
    if (!take_mutex(mutex_))
    {
        return -1;
    }
    int rc = -1;
    if (g_radio != nullptr && buffer != nullptr && size > 0)
    {
        // RadioLib readData() reads from the FIFO at the chip's RxStartBufferPointer
        // and returns RADIOLIB_ERR_NONE (0) on a clean read. The adapter gates on
        // '== RADIOLIB_ERR_NONE', so pass the code straight through.
        const int st = g_radio->readData(buffer, size);
        rc = st; // RADIOLIB_ERR_NONE == 0
    }
    give_mutex(mutex_);
    return rc;
}

bool Sx126xRadio::isRxPayloadEmpty()
{
    // With RadioLib driving the demod there is no post-TX self-reception phantom to
    // filter: a latched RxDone corresponds to a real decoded frame. Never report the
    // payload as empty, so the adapter never drops a genuine reception.
    return false;
}

bool Sx126xRadio::pollRxLadder(uint16_t irq, RxLadderCounts* out, bool deep)
{
    if (!take_mutex(mutex_))
    {
        return false;
    }
    ++rxladder_polls_;
    const bool dead = (irq == 0x0000u) || (irq == 0xFFFFu);
    if (!dead)
    {
        rxladder_irq_seen_ |= irq;
    }
    // Rising-edge count RxDone / CrcErr from the caller-supplied IRQ word (no extra
    // SPI -> safe mid-reception).
    const bool rxdone_now = !dead && (irq & kIrqRxDone) != 0;
    const bool crcerr_now = !dead && (irq & kIrqCrcErr) != 0;
    if (rxdone_now && !rxladder_prev_rxdone_)
    {
        ++rxladder_rxdone_;
    }
    if (crcerr_now && !rxladder_prev_crcerr_)
    {
        ++rxladder_crcerr_;
    }
    rxladder_prev_rxdone_ = rxdone_now;
    rxladder_prev_crcerr_ = crcerr_now;

    if (out != nullptr)
    {
        out->preamble = 0;
        out->header = 0;
        out->rxdone = rxladder_rxdone_;
        out->crcerr = rxladder_crcerr_;
        out->mode = online_ ? 5u : 0u; // RX mode while online
        out->peak_rssi_dbm = -128.0f;
        out->dev_errors = 0;
        out->polls = rxladder_polls_;
        out->notrx_polls = 0;
        out->irq_seen = rxladder_irq_seen_;
    }
    (void)deep;
    give_mutex(mutex_);
    return online_;
}

bool Sx126xRadio::pollCleanRxPacket(uint8_t* out_buf, size_t cap, size_t* out_len)
{
    // The GetStats clean-reader was a workaround for the broken demod's dead RxDone
    // IRQ. RadioLib raises RxDone normally, so RX delivery flows through the adapter's
    // IRQ path (getIrqFlags -> readPacket). Report "nothing pending" and do no SPI.
    (void)out_buf;
    (void)cap;
    if (out_len != nullptr)
    {
        *out_len = 0;
    }
    return true;
}

void Sx126xRadio::logRxDecodeDiag()
{
    // No-op: RadioLib owns the decode; the legacy register-level localization is not
    // meaningful here.
}

const char* Sx126xRadio::lastError() const
{
    return last_error_;
}

void Sx126xRadio::set_error_locked(const char* error)
{
    if (error == nullptr)
    {
        last_error_[0] = '\0';
        return;
    }
    std::strncpy(last_error_, error, sizeof(last_error_) - 1);
    last_error_[sizeof(last_error_) - 1] = '\0';
    ESP_LOGW(kTag, "%s", last_error_);
}

} // namespace platform::esp::idf_common
