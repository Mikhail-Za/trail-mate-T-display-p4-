/**
 * @file idf_chat_factory.h
 * @brief Portable chat / contact / mesh service factory for the minimal
 *        ESP-IDF LoRa-chat facade.
 *
 * Extracted from the Arduino app_context_platform_bindings.cpp chat/contact
 * factory (the create_chat_services / create_contact_services / create_mesh_backend
 * trio, ~132-214), with all Serial logging and team/GPS coupling removed.
 *
 * The IDF chat facade owns the bundles produced here.  Unlike the Arduino path
 * (which wires a chat::MeshAdapterRouter fed by a background FreeRTOS radio task
 * via a RadioPacket queue), this factory uses a self-contained
 * platform::esp::radio::MeshtasticRadioAdapter built directly from the board's
 * LoraBoard as the mesh runtime itself.  That adapter drives the SX126x
 * synchronously inside processSendQueue(), so the facade pumps it inline every
 * tick instead of from a background task.
 */

#pragma once

#include <memory>

#include "app/app_config.h"
#include "app/app_context_platform_bindings.h" // app::ChatServicesBundle / app::ContactServicesBundle
#include "chat/ports/i_mesh_adapter.h"
#include "platform/esp/radio/meshtastic_radio_adapter.h"

class LoraBoard;

namespace platform::esp::idf_common
{

/**
 * @brief Everything the IDF chat facade needs to run on-device LoRa chat.
 *
 * Ownership: the facade moves this whole struct into itself and keeps it alive
 * for the process lifetime.
 *
 * The concrete radio adapter is owned by `chat.mesh_runtime` (a unique_ptr<IMeshAdapter>
 * that points at the protocol adapter selected by config.mesh_protocol -- a
 * platform::esp::radio::MeshtasticRadioAdapter for Meshtastic or a
 * chat::meshcore::MeshCoreAdapter for MeshCore; the radio IS the runtime, no
 * router wrapper).  `mesh_adapter` is a non-owning borrow of that exact object,
 * held through the shared chat::IMeshAdapter seam (both adapters implement it and
 * take the same LoraBoard&), exposed so the facade can satisfy getMeshAdapter()
 * and pump the radio without a downcast.  It stays valid for as long as
 * `chat.mesh_runtime` does.
 */
struct IdfChatRuntime
{
    app::ContactServicesBundle contacts;
    app::ChatServicesBundle chat;
    chat::IMeshAdapter* mesh_adapter = nullptr; // borrow of chat.mesh_runtime (Meshtastic or MeshCore)

    bool isValid() const
    {
        return contacts.isValid() && chat.isValid() && mesh_adapter != nullptr;
    }
};

/**
 * @brief Build the contact service bundle (node store + contact store + service).
 *
 * Portable equivalent of the Arduino create_contact_services(), minus logging.
 */
app::ContactServicesBundle createIdfContactServices();

/**
 * @brief Build the chat service bundle around a MeshtasticRadioAdapter created
 *        from the board's LoRa radio.
 *
 * @param config Active application config (provides mesh protocol + chat policy +
 *               active mesh config to apply to the radio).
 * @param lora_board The board's LoRa radio contract (must outlive the runtime).
 */
IdfChatRuntime createIdfChatRuntime(const app::AppConfig& config, LoraBoard& lora_board);

} // namespace platform::esp::idf_common
