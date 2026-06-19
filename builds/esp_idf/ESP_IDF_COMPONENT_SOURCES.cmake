# Final ESP-IDF component source ownership for migrated surfaces.
#
# The source list owner is this build entrypoint plus the final app, platform,
# and module owners. Do not restore historical app/component roots to include
# this file.

set(TRAILMATE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")

set(TRAILMATE_ESP_IDF_APP_SHELL_SOURCES
    "${TRAILMATE_ROOT}/apps/esp32_lvgl/src/esp32_lvgl_idf_app_registry.cpp"
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
    "${TRAILMATE_ROOT}/modules/core_hostlink/src/c6_frame_codec.cpp")

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
    "${TRAILMATE_ROOT}/modules/core_team/src/protocol/team_chat.cpp"
    "${TRAILMATE_ROOT}/modules/core_team/src/protocol/team_location_marker.cpp"
    "${TRAILMATE_ROOT}/modules/core_team/src/protocol/team_mgmt.cpp"
    "${TRAILMATE_ROOT}/modules/core_team/src/protocol/team_wire.cpp"
    "${TRAILMATE_ROOT}/modules/core_team/src/protocol/team_waypoint.cpp"
    "${TRAILMATE_ROOT}/modules/core_team/src/protocol/team_position.cpp"
    "${TRAILMATE_ROOT}/modules/core_team/src/protocol/team_track.cpp")

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
    # layer-notice path call ::ui::widgets::Toast::show(); the GPS screen that
    # owns the free show_toast() in Arduino/Linux builds is out of scope here, so
    # node_info's show_toast() is provided by the IDF compat producer below.
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/widgets/toast/toast_widget.cpp"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/ui_toast_compat.cpp")

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
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/assets/Setting.c"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/assets/Chat.c"
    # Team position-marker icon descriptors (lv_image_dsc_t) referenced as extern
    # "C" symbols by ui_lvgl_ux_packs/.../team_position_picker_renderer.cpp, which
    # the chat controller pulls in even though team mode is stubbed off at runtime.
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/assets/AreaCleared.c"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/assets/BaseCamp.c"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/assets/GoodFind.c"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/assets/rally.c"
    "${TRAILMATE_ROOT}/modules/ui_shared/src/ui/assets/sos.c")

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
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/bsp_runtime.cpp"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/c6_companion_runtime.cpp"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/debug/sd_coredump_export.cpp"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/display_spi_lock.cpp"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/gps_runtime.cpp"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/platform_ui_device_runtime.cpp"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/platform_ui_gps_runtime.cpp"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/platform_ui_team_ui_store_runtime.cpp"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/platform_ui_settings_store.cpp"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/platform_ui_time_runtime.cpp"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/platform_ui_tracker_runtime.cpp"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/platform_ui_wifi_runtime.cpp"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/startup_support.cpp"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/screen_sleep.cpp"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/sx126x_radio.cpp"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/ui_common.cpp"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/ui_dispatcher.cpp"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/platform_ui_wireless_companion_runtime.cpp"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/idf_chat_factory.cpp"
    "${TRAILMATE_ROOT}/platform/esp/idf_common/src/idf_chat_facade.cpp"
    "${TRAILMATE_ROOT}/platform/esp/radio/meshtastic_radio_adapter.cpp")

set(TRAILMATE_ESP_IDF_TAB5_BOARD_SOURCES
    "${TRAILMATE_ROOT}/boards/tab5/src/codec_compat.cpp"
    "${TRAILMATE_ROOT}/boards/tab5/src/heading_runtime.cpp"
    "${TRAILMATE_ROOT}/boards/tab5/src/rtc_runtime.cpp"
    "${TRAILMATE_ROOT}/boards/tab5/src/tab5_board.cpp")

set(TRAILMATE_ESP_IDF_T_DISPLAY_P4_BOARD_SOURCES
    "${TRAILMATE_ROOT}/boards/t_display_p4/src/rtc_runtime.cpp"
    "${TRAILMATE_ROOT}/boards/t_display_p4/src/runtime_support.cpp"
    "${TRAILMATE_ROOT}/boards/t_display_p4/src/t_display_p4_board.cpp")

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
    "${TRAILMATE_ROOT}/platform/shared/include"
    "${TRAILMATE_ROOT}/boards/tab5/include"
    "${TRAILMATE_ROOT}/boards/t_display_p4/include"
    "${TRAILMATE_ROOT}")
