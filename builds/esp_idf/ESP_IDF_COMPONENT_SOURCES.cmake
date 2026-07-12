# Final ESP-IDF component source ownership for migrated surfaces.
#
# The source list owner is this build entrypoint plus the final app, platform,
# and module owners. Do not restore historical app/component roots to include
# this file.

set(TRAILMATE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

set(TRAILMATE_ESP_IDF_APP_SHELL_SOURCES
    "${TRAILMATE_ROOT}/apps/esp32_lvgl/src/esp32_lvgl_idf_app_registry.cpp"
    "${TRAILMATE_ROOT}/apps/esp32_lvgl/src/esp32_lvgl_translate_app.cpp"
    "${TRAILMATE_ROOT}/apps/esp32_lvgl/src/esp32_lvgl_field_guide_app.cpp"
    "${TRAILMATE_ROOT}/apps/esp32_lvgl/src/esp32_lvgl_waypoints_app.cpp"
    "${TRAILMATE_ROOT}/apps/esp32_lvgl/src/esp32_lvgl_compass_app.cpp"
    "${TRAILMATE_ROOT}/apps/esp32_lvgl/src/esp32_lvgl_sun_moon_app.cpp"
    "${TRAILMATE_ROOT}/apps/esp32_lvgl/src/esp32_lvgl_beacon_app.cpp"
    "${TRAILMATE_ROOT}/apps/esp32_lvgl/src/esp32_lvgl_trip_computer_app.cpp"
    "${TRAILMATE_ROOT}/apps/esp32_lvgl/src/esp32_lvgl_power_app.cpp"
    "${TRAILMATE_ROOT}/apps/esp32_lvgl/src/esp32_lvgl_idf_app_runtime_access.cpp"
    "${TRAILMATE_ROOT}/apps/esp32_lvgl/src/esp32_lvgl_app_shell.cpp"
    "${TRAILMATE_ROOT}/apps/esp32_lvgl/src/esp32_lvgl_historical_source_descriptor.cpp"
    "${TRAILMATE_ROOT}/apps/esp32_lvgl/src/esp32_lvgl_startup_runtime.cpp"
    "${TRAILMATE_ROOT}/apps/esp32_lvgl/src/esp32_lvgl_loop_runtime.cpp"
    "${TRAILMATE_ROOT}/apps/esp32_lvgl/src/esp32_lvgl_runtime_config.cpp")

set(TRAILMATE_ESP_IDF_PRODUCT_COMPOSITION_SOURCES
    "${TRAILMATE_ROOT}/modules/product_composition/src/target_profile.cpp"
    "${TRAILMATE_ROOT}/modules/product_composition/src/target_build_binding.cpp")

set(TRAILMATE_ESP_IDF_CORE_HOSTLINK_SOURCES
    "${TRAILMATE_ROOT}/modules/core_hostlink/src/c6_frame_codec_c.c"
    "${TRAILMATE_ROOT}/modules/core_hostlink/src/c6_frame_codec.cpp"
    # Host (PC) link codec: the session state machine, the frame
    # encoder/decoder + CRC, the command/handshake router, and the
    # service/event/app-data/config payload builders. These are pure
    # C++/stdlib (no Arduino/ESP deps) and back the PC Link app's USB-CDC
    # bridge (TRAILMATE_ESP_IDF_PC_LINK_UI_SOURCES). c6_frame_codec above is the
    # separate SDIO-to-C6 codec and is unrelated to this host link.
    "${TRAILMATE_ROOT}/modules/core_hostlink/src/hostlink_session.cpp"
    "${TRAILMATE_ROOT}/modules/core_hostlink/src/hostlink_codec.cpp"
    "${TRAILMATE_ROOT}/modules/core_hostlink/src/hostlink_frame_router.cpp"
    "${TRAILMATE_ROOT}/modules/core_hostlink/src/hostlink_service_codec.cpp"
    "${TRAILMATE_ROOT}/modules/core_hostlink/src/hostlink_app_data_codec.cpp"
    "${TRAILMATE_ROOT}/modules/core_hostlink/src/hostlink_config_codec.cpp"
    "${TRAILMATE_ROOT}/modules/core_hostlink/src/hostlink_event_codec.cpp")

set(TRAILMATE_ESP_IDF_CORE_SYS_SOURCES
    "${TRAILMATE_ROOT}/modules/core_sys/src/app/app_facade_access.cpp"
    "${TRAILMATE_ROOT}/modules/core_sys/src/sys/clock.cpp"
    "${TRAILMATE_ROOT}/modules/core_sys/src/platform/ui/timezone_profile.cpp")

# core_team: the chat screen's team-action plumbing (chat_page_runtime.cpp
# unconditionally builds a TeamActionRuntimeSink, and team_runtime_adapters.cpp's
# TeamControllerChatCommandPort statically references TeamController::onChat /
# setKeysFromPsk) drags in the team usecase + protocol codecs at link time even
# though getTeamController() returns nullptr at runtime. These are pure C++/stdlib
# (crypto/runtime/event-sink are injected ITeamCrypto/ITeamRuntime/ITeamEventSink
# ports, never instantiated here), so the closure is the controller+service
# forwarders plus the team_* wire/mgmt/chat/waypoint/position/track codecs.
set(TRAILMATE_ESP_IDF_CORE_TEAM_SOURCES
    "${TRAILMATE_ROOT}/modules/core_team/src/usecase/team_controller.cpp"
    "${TRAILMATE_ROOT}/modules/core_team/src/usecase/team_service.cpp"
    # Phase 2 (LoRa pairing): the shared, cross-platform pairing state machine
    # (3-msg Beacon/Join/Key handshake, nonce binding, retries, timeouts) and the
    # pairing wire codec it encodes/decodes with. Both were ABSENT from the IDF
    # build until now (the Arduino build dragged them in via the ESP-NOW pairing
    # service). The IDF LoRa pairing service/transport in
    # TRAILMATE_ESP_IDF_TEAM_SVC_SOURCES below drive the coordinator over the live
    # chat::IMeshAdapter instead of ESP-NOW. The coordinator is REUSED UNCHANGED.
    "${TRAILMATE_ROOT}/modules/core_team/src/usecase/team_pairing_coordinator.cpp"
    "${TRAILMATE_ROOT}/modules/core_team/src/protocol/team_pairing_wire.cpp"
    "${TRAILMATE_ROOT}/modules/core_team/src/protocol/team_chat.cpp"
    "${TRAILMATE_ROOT}/modules/core_team/src/protocol/team_location_marker.cpp"
    "${TRAILMATE_ROOT}/modules/core_team/src/protocol/team_mgmt.cpp"
    "${TRAILMATE_ROOT}/modules/core_team/src/protocol/team_wire.cpp"
    "${TRAILMATE_ROOT}/modules/core_team/src/protocol/team_waypoint.cpp"
    "${TRAILMATE_ROOT}/modules/core_team/src/protocol/team_position.cpp"
    "${TRAILMATE_ROOT}/modules/core_team/src/protocol/team_track.cpp")

# IDF team-service construction layer (Phase 1: make getTeamController() non-null
# so the Team screen's Create/Join enable). These are the IDF-portable ITeam*
# port implementations the facade injects into TeamService/TeamController/
# TeamTrackSampler -- the IDF analogues of the Arduino team_platform_bundle:
#   - team_runtime_idf.cpp     ITeamRuntime  (sys clock + esp_random)
#   - idf_team_crypto.cpp      ITeamCrypto   (mbedtls ChaCha20-Poly1305 + SHA256,
#                                            wire-compatible with the rweather peers)
#   - idf_team_track_source.cpp ITeamTrackSource (IDF GNSS get_data -> lat/lng*1e7)
#   - idf_team_event_sinks.cpp  ITeamEventSink + ITeamPairingEventSink -> sys::EventBus
# Plus team_track_sampler.cpp (the TeamTrackSampler usecase the facade now
# constructs; it was not previously compiled). mbedtls is already in main's
# REQUIRES; ChaCha20/Poly1305/ChaChaPoly are enabled in the target
# sdkconfig.defaults.
#
# Phase 2 (LoRa pairing) adds the two IDF impls that make getTeamPairing() return
# non-null so the Team screen's Create/Join actually pair two units over LoRa:
#   - idf_lora_pairing_transport.cpp  ITeamPairingTransport over chat::IMeshAdapter
#                                     (synthetic 6-byte MAC <-> 32-bit NodeId bridge;
#                                     broadcast MAC -> sendAppData(TEAM_PAIR_APP,dest=0),
#                                     unicast -> the decoded NodeId; RX is fed by the
#                                     facade pump, the transport owns no radio loop).
#   - idf_lora_pairing_service.cpp    wraps the shared TeamPairingCoordinator (the
#                                     IDF analogue of EspNowTeamPairingService) and
#                                     adds the Option-A passphrase floor: both units
#                                     derive PSK = sha256(passphrase||team_id)[:16]
#                                     LOCALLY, so the PSK never crosses LoRa in the
#                                     clear (ESP-NOW's ~5 m range was the old secrecy
#                                     boundary; LoRa reaches km).
set(TRAILMATE_ESP_IDF_TEAM_SVC_SOURCES
    "${TRAILMATE_ROOT}/modules/core_team/src/usecase/team_track_sampler.cpp"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/team/team_runtime_idf.cpp"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/team/idf_team_crypto.cpp"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/team/idf_team_track_source.cpp"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/team/idf_team_event_sinks.cpp"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/team/idf_lora_pairing_transport.cpp"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/team/idf_lora_pairing_service.cpp"
    # Reboot persistence for the Team feature: a durable NVS-backed
    # ITeamUiSnapshotStore that terminates the existing team-key save/restore seam
    # (team_ui_save_keys_now / team page loadOnce) in flash instead of the default
    # RAM-only TeamUiSnapshotMemoryStore, so a paired team's PSK survives reboot.
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/team/idf_nvs_team_ui_snapshot_store.cpp")

# ---------------------------------------------------------------------------
# Minimal LoRa-chat producer set (consumed by the IDF chat facade/factory in
# TRAILMATE_ESP_IDF_PLATFORM_COMMON_SOURCES). Scope = Meshtastic text chat only:
# chat/contact services + RAM store + the Meshtastic codec the radio adapter
# encodes/decodes with, plus the LVGL chat screen and its portable presentation
# deps. SKIP the meshcore/lxmf/rnode/reticulum protocol families, team/admin,
# map, and the SD/flash chat stores.
# ---------------------------------------------------------------------------

# core_chat domain + use cases + RAM store + contact node-store blob format.
set(TRAILMATE_ESP_IDF_CORE_CHAT_SOURCES
    "${TRAILMATE_ROOT}/modules/core_chat/src/domain/chat_model.cpp"
    "${TRAILMATE_ROOT}/modules/core_chat/src/domain/channel_persist.cpp"
    "${TRAILMATE_ROOT}/modules/core_chat/src/domain/channel_hash.cpp"
    "${TRAILMATE_ROOT}/modules/core_chat/src/domain/channel_key.cpp"
    "${TRAILMATE_ROOT}/modules/core_chat/src/domain/chat_sha256.cpp"
    "${TRAILMATE_ROOT}/modules/core_chat/src/usecase/chat_service.cpp"
    "${TRAILMATE_ROOT}/modules/core_chat/src/usecase/contact_service.cpp"
    "${TRAILMATE_ROOT}/modules/core_chat/src/infra/store/ram_store.cpp"
    "${TRAILMATE_ROOT}/modules/core_chat/src/infra/contact_store_core.cpp"
    "${TRAILMATE_ROOT}/modules/core_chat/src/infra/node_store_core.cpp"
    "${TRAILMATE_ROOT}/modules/core_chat/src/infra/node_store_blob_format.cpp"
    "${TRAILMATE_ROOT}/modules/core_chat/src/infra/mesh_protocol_utils.cpp"
    "${TRAILMATE_ROOT}/modules/core_chat/src/infra/meshcore/mc_region_presets.cpp")

# Meshtastic wire codec used by platform/esp/radio/meshtastic_radio_adapter.cpp.
set(TRAILMATE_ESP_IDF_CORE_CHAT_MESHTASTIC_SOURCES
    "${TRAILMATE_ROOT}/modules/core_chat/src/infra/meshtastic/mt_codec_pb.cpp"
    "${TRAILMATE_ROOT}/modules/core_chat/src/infra/meshtastic/mt_dedup.cpp"
    "${TRAILMATE_ROOT}/modules/core_chat/src/infra/meshtastic/mt_node_payload.cpp"
    "${TRAILMATE_ROOT}/modules/core_chat/src/infra/meshtastic/mt_packet_wire.cpp"
    "${TRAILMATE_ROOT}/modules/core_chat/src/infra/meshtastic/mt_pki_crypto.cpp"
    "${TRAILMATE_ROOT}/modules/core_chat/src/infra/meshtastic/mt_protocol_helpers.cpp"
    "${TRAILMATE_ROOT}/modules/core_chat/src/infra/meshtastic/mt_radio_config.cpp"
    "${TRAILMATE_ROOT}/modules/core_chat/src/infra/meshtastic/mt_region.cpp"
    "${TRAILMATE_ROOT}/modules/core_chat/src/infra/meshtastic/compression/unishox2.cpp")

# nanopb runtime + generated Meshtastic protobuf descriptors (.pb.cpp) the codec
# links against.
set(TRAILMATE_ESP_IDF_CORE_CHAT_NANOPB_SOURCES
    "${TRAILMATE_ROOT}/modules/core_chat/third_party/nanopb/pb_common.c"
    "${TRAILMATE_ROOT}/modules/core_chat/third_party/nanopb/pb_decode.c"
    "${TRAILMATE_ROOT}/modules/core_chat/third_party/nanopb/pb_encode.c")

file(GLOB TRAILMATE_ESP_IDF_CORE_CHAT_GENERATED_SOURCES
    "${TRAILMATE_ROOT}/modules/core_chat/generated/meshtastic/*.pb.cpp")

# ---------------------------------------------------------------------------
# MeshCore protocol family (the open-source MeshCore stack, ported into the IDF
# build alongside the Meshtastic path). Mirrors the meshtastic source groups
# above. Scope = the MeshCore protocol engine, its vendored Ed25519 crypto, the
# core_mesh MeshCore strategy + identity flow, and the platform adapter +
# identity that bind it to the shared LoraBoard / IMeshAdapter seam. The
# symmetric crypto (AES-128 / SHA-256 / HMAC) in meshcore_protocol_helpers.cpp
# routes to mbedtls under #if defined(ESP_PLATFORM), so it builds unmodified;
# mbedtls is already in main's REQUIRES.
#
# core_chat MeshCore helpers. mc_region_presets.cpp is intentionally NOT listed
# here: it is already part of TRAILMATE_ESP_IDF_CORE_CHAT_SOURCES (re-adding it
# would be a duplicate-source error).
set(TRAILMATE_ESP_IDF_CORE_CHAT_MESHCORE_SOURCES
    "${TRAILMATE_ROOT}/modules/core_chat/src/infra/meshcore/meshcore_identity_crypto.cpp"
    "${TRAILMATE_ROOT}/modules/core_chat/src/infra/meshcore/meshcore_payload_helpers.cpp"
    "${TRAILMATE_ROOT}/modules/core_chat/src/infra/meshcore/meshcore_protocol_helpers.cpp")

# Vendored Ed25519 (orlp/ed25519, plain portable C99 with a bundled sha512) used
# for MeshCore identity keygen / sign / verify / ECDH.
set(TRAILMATE_ESP_IDF_CORE_CHAT_MESHCORE_ED25519_SOURCES
    "${TRAILMATE_ROOT}/modules/core_chat/src/infra/meshcore/crypto/ed25519/fe.c"
    "${TRAILMATE_ROOT}/modules/core_chat/src/infra/meshcore/crypto/ed25519/ge.c"
    "${TRAILMATE_ROOT}/modules/core_chat/src/infra/meshcore/crypto/ed25519/key_exchange.c"
    "${TRAILMATE_ROOT}/modules/core_chat/src/infra/meshcore/crypto/ed25519/keypair.c"
    "${TRAILMATE_ROOT}/modules/core_chat/src/infra/meshcore/crypto/ed25519/sc.c"
    "${TRAILMATE_ROOT}/modules/core_chat/src/infra/meshcore/crypto/ed25519/sha512.c"
    "${TRAILMATE_ROOT}/modules/core_chat/src/infra/meshcore/crypto/ed25519/sign.c"
    "${TRAILMATE_ROOT}/modules/core_chat/src/infra/meshcore/crypto/ed25519/verify.c")

# core_mesh MeshCore protocol strategy + identity flow.
set(TRAILMATE_ESP_IDF_CORE_MESH_MESHCORE_SOURCES
    "${TRAILMATE_ROOT}/modules/core_mesh/src/protocol/meshcore/meshcore_protocol_strategy.cpp"
    "${TRAILMATE_ROOT}/modules/core_mesh/src/protocol/meshcore/mc_identity_flow.cpp")

# The platform MeshCore adapter + identity (the IDF-portable forms of the Arduino
# shells: SHA-256 -> mbedtls, millis() -> esp_timer, radio via LoraBoard, NVS via
# the IDF blob store below), plus the IDF-native NVS blob store that backs
# MeshCore identity/peer persistence and the no-op app_tasks radio-receive hooks
# the adapter's TX path references.
set(TRAILMATE_ESP_IDF_PLATFORM_MESHCORE_SOURCES
    "${TRAILMATE_ROOT}/platform/esp/arduino_common/src/chat/infra/meshcore/meshcore_adapter.cpp"
    "${TRAILMATE_ROOT}/platform/esp/arduino_common/src/chat/infra/meshcore/meshcore_identity.cpp"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/idf_blob_store_io.cpp"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/idf_app_tasks_radio_compat.cpp")

# ---------------------------------------------------------------------------
# RadioLib (jgromes, MIT) -- vendored SX1262 driver, built as a NON-Arduino /
# custom-HAL build (ARDUINO is undefined in this pure-IDF build, so RadioLib's
# BuildOpt.h auto-selects the generic path: no <SPI.h>/Arduino.h, ArduinoHal.cpp
# compiles out). The hand-rolled Sx126xRadio demod corrupts the LoRa payload on
# this board (TX 11 00 E2.. decodes to RX 8B 12 E6..), while RadioLib decodes
# cleanly on the SAME hardware under MeshOS -- so the proven RadioLib SX126x
# driver replaces our demod via a small ESP-IDF RadioLibHal. Scope = SX1262 +
# SX126x base + Module + HAL + the PhysicalLayer/CRC/FEC/Cryptography/Utils core
# only; every other module/protocol family is omitted from the file list and
# disabled via RADIOLIB_EXCLUDE_* (set in main/CMakeLists.txt).
set(TRAILMATE_RADIOLIB_SRC
    "${TRAILMATE_ROOT}/platform/esp/idf_common/third_party/RadioLib/src")
set(TRAILMATE_ESP_IDF_RADIOLIB_SOURCES
    "${TRAILMATE_RADIOLIB_SRC}/Module.cpp"
    "${TRAILMATE_RADIOLIB_SRC}/Hal.cpp"
    "${TRAILMATE_RADIOLIB_SRC}/utils/Utils.cpp"
    "${TRAILMATE_RADIOLIB_SRC}/utils/CRC.cpp"
    "${TRAILMATE_RADIOLIB_SRC}/utils/FEC.cpp"
    "${TRAILMATE_RADIOLIB_SRC}/utils/Cryptography.cpp"
    "${TRAILMATE_RADIOLIB_SRC}/protocols/PhysicalLayer/PhysicalLayer.cpp"
    "${TRAILMATE_RADIOLIB_SRC}/modules/SX126x/SX126x.cpp"
    "${TRAILMATE_RADIOLIB_SRC}/modules/SX126x/SX126x_commands.cpp"
    "${TRAILMATE_RADIOLIB_SRC}/modules/SX126x/SX126x_config.cpp"
    "${TRAILMATE_RADIOLIB_SRC}/modules/SX126x/SX126x_LR_FHSS.cpp"
    "${TRAILMATE_RADIOLIB_SRC}/modules/SX126x/SX1262.cpp")

# Arduino-common chat infra the factory instantiates directly: only the
# ChatEventBusBridge observer (pure: EventBus::publish + chat_service.h).
#
# The Arduino contact_store.cpp / meshtastic/node_store.cpp / internal/
# blob_store_io.cpp shells are intentionally EXCLUDED: they are persistence
# shells over NodeStoreCore/ContactStoreCore that hard-include <SPI.h>,
# <Arduino.h>, and <Preferences.h> (via storage/sd_card_runtime.h), none of
# which exist in the pure ESP-IDF build. The minimal RAM-only chat scope drives
# NodeStoreCore/ContactStoreCore directly with in-memory blob stores in
# idf_chat_factory.cpp instead (mirrors platform/linux/.../linux_app_services.cpp).
set(TRAILMATE_ESP_IDF_ARDUINO_CHAT_SOURCES
    "${TRAILMATE_ROOT}/platform/esp/arduino_common/src/chat/infra/chat_event_bus_bridge.cpp"
    "${TRAILMATE_ROOT}/platform/esp/arduino_common/src/sys/event_bus.cpp")

# Phone-session pump: the transport-agnostic Meshtastic/MeshCore ToRadio<->FromRadio
# protocol cores plus the AppPhoneFacade adapter (IAppBleFacade -> IPhoneAppFacade).
# Needed on IDF so the C6 BLE companion can drive a real phone session (Stage 2b).
# AppPhoneFacade lives under arduino_common but is NimBLE-free.
set(TRAILMATE_ESP_IDF_CORE_PHONE_SOURCES
    "${TRAILMATE_ROOT}/modules/core_phone/src/meshtastic/meshtastic_phone_core.cpp"
    "${TRAILMATE_ROOT}/modules/core_phone/src/meshtastic/meshtastic_phone_session.cpp"
    "${TRAILMATE_ROOT}/modules/core_phone/src/meshcore/meshcore_phone_core.cpp"
    "${TRAILMATE_ROOT}/platform/esp/arduino_common/src/ble/app_phone_facade.cpp"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/idf_ble_phone_facade_stubs.cpp")

# Portable presentation/runtime deps of the LVGL chat screen.
set(TRAILMATE_ESP_IDF_CHAT_PRESENTATION_SOURCES
    "${TRAILMATE_ROOT}/modules/ui_presentation/src/chat/chat_workspace_model.cpp"
    "${TRAILMATE_ROOT}/modules/ui_presentation/src/key_verification/key_verification_model.cpp"
    "${TRAILMATE_ROOT}/modules/chat_presentation_adapters/src/chat_conversation_mapper.cpp"
    "${TRAILMATE_ROOT}/modules/chat_presentation_adapters/src/chat_message_mapper.cpp"
    "${TRAILMATE_ROOT}/modules/ui_chat_runtime/src/chat_delivery_action_port_adapter.cpp"
    "${TRAILMATE_ROOT}/modules/ui_chat_runtime/src/chat_delivery_event_projection_adapter.cpp"
    "${TRAILMATE_ROOT}/modules/ui_chat_runtime/src/chat_page_runtime_event_pump.cpp"
    "${TRAILMATE_ROOT}/modules/ui_key_verification_runtime/src/key_verification_action_sink.cpp"
    "${TRAILMATE_ROOT}/modules/ui_key_verification_runtime/src/key_verification_presentation_source.cpp"
    "${TRAILMATE_ROOT}/modules/core_chat/src/delivery/chat_delivery_action_service.cpp"
    "${TRAILMATE_ROOT}/modules/core_chat/src/delivery/chat_delivery_event_port.cpp"
    "${TRAILMATE_ROOT}/modules/core_chat/src/delivery/chat_delivery_event_projector.cpp"
    "${TRAILMATE_ROOT}/modules/core_chat/src/delivery/chat_delivery_message_projection.cpp"
    "${TRAILMATE_ROOT}/modules/core_chat/src/delivery/chat_delivery_read_model.cpp"
    "${TRAILMATE_ROOT}/modules/core_chat/src/delivery/legacy_chat_delivery_bridge.cpp"
    "${TRAILMATE_ROOT}/modules/core_chat/src/delivery/legacy_chat_send_result_mapper.cpp")

# LVGL chat screen (ui_shared) + the chat-screen presentation_sources/team_action
# sinks it instantiates + the IME widget the composer uses.
set(TRAILMATE_ESP_IDF_CHAT_UI_SOURCES
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/chat/chat_compose_components.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/chat/chat_compose_input.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/chat/chat_compose_layout.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/chat/chat_compose_styles.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/chat/chat_conversation_components.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/chat/chat_conversation_input.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/chat/chat_conversation_layout.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/chat/chat_conversation_styles.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/chat/chat_message_list_components.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/chat/chat_message_list_input.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/chat/chat_message_list_layout.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/chat/chat_message_list_styles.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/chat/chat_page_runtime.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/chat/chat_page_shell.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/chat/chat_protocol_support.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/chat/chat_send_flow.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/chat/chat_team_workflow.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/chat/chat_ui_controller.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/presentation_sources/chat_presentation_source.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/presentation_sources/runtime_chat_action_sink.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/presentation_sources/team_chat_action_sink.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/presentation_sources/team_chat_presentation_source.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/team_actions/team_action_runtime_sink.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/team_actions/team_runtime_adapters.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/team_presentation/team_rich_payload_projector.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/widgets/top_bar.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/components/two_pane_layout.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/components/two_pane_nav.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/components/two_pane_styles.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/components/info_card.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/components/air_status_footer.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/presentation_sources/runtime_device_status_source.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/widgets/ime/ime_widget.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/widgets/ime/pinyin_ime.cpp")

# ---------------------------------------------------------------------------
# Contacts app (mesh NODE LIST: other nodes seen, last-heard, SNR/RSSI, per-node
# detail). stable_id = 'contacts'. Reuses the chat producer set (chat/contact
# services, RAM node store, chat screen widgets the contacts compose/conversation
# panels embed). Adds the contacts page set, the node_info per-node detail screen,
# and the map-tile WIDGET (map_viewport + the arduino_common map_tiles tile engine
# + the ui_map_runtime tile producers it links) used to draw the node-detail
# mini-map. The map_tiles engine is FreeRTOS-based (not Arduino) and degrades to an
# empty map when no SD tiles are present, so it builds and runs in the pure
# IDF/no-SD self-test. SKIP the full map app + its live overlay projection.
set(TRAILMATE_ESP_IDF_CONTACTS_UI_SOURCES
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/contacts/contacts_page_components.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/contacts/contacts_page_input.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/contacts/contacts_page_layout.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/contacts/contacts_page_runtime.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/contacts/contacts_page_shell.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/contacts/contacts_page_styles.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/contacts/contacts_state.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/contacts/contacts_team_snapshot_source.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/node_info/node_info_page_components.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/node_info/node_info_page_layout.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/widgets/map/map_viewport.cpp"
    "${TRAILMATE_ROOT}/platform/esp/arduino_common/src/ui/widgets/map/map_tiles.cpp"
    "${TRAILMATE_ROOT}/modules/ui_map_runtime/src/map_tiles/filesystem_map_tile_source.cpp"
    "${TRAILMATE_ROOT}/modules/ui_map_runtime/src/map_tiles/map_tile_render_queue.cpp"
    "${TRAILMATE_ROOT}/modules/ui_map_runtime/src/map_tiles/map_tile_resolver.cpp"
    # Toast widget: the contacts compose panel (chat_compose) and the node_info
    # layer-notice path call ::ui::widgets::Toast::show(). The free
    # show_toast(const char*, uint32_t) that node_info also calls is owned by the
    # GPS screen (gps_page_components.cpp); when the Map app is bound that TU is
    # compiled in TRAILMATE_ESP_IDF_GPS_UI_SOURCES and provides the real
    # show_toast for BOTH node_info and the GPS page, so the IDF node_info-only
    # stand-in (idf_common/src/ui_toast_compat.cpp) is intentionally NOT listed
    # here -- keeping it would duplicate the show_toast symbol at link time.
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/widgets/toast/toast_widget.cpp")

# ---------------------------------------------------------------------------
# Settings app (device/radio/channel/GPS config editor + broadcast-nodeinfo
# action). stable_id = 'settings'. The editor reads/writes live config via
# app::configFacade().getConfig() (AppConfig, provided by IdfChatFacade) and the
# platform::ui::* runtime services. Most of those producers are already compiled
# in TRAILMATE_ESP_IDF_PLATFORM_COMMON_SOURCES (time, device, tracker, wifi, gps,
# team_ui_store, wireless_companion, settings_store -- which also implements the
# platform::ui::screen contract inline). This adds the two remaining producers the
# settings screen references -- firmware_update + settings_backup (self-contained
# IDF "unsupported" runtimes, no Arduino deps) -- in the PLATFORM block below.
#
# Screen set = the settings page .cpp's plus the portable presentation/runtime
# deps it instantiates: the SettingsModel (ui_presentation) bound to the
# RuntimeSettingsSource/ActionSink (ui_shared presentation_sources), and the
# busy_overlay widget the firmware-update progress path drives. menu_layout,
# info_card, system_notification, the mc/mt region presets, and the two_pane_*
# components are already in the chat/contacts/ui_shared sets.
set(TRAILMATE_ESP_IDF_SETTINGS_UI_SOURCES
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/settings/settings_page_components.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/settings/settings_page_input.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/settings/settings_page_layout.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/settings/settings_page_runtime.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/settings/settings_page_shell.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/settings/settings_page_styles.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/settings/settings_state.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/presentation_sources/runtime_settings_source.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/widgets/busy_overlay.cpp"
    "${TRAILMATE_ROOT}/modules/ui_presentation/src/settings/settings_model.cpp"
    # The settings screen pulls in the menu/dashboard refresh path
    # (menu_layout::refresh_localized_text -> dashboard widgets), which makes the
    # dashboard mesh + compass widgets reachable (previously gc-section'd away in
    # the chat/contacts-only boot). Those widgets bind the live mesh-status model
    # to its runtime source; both are portable (facade + team_ui_snapshot_store,
    # all already-compiled producers) so add them here.
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/presentation_sources/runtime_mesh_status_source.cpp"
    "${TRAILMATE_ROOT}/modules/ui_presentation/src/mesh/mesh_status_model.cpp")

# ---------------------------------------------------------------------------
# Sky-plot / GNSS satellite app (sky-plot of satellites in view + per-satellite
# signal/elevation table). stable_id = 'sky_plot'; screen dir is screens/gnss.
# Self-contained, no map/SD dependency. The page shell wraps the runtime with the
# header-only page_shell_fallback template: when device::gps_supported() is false
# it shows the shared placeholder_page; otherwise the live runtime reads satellites
# via platform::ui::gps::get_gnss_snapshot() (already compiled in
# TRAILMATE_ESP_IDF_PLATFORM_COMMON_SOURCES). Its other deps -- the TopBar widget,
# app_runtime group helpers, page_profile, font_utils, ui_common battery readout --
# are all already in the chat/ui_shared/platform sets. Only the two gnss screen
# .cpp's are new here, plus placeholder_page.cpp: the fallback template instantiates
# placeholder_page::show/hide (non-inline), so its translation unit must link even
# though gps_supported() is true on this board and the placeholder path is not taken.
set(TRAILMATE_ESP_IDF_GNSS_UI_SOURCES
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/gnss/gnss_skyplot_page_runtime.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/gnss/gnss_skyplot_page_shell.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/common/placeholder_page.cpp")

# ---------------------------------------------------------------------------
# GPS / Map app (the full offline-tile map: your-position marker + mesh node
# markers + tracker/route overlays). stable_id = 'map'; screen dir is
# screens/gps. The page shell wraps the runtime with the header-only
# page_shell_fallback template (its placeholder_page::show/hide non-inline TU is
# ALREADY linked via TRAILMATE_ESP_IDF_GNSS_UI_SOURCES, so it is intentionally
# NOT repeated here -- a second copy would be a duplicate-symbol link error).
# is_available() == platform::ui::device::gps_supported() == true on the P4, so
# the live map runtime is entered.
#
# The runtime is the ESP/ARDUINO branch of gps_page_runtime.cpp (ESP_PLATFORM is
# defined). It composes the page from the screens/gps sub-modules (layout /
# styles / lifetime / components / input / map / modal / route & tracker
# overlays), whose bodies live in platform/esp/arduino_common/src/ui/screens/gps/
# (pure FreeRTOS/stdlib, not Arduino-API bound -- they reach config via
# app::configFacade().getConfig(), the SD via bsp_runtime, and GPS/team via the
# platform::ui producers), so they build and run in the pure IDF/no-SD self-test.
# It also drives the GpsPageRuntimePump (ui_gps_runtime), whose out-of-line
# methods are in gps_page_runtime_pump.cpp.
#
# The map-tile WIDGET stack the runtime renders into -- map_viewport.cpp, the
# arduino_common map_tiles tile engine, and the ui_map_runtime tile producers --
# is intentionally NOT listed here: it is ALREADY compiled in
# TRAILMATE_ESP_IDF_CONTACTS_UI_SOURCES (the node-detail mini-map), and re-adding
# it would be a duplicate-symbol link error. Likewise, the free
# show_toast(const char*, uint32_t) the GPS components own is provided here by
# gps_page_components.cpp, so the node_info-only IDF stand-in
# (idf_common/src/ui_toast_compat.cpp) is removed from the Contacts group above
# to avoid a duplicate definition.
set(TRAILMATE_ESP_IDF_GPS_UI_SOURCES
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/gps/gps_page_shell.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/gps/gps_page_runtime.cpp"
    "${TRAILMATE_ROOT}/modules/ui_gps_runtime/src/gps_page_runtime_pump.cpp"
    "${TRAILMATE_ROOT}/platform/esp/arduino_common/src/ui/screens/gps/gps_page_layout.cpp"
    "${TRAILMATE_ROOT}/platform/esp/arduino_common/src/ui/screens/gps/gps_page_styles.cpp"
    "${TRAILMATE_ROOT}/platform/esp/arduino_common/src/ui/screens/gps/gps_page_lifetime.cpp"
    "${TRAILMATE_ROOT}/platform/esp/arduino_common/src/ui/screens/gps/gps_page_components.cpp"
    "${TRAILMATE_ROOT}/platform/esp/arduino_common/src/ui/screens/gps/gps_page_input.cpp"
    "${TRAILMATE_ROOT}/platform/esp/arduino_common/src/ui/screens/gps/gps_page_map.cpp"
    "${TRAILMATE_ROOT}/platform/esp/arduino_common/src/ui/screens/gps/gps_modal.cpp"
    "${TRAILMATE_ROOT}/platform/esp/arduino_common/src/ui/screens/gps/gps_route_overlay.cpp"
    "${TRAILMATE_ROOT}/platform/esp/arduino_common/src/ui/screens/gps/gps_tracker_overlay.cpp"
    # gps_page_map.cpp draws the team node markers via TeamMapOverlaySource
    # (loads each member's last location from the shared team_ui snapshot store)
    # and the "your position" marker from the room_24px icon descriptor, so both
    # of those producers are pulled in here.
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/presentation_sources/team_map_overlay_source.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/assets/room-24px.c")

# ---------------------------------------------------------------------------
# Extensions app (Wi-Fi / companion extensions status panel: language-pack
# catalog browser + install/update/uninstall + per-package detail).
# stable_id = 'extensions'. The screen reads Wi-Fi reachability via
# platform::ui::wifi::status() (already compiled) and the package catalog/install
# state via ui::runtime::packs::* (pack_repository.cpp). Its other UI deps -- the
# TopBar/two_pane_layout/two_pane_styles/info_card components, busy_overlay +
# system_notification widgets, page_profile, localization, theme -- are all
# already in the chat/settings/ui_shared sets.
#
# The pack_repository backend lives in arduino_common but its translation unit
# has a pure ESP-IDF (non-ARDUINO) branch (#if defined(ESP_PLATFORM) ... #else)
# that talks to the SD card via bsp_runtime::sdcard_mount_point() and fetches the
# remote catalog with esp_http_client + the json/mbedtls(esp_crt_bundle)/miniz
# components (added to main's REQUIRES). card_ready()/firmware_version()
# (platform_ui_device_runtime), current_memory_profile() (memory_profile.cpp),
# and reload_language() (resource_pack_registry) are all already-compiled
# producers, so only these two screen .cpp's plus the backend are new here.
set(TRAILMATE_ESP_IDF_EXTENSIONS_UI_SOURCES
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/extensions/extensions_page_runtime.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/extensions/extensions_page_shell.cpp"
    "${TRAILMATE_ROOT}/platform/esp/arduino_common/src/ui/runtime/pack_repository.cpp")

# ---------------------------------------------------------------------------
# Tracker app (record/list/delete GPS tracks + KML route files). stable_id =
# 'tracker'. The screen reads/writes tracks via platform::ui::tracker::* and
# routes via platform::ui::route_storage::* (both SD-backed via bsp_runtime), and
# reads route config through app::appFacade().getConfig() (provided by
# IdfChatFacade). platform_ui_tracker_runtime.cpp is already compiled in the
# PLATFORM block; platform_ui_route_storage.cpp is added there too. The page shell
# wraps the runtime with the header-only page_shell_fallback template, whose
# placeholder_page::show/hide (non-inline) translation unit is ALREADY linked via
# TRAILMATE_ESP_IDF_GNSS_UI_SOURCES, so it is intentionally NOT repeated here (a
# second copy would be a duplicate-symbol link error). All other UI deps -- the
# TopBar widget, two_pane_nav controller, page_profile, font_utils, app_runtime
# group helpers, ui_common battery, localization/theme -- are already in the
# chat/contacts/settings/ui_shared sets. Self-test note: refresh_record_list /
# refresh_route_list short-circuit to a "No SD Card" empty state when
# device::sd_ready() is false, and cleanup_page tears down synchronously (no queued
# lv_async_call), so the boot self-test enters+exits cleanly regardless of SD state.
set(TRAILMATE_ESP_IDF_TRACKER_UI_SOURCES
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/tracker/tracker_page_components.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/tracker/tracker_page_input.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/tracker/tracker_page_layout.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/tracker/tracker_page_runtime.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/tracker/tracker_page_shell.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/tracker/tracker_state.cpp")

# ---------------------------------------------------------------------------
# Energy Sweep app (LoRa RSSI spectrum sweep over the configured region band:
# per-bin RSSI bars, noise floor + hot-bin detection, "best" clear-channel pick).
# stable_id = 'energy_sweep'. The screen samples RSSI through platform::ui::lora::*
# (configure_receive/read_instant_rssi over the shared SX126x radio) and derives
# the sweep band from the live mesh config via app::configFacade().getConfig() +
# the region tables (chat::meshtastic::getRegionTable / chat::meshcore::
# findRegionPresetById, both already compiled in the MESHTASTIC/CORE_CHAT sets). It
# only acquires the radio lazily on a SCAN press (acquire_radio_runtime); on entry
# it shows simulated bars, so the boot self-test enters+exits with no radio
# contention. Screen-sleep is held off during the view via platform::ui::screen::
# disable_sleep/enable_sleep -- already provided inline by screen_sleep.cpp (no shim
# needed). device::delay_ms (inter-sample settle) comes from the device runtime.
# The page shell wraps the runtime with the header-only page_shell_fallback
# template, whose placeholder_page::show/hide (non-inline) translation unit is
# ALREADY linked via TRAILMATE_ESP_IDF_GNSS_UI_SOURCES, so it is intentionally NOT
# repeated here (a second copy would be a duplicate-symbol link error). Only the two
# energy_sweep screen .cpp's are new here; the backing producer
# platform_ui_lora_runtime.cpp is added to the PLATFORM block below. All other UI
# deps -- localization, theme, app_runtime group helpers, fonts, ui_common -- are
# already in the chat/ui_shared/platform sets.
set(TRAILMATE_ESP_IDF_ENERGY_SWEEP_UI_SOURCES
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/energy_sweep/energy_sweep_page_runtime.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/energy_sweep/energy_sweep_page_shell.cpp")

# ---------------------------------------------------------------------------
# PC Link app (USB-CDC bridge to a PC companion: shows host link state +
# RX/TX frame counters; in RNode-protocol mode it presents as a KISS modem for
# Reticulum). stable_id = 'pc_link'; screen dir is screens/pc_link. The screen
# reads link state via platform::ui::hostlink::* and labels itself from
# app::appFacade().getMeshProtocol() (provided by IdfChatFacade). The page shell
# wraps the runtime with the header-only page_shell_fallback template, whose
# placeholder_page::show/hide (non-inline) translation unit is ALREADY linked via
# TRAILMATE_ESP_IDF_GNSS_UI_SOURCES, so it is intentionally NOT repeated here (a
# second copy would be a duplicate-symbol link error). is_available() ==
# platform::ui::hostlink::is_supported(), which is true on the P4 (it has the
# USB-Serial-JTAG controller), so the live runtime is entered. enter() spawns the
# hostlink FreeRTOS task (no USB host attached during the boot self-test, so it
# simply sits in the "Waiting for host" state) and exit() stops it + deletes the
# root synchronously (lv_timer_del of the 300ms refresh timer, no queued
# lv_async_call), so it is self-test safe.
#
# Backing = the IDF hostlink runtime (platform_ui_hostlink_runtime.cpp, added to
# the PLATFORM block) which delegates to the shared (arduino_common) hostlink
# service. That service is FreeRTOS/stdlib-portable, not Arduino-bound: it speaks
# the host link wire protocol over the IDF USB-Serial-JTAG transport
# (idf_common/usb_cdc_transport.cpp -- the IDF implementation of the usb_cdc::
# interface, NOT the Arduino one) using the core_hostlink codec set above, and
# reaches mesh/team/config through the app facades (app::messagingFacade()/
# teamFacade()/configFacade(), all the bound IdfChatFacade). Its only Arduino-GPS
# coupling is the gps::gps_get_data() free function it calls when streaming GPS
# events; that single symbol is provided for the IDF build by
# idf_hostlink_gps_compat.cpp (added to the PLATFORM block), which forwards to the
# live platform::ui::gps fix. set_time_epoch() uses settimeofday() on the P4
# (the tab5 RTC branch is compiled out unless TRAIL_MATE_ESP_BOARD_TAB5).
#
# team_presence_model.cpp (the isTeamMemberOnline helper the team-state bridge
# uses) is the only ui_shared straggler not already pulled by another app set.
set(TRAILMATE_ESP_IDF_PC_LINK_UI_SOURCES
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/pc_link/pc_link_page_runtime.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/pc_link/pc_link_page_shell.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/team_presence/team_presence_model.cpp"
    "${TRAILMATE_ROOT}/platform/esp/arduino_common/src/hostlink/hostlink_service.cpp"
    "${TRAILMATE_ROOT}/platform/esp/arduino_common/src/hostlink/hostlink_config_service.cpp"
    "${TRAILMATE_ROOT}/platform/esp/arduino_common/src/hostlink/hostlink_bridge_radio.cpp"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/usb_cdc_transport.cpp")

# ---------------------------------------------------------------------------
# Team app (the team-coordination screen: a shared-map/team-awareness status +
# create/join/manage flow). stable_id = 'team'; screen dir is screens/team.
# SAFE-SCREEN BIND ONLY: the functional team controller + pairing service are a
# separate owner-verified follow-up. On this IDF build app::teamFacade()
# .getTeamController()/getTeamPairing() return nullptr (idf_chat_facade.cpp), so
# team::ui::runtime::enter() builds the REAL page at a guarded 'You are not in a
# team' status; the page's runtime port guards the null controller/pairing
# everywhere (team_page_runtime_port.cpp's controller_/pairing_ null checks), and
# the Create/Join actions are disabled + labeled '(soon)' on the status screen
# when no controller is present (touch-only device: no dead buttons).
#
# is_available() == app::hasAppFacade() (true at boot), so enter() runs
# team_page_create(); exit() runs team_page_destroy() which tears down
# synchronously (no queued lv_async_call: cleanup_team_input + lv_group_del +
# deferred_dispatch.clearAll() + lv_obj_del of the root), so the boot self-test
# enters+exits cleanly.
#
# These 26 team screen TUs were NOT compiled before. Their backend closure --
# the team controller/service + team_* protocol codecs
# (TRAILMATE_ESP_IDF_CORE_TEAM_SOURCES) and the IDF team UI snapshot store
# (platform_ui_team_ui_store_runtime.cpp, in the PLATFORM block) -- is ALREADY
# linked (the chat screen's team-action plumbing drags it in), so the UI TUs
# link without adding backend sources. team_topbar.c is intentionally NOT added
# here (it is a different, already-compiled symbol); the IDF team UI store is
# likewise already compiled, so its arduino store shell is not added (would be a
# duplicate symbol). The launcher icon descriptor (team_icon, in
# ui/assets/team.c) is added to TRAILMATE_ESP_IDF_UI_SHARED_SOURCES alongside
# Chat.c / walkie_talkie.c.
set(TRAILMATE_ESP_IDF_TEAM_UI_SOURCES
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_activity_adapters.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_activity_sink.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_command_reducer.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_components.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_create_team_action.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_deferred_dispatch.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_deferred_dispatch_adapters.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_event_effect_sink.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_event_reducer.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_flow_controller.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_input.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_key_event_log.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_key_request_action.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_kick_confirm_action.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_layout.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_lvgl_renderer.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_pairing_command_action.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_read_model.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_request_keys_action.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_runtime.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_runtime_adapters.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_runtime_port.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_shell.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_state_store.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_styles.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_transfer_leader_action.cpp")

# ---------------------------------------------------------------------------
# SSTV receiver app (decode an incoming slow-scan-TV image from off-air audio
# captured through the ES8311 mic and render/save it). stable_id = 'sstv'; screen
# dir is screens/sstv. Mirrors the chat/contacts/pc_link bind: the sstv page
# shell's enter/exit take a ui::page::Host* (sstv_page::ui::shell::Host is an alias
# of ::ui::page::Host) as user_data and route the back request through
# ui_request_exit_to_menu() via the menu host. The shell wraps the runtime with the
# header-only page_shell_fallback template; placeholder_page::show/hide (non-inline)
# is ALREADY linked via TRAILMATE_ESP_IDF_GNSS_UI_SOURCES, so it is intentionally
# NOT repeated here (a second copy would be a duplicate-symbol link error).
# is_available() == platform::ui::sstv::is_supported() == (has_audio && has_sdcard)
# == true on the P4, so the live runtime is entered. The RX button (already in the
# runtime) calls platform::ui::sstv::start(); on the P4 that drives the functional
# capture/decode backend below. enter() only builds the page + a 120ms refresh
# timer (no audio task spawns until RX is pressed) and exit() deletes the timer +
# root synchronously and stops any active capture, so the boot self-test enters+
# exits cleanly.
#
# Backing producer platform::ui::sstv (idf_common/platform_ui_sstv_runtime.cpp,
# delegating to the shared ::sstv service) is added to the PLATFORM block; the
# functional ::sstv service + c_sstv_decoder demodulator are added to the P4 board
# source set (they reference the board ES8311 codec). The launcher icon descriptor
# (sstv, lv_image_dsc_t, in ui/assets/sstv.c) is added to
# TRAILMATE_ESP_IDF_UI_SHARED_SOURCES alongside Chat.c / walkie_talkie.c / team.c.
set(TRAILMATE_ESP_IDF_SSTV_UI_SOURCES
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/sstv/sstv_page_shell.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/sstv/sstv_page_runtime.cpp")

# Chat-screen LVGL renderers from the ux-pack common layer (modal + picker the
# chat controller wires).
set(TRAILMATE_ESP_IDF_CHAT_UX_PACK_SOURCES
    "${TRAILMATE_ROOT}/modules/ui_lvgl_ux_packs/src/common/key_verification_modal_renderer.cpp"
    "${TRAILMATE_ROOT}/modules/ui_lvgl_ux_packs/src/common/team_position_picker_renderer.cpp")

set(TRAILMATE_ESP_IDF_UI_SHARED_SOURCES
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/assets/alert.c"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/assets/ble_topbar.c"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/assets/gps_topbar.c"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/assets/logo.c"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/assets/message_topbar.c"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/assets/route_topbar.c"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/assets/team_topbar.c"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/assets/tracker_topbar.c"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/assets/wifi_topbar.c"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/app_runtime.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/formatters.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/loop_shell.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/startup_shell.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/ui_boot.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/ui_status.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/watch_face.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/assets/fonts/font_utils.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/i18n/resource_pack_registry.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/menu/dashboard/dashboard_compass_widget.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/menu/dashboard/dashboard_gps_widget.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/menu/dashboard/dashboard_mesh_widget.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/menu/dashboard/dashboard_recent_widget.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/menu/dashboard/dashboard_state.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/menu/dashboard/dashboard_style.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/menu/menu_dashboard.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/menu/menu_layout.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/menu/menu_profile.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/menu/menu_runtime.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/page/page_profile.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/presentation_sources/runtime_gps_status_source.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/runtime/memory_profile.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/widgets/system_notification.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/widgets/keyboard_button.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/assets/Setting.c"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/assets/Chat.c"
    # Team position-marker icon descriptors (lv_image_dsc_t) referenced as extern
    # "C" symbols by ui_lvgl_ux_packs/.../team_position_picker_renderer.cpp, which
    # the chat controller pulls in even though team mode is stubbed off at runtime.
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/assets/AreaCleared.c"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/assets/BaseCamp.c"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/assets/GoodFind.c"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/assets/rally.c"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/assets/sos.c"
    # Walkie-talkie (push-to-talk FSK voice) screen: the page shell + live runtime
    # and its launcher-icon descriptor. Bound into the IDF launcher's hardcoded
    # s_apps[] as s_walkie_app (see esp32_lvgl_idf_app_registry.cpp). The shell
    # wraps the runtime with the page_shell_fallback template, whose
    # placeholder_page::show/hide TU is ALREADY linked via the GNSS UI set, so it is
    # not repeated here. The runtime's audio/radio backend is the walkie service +
    # idf_common walkie runtime in TRAILMATE_ESP_IDF_WALKIE_SOURCES (P4 only).
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/walkie_talkie/walkie_talkie_page_shell.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/screens/walkie_talkie/walkie_talkie_page_runtime.cpp"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/assets/walkie_talkie.c"
    # Team launcher icon descriptor (team_icon, lv_image_dsc_t). Referenced as an
    # extern "C" symbol by s_team_app in esp32_lvgl_idf_app_registry.cpp; the team
    # screen TUs that use it are in TRAILMATE_ESP_IDF_TEAM_UI_SOURCES.
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/assets/team.c"
    # SSTV launcher icon descriptor (sstv, lv_image_dsc_t). Referenced as an
    # extern "C" symbol by s_sstv_app in esp32_lvgl_idf_app_registry.cpp; the sstv
    # screen TUs that use it are in TRAILMATE_ESP_IDF_SSTV_UI_SOURCES.
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/assets/sstv.c")

set(TRAILMATE_ESP_IDF_UI_PRESENTATION_SOURCES
    "${TRAILMATE_ROOT}/modules/ui_presentation/src/gps/gps_status_model.cpp"
    "${TRAILMATE_ROOT}/modules/ui_presentation/src/menu/menu_model.cpp"
    "${TRAILMATE_ROOT}/modules/ui_presentation/src/device/device_status_model.cpp")

set(TRAILMATE_ESP_IDF_UI_LVGL_UX_PACK_SOURCES
    "${TRAILMATE_ROOT}/modules/ui_lvgl_ux_packs/src/packs/cardputer_compact_ux_pack.cpp"
    "${TRAILMATE_ROOT}/modules/ui_lvgl_ux_packs/src/packs/compatibility_ux_pack.cpp"
    "${TRAILMATE_ROOT}/modules/ui_lvgl_ux_packs/src/packs/simulator_full_ux_pack.cpp"
    "${TRAILMATE_ROOT}/modules/ui_lvgl_ux_packs/src/packs/tiny_node_status_ux_pack.cpp"
    "${TRAILMATE_ROOT}/modules/ui_lvgl_ux_packs/src/packs/uconsole_desktop_ux_pack.cpp"
    "${TRAILMATE_ROOT}/modules/ui_lvgl_ux_packs/src/ux/input_binding_set.cpp"
    "${TRAILMATE_ROOT}/modules/ui_lvgl_ux_packs/src/ux/screen_registry.cpp"
    "${TRAILMATE_ROOT}/modules/ui_lvgl_ux_packs/src/ux/ux_menu_provider.cpp"
    "${TRAILMATE_ROOT}/modules/ui_lvgl_ux_packs/src/ux/ux_pack_registry.cpp"
    "${TRAILMATE_ROOT}/modules/ui_lvgl_ux_packs/src/ux/ux_screen_menu_adapter.cpp")

set(TRAILMATE_ESP_IDF_PLATFORM_COMMON_SOURCES
    "${TRAILMATE_ROOT}/platform/esp/boards/src/board_runtime.cpp"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/app_runtime_support.cpp"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/ble_manager_stub.cpp"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/bsp_runtime.cpp"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/c6_companion_runtime.cpp"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/debug/sd_coredump_export.cpp"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/display_spi_lock.cpp"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/gps_runtime.cpp"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/lv_helper.cpp"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/platform_ui_device_runtime.cpp"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/platform_ui_firmware_update_runtime.cpp"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/platform_ui_gps_runtime.cpp"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/platform_ui_orientation_runtime.cpp"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/platform_ui_settings_backup_runtime.cpp"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/platform_ui_team_ui_store_runtime.cpp"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/platform_ui_settings_store.cpp"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/platform_ui_time_runtime.cpp"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/platform_ui_tracker_runtime.cpp"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/platform_ui_route_storage.cpp"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/platform_ui_wifi_runtime.cpp"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/startup_support.cpp"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/screen_sleep.cpp"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/sx126x_radio.cpp"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/radiolib_idf_hal.cpp"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/platform_ui_lora_runtime.cpp"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/ui_common.cpp"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/ui_dispatcher.cpp"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/platform_ui_wireless_companion_runtime.cpp"
    # SSTV capability runtime: platform::ui::sstv::start/stop/is_supported/... that
    # the SSTV screen (TRAILMATE_ESP_IDF_SSTV_UI_SOURCES) calls. Without it the
    # screen does not link. It delegates to the shared ::sstv service (added to the
    # board source set below); is_supported() is (has_audio && has_sdcard), both
    # true on the P4.
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/platform_ui_sstv_runtime.cpp"
    # PC Link host-bridge backend: the platform::ui::hostlink runtime (delegates
    # to the shared ::hostlink service over the IDF USB-Serial-JTAG transport)
    # plus the gps::gps_get_data() free-function compat the service needs to
    # stream GPS events from the live IDF GPS fix. See the
    # TRAILMATE_ESP_IDF_PC_LINK_UI_SOURCES block for the full rationale.
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/platform_ui_hostlink_runtime.cpp"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/idf_hostlink_gps_compat.cpp"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/idf_chat_factory.cpp"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/idf_chat_facade.cpp"
    "${TRAILMATE_ROOT}/platform/esp/radio/meshtastic_radio_adapter.cpp")

# Walkie-talkie (push-to-talk FSK voice) backend, shared by every IDF board that
# has both an audio codec and the SX1262: the platform::ui::walkie facade runtime
# (delegates to ::walkie::*), the shared ::walkie service (codec2 3200 encode/decode
# + FSK packetization, the REAL ::walkie::start() with the codec_open marker), and
# the idf_common walkie runtime (radio + board-codec glue; board-gated internally to
# CodecCompat on Tab5 / CodecEs8311 on the T-Display P4). Compiled per-board (added
# to each board source set below) because it references the board-specific codec
# class. codec2 is pulled in too: the walkie service's real path #include <codec2.h>
# and calls codec2_* (this is also what links the codec2_* symbol the walkie check
# asserts).
include("${TRAILMATE_ROOT}/third_party/codec2/codec2_sources.cmake")
set(TRAILMATE_ESP_IDF_WALKIE_SOURCES
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/platform_ui_walkie_runtime.cpp"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/walkie_runtime.cpp"
    "${TRAILMATE_ROOT}/platform/esp/arduino_common/src/walkie/walkie_service.cpp"
    ${trail_mate_codec2_sources})

set(TRAILMATE_ESP_IDF_TAB5_BOARD_SOURCES
    "${TRAILMATE_ROOT}/boards/tab5/src/codec_compat.cpp"
    "${TRAILMATE_ROOT}/boards/tab5/src/heading_runtime.cpp"
    "${TRAILMATE_ROOT}/boards/tab5/src/rtc_runtime.cpp"
    "${TRAILMATE_ROOT}/boards/tab5/src/tab5_board.cpp"
    ${TRAILMATE_ESP_IDF_WALKIE_SOURCES})

# SSTV functional capture/decode backend, compiled per-board because the service
# references the board's audio codec class (CodecEs8311 on the P4, the walkie
# service is the exact precedent for adding board-specific audio sources here).
# sstv_service.cpp's board #if now has a T-Display P4 arm (mirroring the Tab5
# branch but with the ES8311 codec): the REAL ::sstv::start() spawns the capture
# task and feeds samples to the c_sstv_decoder demodulator in decode_sstv.cpp.
# c_sstv_decoder is pure C++/stdlib but links against two helper TUs in the same
# directory -- cordic.cpp (cordic_init / cordic_rectangular_to_polar, the FM
# discriminator's fixed-point rect->polar) and half_band_filter2.cpp
# (half_band_filter2, the IQ decimation half-band filter) -- so both are part of
# the decoder closure here. No Arduino/board deps. On any board NOT in the SSTV
# gate the service compiles to the #else stub.
set(TRAILMATE_ESP_IDF_SSTV_SERVICE_SOURCES
    "${TRAILMATE_ROOT}/platform/esp/arduino_common/src/sstv/sstv_service.cpp"
    "${TRAILMATE_ROOT}/platform/esp/arduino_common/src/sstv/decode_sstv.cpp"
    "${TRAILMATE_ROOT}/platform/esp/arduino_common/src/sstv/cordic.cpp"
    "${TRAILMATE_ROOT}/platform/esp/arduino_common/src/sstv/half_band_filter2.cpp")

set(TRAILMATE_ESP_IDF_T_DISPLAY_P4_BOARD_SOURCES
    "${TRAILMATE_ROOT}/boards/t_display_p4/src/rtc_runtime.cpp"
    "${TRAILMATE_ROOT}/boards/t_display_p4/src/runtime_support.cpp"
    "${TRAILMATE_ROOT}/boards/t_display_p4/src/codec_es8311.cpp"
    "${TRAILMATE_ROOT}/boards/t_display_p4/src/t_display_p4_board.cpp"
    ${TRAILMATE_ESP_IDF_WALKIE_SOURCES}
    ${TRAILMATE_ESP_IDF_SSTV_SERVICE_SOURCES})

set(TRAILMATE_ESP_IDF_FINAL_INCLUDE_DIRS
    "${TRAILMATE_ROOT}/apps/esp32_lvgl/src"
    "${TRAILMATE_ROOT}/modules/core_hostlink/include"
    "${TRAILMATE_ROOT}/modules/core_sys/include"
    "${TRAILMATE_ROOT}/modules/core_chat/include"
    "${TRAILMATE_ROOT}/modules/core_chat/generated"
    "${TRAILMATE_ROOT}/modules/core_chat/third_party/nanopb"
    "${TRAILMATE_ROOT}/modules/core_device/include"
    "${TRAILMATE_ROOT}/modules/core_gps/include"
    "${TRAILMATE_ROOT}/modules/core_mesh/include"
    "${TRAILMATE_ROOT}/modules/core_phone/include"
    "${TRAILMATE_ROOT}/modules/core_team/include"
    "${TRAILMATE_ROOT}/modules/product_composition/include"
    "${TRAILMATE_ROOT}/modules/chat_presentation_adapters/include"
    "${TRAILMATE_ROOT}/modules/ui_chat_runtime/include"
    "${TRAILMATE_ROOT}/modules/ui_gps_runtime/include"
    "${TRAILMATE_ROOT}/modules/ui_key_verification_runtime/include"
    "${TRAILMATE_ROOT}/modules/ui_lvgl_core/include"
    "${TRAILMATE_ROOT}/modules/ui_lvgl_ux_packs/include"
    "${TRAILMATE_ROOT}/modules/ui_map_runtime/include"
    "${TRAILMATE_ROOT}/modules/ui_presentation/include"
    "${TRAILMATE_ROOT}/modules/ui_shared/include"
    "${TRAILMATE_ROOT}/platform/esp/arduino_common/include"
    "${TRAILMATE_ROOT}/platform/esp/boards/include"
    "${TRAILMATE_ROOT}/platform/esp/common/include"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/include"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/third_party/RadioLib/src"
    # codec2 vocoder headers (codec2.h) for the walkie-talkie voice path.
    "${TRAILMATE_ROOT}/third_party/codec2/src"
    "${TRAILMATE_ROOT}/platform/shared/include"
    "${TRAILMATE_ROOT}/boards/tab5/include"
    "${TRAILMATE_ROOT}/boards/t_display_p4/include"
    "${TRAILMATE_ROOT}")
