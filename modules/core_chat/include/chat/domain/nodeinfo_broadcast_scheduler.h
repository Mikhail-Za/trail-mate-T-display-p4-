/**
 * @file nodeinfo_broadcast_scheduler.h
 * @brief Pure timing policy for periodically re-announcing our own NodeInfo.
 *
 * Meshtastic announces a node's NodeInfo (long/short name, hw model) once at
 * boot. To stay discoverable, a node should also RE-announce itself on a cadence
 * and on demand (the manual "broadcast now" button). This class owns ONLY that
 * decision -- WHEN a NodeInfo broadcast is due -- with no knowledge of the radio,
 * the packet, or the platform clock; the caller feeds it a millis() timestamp and
 * acts on the answer, then reports the send back via markSent().
 *
 * Behaviour:
 *   - configure(interval_ms): set the periodic cadence in ms. 0 disables the
 *     periodic re-announce (initial boot announce + manual requestNow() only).
 *   - due(now): true when a broadcast should go out now -- (a) nothing has been
 *     sent yet (boot announce), or (b) a manual requestNow() is pending, or
 *     (c) interval_ms > 0 and at least interval_ms have elapsed since the last
 *     markSent. It is const and never mutates state, so it can be polled freely.
 *   - markSent(now): record a successful broadcast at `now` -- re-arms the
 *     interval from `now` and clears any pending manual request.
 *   - requestNow(): force the next due() to return true regardless of cadence.
 *
 * Wraparound: elapsed time is computed as the UNSIGNED 32-bit difference
 * (now - last_sent_). uint32_t modular arithmetic makes this the true elapsed ms
 * across a 32-bit millis() rollover (~49.7 days), so the wrap never causes a
 * spurious early or skipped broadcast. (A signed difference would go negative
 * across the wrap and answer wrong.)
 *
 * Header-only, all methods inline, plain C++ over only <cstdint>: no ESP-IDF,
 * Arduino, or LVGL dependency, so it host-compiles standalone for the behavioural
 * test AND links into the firmware unchanged.
 */

#pragma once

#include <cstdint>

namespace chat
{

/**
 * @brief Decides when to (re)broadcast our own Meshtastic NodeInfo.
 *
 * Pure timing logic; not thread-safe (intended for the single mesh task loop).
 */
class NodeInfoBroadcastScheduler
{
public:
    /**
     * @brief Set the periodic re-announce interval.
     * @param interval_ms Milliseconds between automatic re-announces.
     *                    0 disables the periodic cadence (boot + manual only).
     *
     * Does not itself trigger or suppress a pending boot/manual broadcast; it
     * only governs the automatic cadence used by due().
     */
    void configure(uint32_t interval_ms)
    {
        interval_ms_ = interval_ms;
    }

    /**
     * @brief Whether a NodeInfo broadcast should be sent at time @p now_ms.
     * @param now_ms Current monotonic millis() timestamp.
     * @return true if due. const: never mutates scheduler state.
     *
     * Due when: nothing has ever been sent (initial boot announce), OR a manual
     * requestNow() is pending, OR the cadence is enabled (interval_ms_ > 0) and
     * at least interval_ms_ have elapsed since the last markSent.
     */
    bool due(uint32_t now_ms) const
    {
        // Initial boot announce: due until the first successful send.
        if (!has_sent_)
        {
            return true;
        }
        // Manual "broadcast now" button: forced regardless of cadence.
        if (manual_pending_)
        {
            return true;
        }
        // Periodic cadence (disabled when interval_ms_ == 0). Unsigned 32-bit
        // subtraction is wraparound-safe: it yields the true elapsed ms even
        // across a millis() rollover.
        if (interval_ms_ != 0)
        {
            const uint32_t elapsed = now_ms - last_sent_ms_;
            if (elapsed >= interval_ms_)
            {
                return true;
            }
        }
        return false;
    }

    /**
     * @brief Record a successful broadcast at @p now_ms.
     * @param now_ms Timestamp of the broadcast that was just sent.
     *
     * Re-arms the periodic interval from @p now_ms and clears any pending manual
     * request, so the next periodic broadcast is due interval_ms_ later.
     */
    void markSent(uint32_t now_ms)
    {
        last_sent_ms_  = now_ms;
        has_sent_      = true;
        manual_pending_ = false;
    }

    /**
     * @brief Request an immediate broadcast (the manual "broadcast now" button).
     *
     * Makes the next due() return true regardless of the cadence; the request is
     * cleared by the next markSent().
     */
    void requestNow()
    {
        manual_pending_ = true;
    }

private:
    uint32_t interval_ms_   = 0;     ///< Periodic cadence in ms (0 = disabled).
    uint32_t last_sent_ms_  = 0;     ///< millis() of the last markSent.
    bool     has_sent_      = false; ///< False until the first broadcast (boot announce).
    bool     manual_pending_ = false; ///< A requestNow() awaiting its send.
};

} // namespace chat
