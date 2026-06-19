/**
 * @file idf_app_tasks_radio_compat.cpp
 * @brief No-op IDF definitions for the two app::AppTasks radio-receive hooks the
 *        MeshCore adapter calls on its TX path.
 *
 * The Arduino build owns a background radio FreeRTOS task manager
 * (platform/esp/arduino_common/.../app_tasks.cpp) that the MeshCore adapter pokes
 * via AppTasks::requestRadioReceiveRestart() / setRadioReceiveActive() to re-arm
 * RX after a transmit. The pure ESP-IDF chat path does NOT use that task manager;
 * it pumps the radio inline (the adapter calls board_.startRadioReceive()
 * directly right after transmit, exactly like the IDF Meshtastic adapter), so
 * these two hooks have nothing to do here.
 *
 * app_tasks.cpp itself is Arduino-only and excluded from the IDF source set, so
 * without these definitions the two static methods would be unresolved symbols at
 * link time. Provide them as no-ops. None of the other AppTasks members are
 * referenced by the IDF build, so they are intentionally left undefined.
 */

#include "platform/esp/arduino_common/app_tasks.h"

namespace app
{

void AppTasks::requestRadioReceiveRestart()
{
    // No background radio task in the IDF build; RX is re-armed inline by the
    // adapter after transmit. Nothing to schedule.
}

void AppTasks::setRadioReceiveActive(bool active)
{
    // The IDF build does not track a shared radio-receive flag for a background
    // task; the inline pump is the single owner of RX state.
    (void)active;
}

} // namespace app
