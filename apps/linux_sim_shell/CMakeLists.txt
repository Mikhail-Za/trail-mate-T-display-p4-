cmake_minimum_required(VERSION 3.20)

project(TrailMateLinuxSimShell LANGUAGES CXX)

include(CTest)

set(TRAIL_MATE_REPO_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../.."
    CACHE PATH "Trail Mate repository root")

include("${TRAIL_MATE_REPO_ROOT}/cmake/TrailMateUxPacks.cmake")
trailmate_add_ui_lvgl_ux_packs(trailmate_ui_lvgl_ux_packs)

add_library(trailmate_linux_sim_shell
    src/linux_sim_app_shell.cpp
    src/linux_sim_historical_source_descriptor.cpp
    src/linux_sim_runtime_entry.cpp
    src/linux_sim_runtime_entry_adoption_probe.cpp
    src/linux_sim_runtime_renderer.cpp
    "${TRAIL_MATE_REPO_ROOT}/modules/product_composition/src/target_profile.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/product_composition/src/target_ux_binding.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/ui_ascii_runtime/src/ascii_menu_runtime_adapter.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/ui_ascii_runtime/src/ascii_screen_host_adapter.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/ui_ascii_runtime/src/ascii_screen_graph_bridge.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/ui_ascii_runtime/src/ascii_runtime_screen_graph_presenter.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/ui_ascii_runtime/src/ascii_runtime_entry_adoption.cpp"
    "${TRAIL_MATE_REPO_ROOT}/modules/ui_ascii_runtime/src/ascii_descriptor_renderer.cpp")

target_include_directories(trailmate_linux_sim_shell
    PUBLIC
        "${CMAKE_CURRENT_LIST_DIR}/src"
        "${TRAIL_MATE_REPO_ROOT}/modules/product_composition/include"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_ascii_runtime/include")

target_link_libraries(trailmate_linux_sim_shell
    PUBLIC
        trailmate_ui_lvgl_ux_packs)

target_compile_features(trailmate_linux_sim_shell PUBLIC cxx_std_17)

if(BUILD_TESTING)
    enable_language(C)

    add_executable(trailmate_linux_sim_app_shell_smoke
        tests/linux_sim_app_shell_smoke.cpp)
    target_link_libraries(trailmate_linux_sim_app_shell_smoke
        PRIVATE trailmate_linux_sim_shell)
    add_test(NAME trailmate_linux_sim_app_shell_smoke
             COMMAND trailmate_linux_sim_app_shell_smoke)

    add_executable(trailmate_linux_sim_historical_source_descriptor_smoke
        tests/linux_sim_historical_source_descriptor_smoke.cpp)
    target_link_libraries(trailmate_linux_sim_historical_source_descriptor_smoke
        PRIVATE trailmate_linux_sim_shell)
    add_test(NAME trailmate_linux_sim_historical_source_descriptor_smoke
             COMMAND trailmate_linux_sim_historical_source_descriptor_smoke)

    add_executable(trailmate_linux_sim_runtime_entry_adoption_probe_smoke
        tests/linux_sim_runtime_entry_adoption_probe_smoke.cpp)
    target_link_libraries(trailmate_linux_sim_runtime_entry_adoption_probe_smoke
        PRIVATE trailmate_linux_sim_shell)
    add_test(NAME trailmate_linux_sim_runtime_entry_adoption_probe_smoke
             COMMAND trailmate_linux_sim_runtime_entry_adoption_probe_smoke)

    add_executable(trailmate_linux_sim_runtime_entry_smoke
        tests/linux_sim_runtime_entry_smoke.cpp)
    target_link_libraries(trailmate_linux_sim_runtime_entry_smoke
        PRIVATE trailmate_linux_sim_shell)
    add_test(NAME trailmate_linux_sim_runtime_entry_smoke
             COMMAND trailmate_linux_sim_runtime_entry_smoke)

    add_executable(trailmate_linux_sim_runtime_entry_fallback_smoke
        tests/linux_sim_runtime_entry_fallback_smoke.cpp)
    target_link_libraries(trailmate_linux_sim_runtime_entry_fallback_smoke
        PRIVATE trailmate_linux_sim_shell)
    add_test(NAME trailmate_linux_sim_runtime_entry_fallback_smoke
             COMMAND trailmate_linux_sim_runtime_entry_fallback_smoke)

    add_executable(trailmate_linux_sim_runtime_renderer_smoke
        tests/linux_sim_runtime_renderer_smoke.cpp)
    target_link_libraries(trailmate_linux_sim_runtime_renderer_smoke
        PRIVATE trailmate_linux_sim_shell)
    add_test(NAME trailmate_linux_sim_runtime_renderer_smoke
             COMMAND trailmate_linux_sim_runtime_renderer_smoke)

    add_executable(trailmate_ascii_runtime_entry_adoption_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_ascii_runtime/tests/ascii_runtime_entry_adoption_smoke.cpp")
    target_link_libraries(trailmate_ascii_runtime_entry_adoption_smoke
        PRIVATE trailmate_linux_sim_shell)
    add_test(NAME trailmate_ascii_runtime_entry_adoption_smoke
             COMMAND trailmate_ascii_runtime_entry_adoption_smoke)

    add_executable(trailmate_ascii_descriptor_renderer_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_ascii_runtime/tests/test_ascii_descriptor_renderer.cpp")
    target_link_libraries(trailmate_ascii_descriptor_renderer_smoke
        PRIVATE trailmate_linux_sim_shell)
    add_test(NAME trailmate_ascii_descriptor_renderer_smoke
             COMMAND trailmate_ascii_descriptor_renderer_smoke)

    add_executable(trailmate_legacy_chat_delivery_action_bridge_alias_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_legacy_adapters/tests/test_legacy_chat_delivery_action_bridge_legacy_alias.cpp")
    target_include_directories(trailmate_legacy_chat_delivery_action_bridge_alias_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_legacy_adapters/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_chat_runtime/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
    target_compile_features(trailmate_legacy_chat_delivery_action_bridge_alias_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_legacy_chat_delivery_action_bridge_alias_smoke
             COMMAND trailmate_legacy_chat_delivery_action_bridge_alias_smoke)

    add_executable(trailmate_legacy_chat_delivery_event_bridge_alias_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_legacy_adapters/tests/test_legacy_chat_delivery_event_bridge_legacy_alias.cpp")
    target_include_directories(trailmate_legacy_chat_delivery_event_bridge_alias_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_legacy_adapters/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_chat_runtime/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
    target_compile_features(trailmate_legacy_chat_delivery_event_bridge_alias_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_legacy_chat_delivery_event_bridge_alias_smoke
             COMMAND trailmate_legacy_chat_delivery_event_bridge_alias_smoke)

    add_executable(trailmate_chat_presentation_source_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_chat_presentation_source.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/domain/chat_model.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/usecase/chat_service.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/store/ram_store.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/delivery/chat_delivery_read_model.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/delivery/chat_delivery_event_projector.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/delivery/chat_delivery_message_projection.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/delivery/legacy_chat_delivery_bridge.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/usecase/contact_service.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/src/sys/clock.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/presentation_sources/chat_presentation_source.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/presentation_sources/runtime_chat_action_sink.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/presentation_sources/legacy_chat_action_sink.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/chat_presentation_adapters/src/chat_conversation_mapper.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/chat_presentation_adapters/src/chat_message_mapper.cpp")
    target_include_directories(trailmate_chat_presentation_source_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/chat_presentation_adapters/include")
    target_compile_features(trailmate_chat_presentation_source_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_chat_presentation_source_smoke
             COMMAND trailmate_chat_presentation_source_smoke)

    add_executable(trailmate_mesh_adapter_router_core_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/tests/test_mesh_adapter_router_core.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/mesh_adapter_router_core.cpp")
    target_include_directories(trailmate_mesh_adapter_router_core_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
    target_compile_features(trailmate_mesh_adapter_router_core_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_mesh_adapter_router_core_smoke
             COMMAND trailmate_mesh_adapter_router_core_smoke)

    add_executable(trailmate_meshcore_protocol_strategy_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_mesh/tests/test_meshcore_protocol_strategy.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_mesh/src/protocol/meshcore/meshcore_protocol_strategy.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_mesh/src/protocol/meshcore/mc_identity_flow.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshcore/mc_region_presets.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshcore/meshcore_identity_crypto.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshcore/meshcore_payload_helpers.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshcore/meshcore_protocol_helpers.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshcore/crypto/ed25519/fe.c"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshcore/crypto/ed25519/ge.c"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshcore/crypto/ed25519/key_exchange.c"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshcore/crypto/ed25519/keypair.c"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshcore/crypto/ed25519/sc.c"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshcore/crypto/ed25519/sha512.c"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshcore/crypto/ed25519/sign.c"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshcore/crypto/ed25519/verify.c")
    target_include_directories(trailmate_meshcore_protocol_strategy_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_mesh/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
    target_compile_features(trailmate_meshcore_protocol_strategy_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_meshcore_protocol_strategy_smoke
             COMMAND trailmate_meshcore_protocol_strategy_smoke)

    add_executable(trailmate_receive_packet_service_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_mesh/tests/test_receive_packet_service.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_mesh/src/usecase/mesh_dedup_service.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_mesh/src/usecase/peer_identity_service.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_mesh/src/usecase/receive_packet_service.cpp")
    target_include_directories(trailmate_receive_packet_service_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_mesh/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_mesh/tests")
    target_compile_features(trailmate_receive_packet_service_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_receive_packet_service_smoke
             COMMAND trailmate_receive_packet_service_smoke)

    add_executable(trailmate_phone_core_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_phone/tests/test_phone_core_smoke.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_phone/src/meshcore/meshcore_phone_core.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_phone/src/meshtastic/meshtastic_phone_core.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_phone/src/meshtastic/meshtastic_phone_session.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshtastic/mt_packet_wire.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshtastic/mt_protocol_helpers.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/domain/channel_hash.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshtastic/mt_radio_config.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/infra/meshtastic/mt_region.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/src/platform/ui/timezone_profile.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/src/sys/clock.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/third_party/nanopb/pb_common.c"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/third_party/nanopb/pb_decode.c"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/third_party/nanopb/pb_encode.c"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/generated/meshtastic/admin.pb.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/generated/meshtastic/channel.pb.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/generated/meshtastic/config.pb.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/generated/meshtastic/connection_status.pb.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/generated/meshtastic/device_ui.pb.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/generated/meshtastic/localonly.pb.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/generated/meshtastic/mesh.pb.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/generated/meshtastic/module_config.pb.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/generated/meshtastic/portnums.pb.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/generated/meshtastic/telemetry.pb.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/generated/meshtastic/xmodem.pb.cpp")
    target_include_directories(trailmate_phone_core_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_phone/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_phone/tests"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/generated"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/third_party/nanopb"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include")
    target_compile_features(trailmate_phone_core_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_phone_core_smoke
             COMMAND trailmate_phone_core_smoke)

    add_executable(trailmate_key_verification_runtime_adapters_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_key_verification_runtime/tests/test_key_verification_runtime_adapters.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_key_verification_runtime/src/key_verification_action_sink.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_key_verification_runtime/src/key_verification_presentation_source.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/usecase/contact_service.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/src/sys/clock.cpp")
    target_include_directories(trailmate_key_verification_runtime_adapters_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_key_verification_runtime/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include")
    target_compile_features(trailmate_key_verification_runtime_adapters_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_key_verification_runtime_adapters_smoke
             COMMAND trailmate_key_verification_runtime_adapters_smoke)

    add_executable(trailmate_legacy_key_verification_adapters_alias_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_legacy_adapters/tests/test_legacy_key_verification_adapters_legacy_alias.cpp")
    target_include_directories(trailmate_legacy_key_verification_adapters_alias_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_legacy_adapters/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_key_verification_runtime/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
    target_compile_features(trailmate_legacy_key_verification_adapters_alias_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_legacy_key_verification_adapters_alias_smoke
             COMMAND trailmate_legacy_key_verification_adapters_alias_smoke)

    add_executable(trailmate_map_overlay_snapshot_source_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_map_runtime/tests/test_map_overlay_snapshot_source.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_map_runtime/src/map_overlay_snapshot_source.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_map_runtime/src/map_overlay_projection_adapter.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_map_runtime/src/map_overlay/map_overlay_projector.cpp")
    target_include_directories(trailmate_map_overlay_snapshot_source_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_map_runtime/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/include")
    target_compile_features(trailmate_map_overlay_snapshot_source_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_map_overlay_snapshot_source_smoke
             COMMAND trailmate_map_overlay_snapshot_source_smoke)

    add_executable(trailmate_legacy_map_overlay_source_alias_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_legacy_adapters/tests/test_legacy_map_overlay_source_legacy_alias.cpp")
    target_include_directories(trailmate_legacy_map_overlay_source_alias_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_legacy_adapters/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_map_runtime/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/include")
    target_compile_features(trailmate_legacy_map_overlay_source_alias_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_legacy_map_overlay_source_alias_smoke
             COMMAND trailmate_legacy_map_overlay_source_alias_smoke)

    add_executable(trailmate_ui_presentation_screen_binding_registry_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/tests/test_screen_binding_registry.cpp")
    target_link_libraries(trailmate_ui_presentation_screen_binding_registry_smoke
        PRIVATE trailmate_ui_lvgl_ux_packs)
    add_test(NAME trailmate_ui_presentation_screen_binding_registry_smoke
             COMMAND trailmate_ui_presentation_screen_binding_registry_smoke)

    add_executable(trailmate_timezone_profile_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/tests/test_timezone_profile.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/src/platform/ui/timezone_profile.cpp")
    target_include_directories(trailmate_timezone_profile_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include")
    target_compile_features(trailmate_timezone_profile_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_timezone_profile_smoke
             COMMAND trailmate_timezone_profile_smoke)

    add_executable(trailmate_battery_estimator_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/tests/test_battery_estimator.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/src/power/battery_estimator.cpp")
    target_include_directories(trailmate_battery_estimator_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include")
    target_compile_features(trailmate_battery_estimator_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_battery_estimator_smoke
             COMMAND trailmate_battery_estimator_smoke)

    add_executable(trailmate_team_presence_snapshot_codec_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_team_presence_and_snapshot_codec.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/team_presence/team_presence_model.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/team_persistence/team_ui_snapshot_codec.cpp")
    target_include_directories(trailmate_team_presence_snapshot_codec_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_team/include")
    target_compile_features(trailmate_team_presence_snapshot_codec_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_team_presence_snapshot_codec_smoke
             COMMAND trailmate_team_presence_snapshot_codec_smoke)

    add_executable(trailmate_team_mgmt_key_request_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_team/tests/test_team_mgmt_key_request.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_team/src/protocol/team_mgmt.cpp")
    target_include_directories(trailmate_team_mgmt_key_request_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_team/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
    target_compile_features(trailmate_team_mgmt_key_request_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_team_mgmt_key_request_smoke
             COMMAND trailmate_team_mgmt_key_request_smoke)

    add_executable(trailmate_team_app_data_poll_order_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/core_team/tests/test_team_app_data_poll_order.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/domain/chat_model.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/src/usecase/chat_service.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_team/src/protocol/team_chat.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_team/src/protocol/team_wire.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_team/src/protocol/team_waypoint.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_team/src/protocol/team_mgmt.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_team/src/usecase/team_service.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/src/sys/clock.cpp")
    target_include_directories(trailmate_team_app_data_poll_order_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_team/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include")
    target_compile_features(trailmate_team_app_data_poll_order_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_team_app_data_poll_order_smoke
             COMMAND trailmate_team_app_data_poll_order_smoke)

    add_executable(trailmate_team_page_read_model_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_team_page_read_model.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_read_model.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/team_presence/team_presence_model.cpp")
    target_include_directories(trailmate_team_page_read_model_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_team/include")
    target_compile_features(trailmate_team_page_read_model_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_team_page_read_model_smoke
             COMMAND trailmate_team_page_read_model_smoke)

    add_executable(trailmate_team_page_state_store_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_team_page_state_store.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_state_store.cpp")
    target_include_directories(trailmate_team_page_state_store_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_team/include")
    target_compile_features(trailmate_team_page_state_store_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_team_page_state_store_smoke
             COMMAND trailmate_team_page_state_store_smoke)

    add_executable(trailmate_contacts_team_snapshot_source_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_contacts_team_snapshot_source.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/contacts/contacts_team_snapshot_source.cpp")
    target_include_directories(trailmate_contacts_team_snapshot_source_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_team/include")
    target_compile_features(trailmate_contacts_team_snapshot_source_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_contacts_team_snapshot_source_smoke
             COMMAND trailmate_contacts_team_snapshot_source_smoke)

    add_executable(trailmate_team_map_overlay_source_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_team_map_overlay_source.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/presentation_sources/team_map_overlay_source.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/src/platform/ui/team_ui_store_runtime.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/src/platform/linux/runtime_paths.cpp")
    target_include_directories(trailmate_team_map_overlay_source_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_map_runtime/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_team/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include"
            "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/include")
    target_compile_features(trailmate_team_map_overlay_source_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_team_map_overlay_source_smoke
             COMMAND trailmate_team_map_overlay_source_smoke)

    add_executable(trailmate_team_page_event_reducer_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_team_page_event_reducer.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_event_reducer.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/team_presence/team_presence_model.cpp")
    target_include_directories(trailmate_team_page_event_reducer_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_team/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
    target_compile_features(trailmate_team_page_event_reducer_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_team_page_event_reducer_smoke
             COMMAND trailmate_team_page_event_reducer_smoke)

    add_executable(trailmate_team_page_command_reducer_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_team_page_command_reducer.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_command_reducer.cpp")
    target_include_directories(trailmate_team_page_command_reducer_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_team/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
    target_compile_features(trailmate_team_page_command_reducer_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_team_page_command_reducer_smoke
             COMMAND trailmate_team_page_command_reducer_smoke)

    add_executable(trailmate_team_page_key_event_log_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_team_page_key_event_log.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_key_event_log.cpp")
    target_include_directories(trailmate_team_page_key_event_log_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_team/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
    target_compile_features(trailmate_team_page_key_event_log_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_team_page_key_event_log_smoke
             COMMAND trailmate_team_page_key_event_log_smoke)

    add_executable(trailmate_team_page_runtime_port_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_team_page_runtime_port.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_runtime_port.cpp")
    target_include_directories(trailmate_team_page_runtime_port_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_team/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
    target_compile_features(trailmate_team_page_runtime_port_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_team_page_runtime_port_smoke
             COMMAND trailmate_team_page_runtime_port_smoke)

    add_executable(trailmate_team_page_activity_sink_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_team_page_activity_sink.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_activity_sink.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_team/src/protocol/team_chat.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_team/src/protocol/team_position.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_team/src/protocol/team_track.cpp")
    target_include_directories(trailmate_team_page_activity_sink_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_team/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
    target_compile_features(trailmate_team_page_activity_sink_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_team_page_activity_sink_smoke
             COMMAND trailmate_team_page_activity_sink_smoke)

    add_executable(trailmate_team_page_deferred_dispatch_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_team_page_deferred_dispatch.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_deferred_dispatch.cpp")
    target_include_directories(trailmate_team_page_deferred_dispatch_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_team/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
    target_compile_features(trailmate_team_page_deferred_dispatch_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_team_page_deferred_dispatch_smoke
             COMMAND trailmate_team_page_deferred_dispatch_smoke)

    add_executable(trailmate_team_page_kick_confirm_action_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_team_page_kick_confirm_action.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_kick_confirm_action.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_command_reducer.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_runtime_port.cpp")
    target_include_directories(trailmate_team_page_kick_confirm_action_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_team/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
    target_compile_features(trailmate_team_page_kick_confirm_action_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_team_page_kick_confirm_action_smoke
             COMMAND trailmate_team_page_kick_confirm_action_smoke)

    add_executable(trailmate_team_page_transfer_leader_action_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_team_page_transfer_leader_action.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_transfer_leader_action.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_command_reducer.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_key_event_log.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_runtime_port.cpp")
    target_include_directories(trailmate_team_page_transfer_leader_action_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_team/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
    target_compile_features(trailmate_team_page_transfer_leader_action_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_team_page_transfer_leader_action_smoke
             COMMAND trailmate_team_page_transfer_leader_action_smoke)

    add_executable(trailmate_team_page_create_team_action_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_team_page_create_team_action.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_create_team_action.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_command_reducer.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_key_event_log.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_runtime_port.cpp")
    target_include_directories(trailmate_team_page_create_team_action_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_team/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
    target_compile_features(trailmate_team_page_create_team_action_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_team_page_create_team_action_smoke
             COMMAND trailmate_team_page_create_team_action_smoke)

    add_executable(trailmate_team_page_pairing_command_action_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_team_page_pairing_command_action.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_pairing_command_action.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_command_reducer.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_runtime_port.cpp")
    target_include_directories(trailmate_team_page_pairing_command_action_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_team/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
    target_compile_features(trailmate_team_page_pairing_command_action_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_team_page_pairing_command_action_smoke
             COMMAND trailmate_team_page_pairing_command_action_smoke)

    add_executable(trailmate_team_page_event_effect_sink_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_team_page_event_effect_sink.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_event_effect_sink.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_event_reducer.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_key_event_log.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_runtime_port.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/team_presence/team_presence_model.cpp")
    target_include_directories(trailmate_team_page_event_effect_sink_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_team/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
    target_compile_features(trailmate_team_page_event_effect_sink_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_team_page_event_effect_sink_smoke
             COMMAND trailmate_team_page_event_effect_sink_smoke)

    add_executable(trailmate_team_page_request_keys_action_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_team_page_request_keys_action.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_request_keys_action.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_command_reducer.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_runtime_port.cpp")
    target_include_directories(trailmate_team_page_request_keys_action_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_team/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
    target_compile_features(trailmate_team_page_request_keys_action_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_team_page_request_keys_action_smoke
             COMMAND trailmate_team_page_request_keys_action_smoke)

    add_executable(trailmate_team_page_key_request_action_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_team_page_key_request_action.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_key_request_action.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_runtime_port.cpp")
    target_include_directories(trailmate_team_page_key_request_action_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_team/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_chat/include")
    target_compile_features(trailmate_team_page_key_request_action_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_team_page_key_request_action_smoke
             COMMAND trailmate_team_page_key_request_action_smoke)

    add_executable(trailmate_team_page_flow_controller_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_team_page_flow_controller.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/team/team_page_flow_controller.cpp")
    target_include_directories(trailmate_team_page_flow_controller_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_team/include")
    target_compile_features(trailmate_team_page_flow_controller_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_team_page_flow_controller_smoke
             COMMAND trailmate_team_page_flow_controller_smoke)

    add_executable(trailmate_team_chat_action_sink_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_team_chat_action_sink.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/presentation_sources/team_chat_action_sink.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/src/platform/ui/team_ui_store_runtime.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/src/platform/linux/runtime_paths.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/src/sys/clock.cpp")
    target_include_directories(trailmate_team_chat_action_sink_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_team/include"
            "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/include")
    target_compile_features(trailmate_team_chat_action_sink_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_team_chat_action_sink_smoke
             COMMAND trailmate_team_chat_action_sink_smoke)

    add_executable(trailmate_team_chat_presentation_source_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_team_chat_presentation_source.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/presentation_sources/team_chat_presentation_source.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/team_presentation/team_rich_payload_projector.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_team/src/protocol/team_chat.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_team/src/protocol/team_location_marker.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/src/platform/ui/team_ui_store_runtime.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/src/platform/linux/runtime_paths.cpp")
    target_include_directories(trailmate_team_chat_presentation_source_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_team/include"
            "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/include")
    target_compile_features(trailmate_team_chat_presentation_source_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_team_chat_presentation_source_smoke
             COMMAND trailmate_team_chat_presentation_source_smoke)

    add_executable(trailmate_chat_team_workflow_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_chat_team_workflow.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/screens/chat/chat_team_workflow.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/src/chat/chat_workspace_model.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_team/src/protocol/team_location_marker.cpp")
    target_include_directories(trailmate_chat_team_workflow_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_team/include")
    target_compile_features(trailmate_chat_team_workflow_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_chat_team_workflow_smoke
             COMMAND trailmate_chat_team_workflow_smoke)

    add_executable(trailmate_team_action_runtime_sink_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_team_action_runtime_sink.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/src/ui/team_actions/team_action_runtime_sink.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_team/src/protocol/team_chat.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_team/src/protocol/team_location_marker.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/src/platform/ui/team_ui_store_runtime.cpp"
        "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/src/platform/linux/runtime_paths.cpp"
        "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/src/sys/clock.cpp")
    target_include_directories(trailmate_team_action_runtime_sink_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_presentation/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_sys/include"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_team/include"
            "${TRAIL_MATE_REPO_ROOT}/platform/linux/common/include")
    target_compile_features(trailmate_team_action_runtime_sink_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_team_action_runtime_sink_smoke
             COMMAND trailmate_team_action_runtime_sink_smoke)

    add_executable(trailmate_team_action_types_smoke
        "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/tests/test_team_action_types.cpp")
    target_include_directories(trailmate_team_action_types_smoke
        PRIVATE
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_shared/include")
    target_compile_features(trailmate_team_action_types_smoke
        PRIVATE cxx_std_17)
    add_test(NAME trailmate_team_action_types_smoke
             COMMAND trailmate_team_action_types_smoke)

    foreach(gps_test IN ITEMS nmea_parser nmea_coordinate_validation)
        add_executable(trailmate_${gps_test}_smoke
            "${TRAIL_MATE_REPO_ROOT}/modules/core_gps/tests/test_${gps_test}.cpp"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_gps/src/protocol/nmea/nmea_parser.cpp"
            "${TRAIL_MATE_REPO_ROOT}/modules/core_gps/src/protocol/nmea/nmea_sentence.cpp")
        target_include_directories(trailmate_${gps_test}_smoke
            PRIVATE "${TRAIL_MATE_REPO_ROOT}/modules/core_gps/include")
        target_compile_features(trailmate_${gps_test}_smoke PRIVATE cxx_std_17)
        add_test(NAME trailmate_${gps_test}_smoke
                 COMMAND trailmate_${gps_test}_smoke)
    endforeach()

    foreach(ux_pack_test IN ITEMS
        screen_registry
        input_binding_set
        ux_screen_menu_adapter
        ux_menu_provider
        lvgl_menu_runtime_adapter
        compatibility_screen_factory
        lvgl_screen_host_adapter
        lvgl_screen_graph_bridge
        lvgl_runtime_screen_graph_presenter
        lvgl_runtime_entry_adoption
        lvgl_runtime_adoption_probe
        lvgl_primary_screen_graph_runtime
        lvgl_descriptor_menu_model
        lvgl_descriptor_renderer_probe
        compatibility_menu_binding
        cardputer_compact_ux_pack
        compatibility_ux_pack
        uconsole_desktop_ux_pack
        tiny_node_status_ux_pack
        simulator_full_ux_pack
        ux_pack_registry)
        add_executable(trailmate_ui_lvgl_ux_packs_${ux_pack_test}_smoke
            "${TRAIL_MATE_REPO_ROOT}/modules/ui_lvgl_ux_packs/tests/test_${ux_pack_test}.cpp")
        target_link_libraries(trailmate_ui_lvgl_ux_packs_${ux_pack_test}_smoke
            PRIVATE trailmate_ui_lvgl_ux_packs)
        add_test(NAME trailmate_ui_lvgl_ux_packs_${ux_pack_test}_smoke
                 COMMAND trailmate_ui_lvgl_ux_packs_${ux_pack_test}_smoke)
    endforeach()
endif()
