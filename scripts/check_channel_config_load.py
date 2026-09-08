#!/usr/bin/env python3
"""Compile the production channel loader body with observable host NVS stubs."""

from pathlib import Path
import resource
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
FACTORY = (ROOT / "platform/esp/idf_common/src/idf_chat_factory.cpp").read_text()
FACADE = (ROOT / "platform/esp/idf_common/src/idf_chat_facade.cpp").read_text()
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))


def block(source, marker):
    start = source.index(marker)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end] + (";" if source[start:].startswith("class ") else "")


store = block(FACTORY, "class NvsChannelBlobStore final")
loader = block(FACTORY, "bool loadChannelConfigFromNvs(app::AppConfig& config)")

initialize = block(FACADE, "bool IdfChatFacade::initialize()")
load_pos = initialize.index("loadChannelConfigFromNvs(config_)")
runtime_pos = initialize.index("createIdfChatRuntime(config_, lora_board_)")
guard_end = initialize.index("// Create the global EventBus", load_pos)
guard = initialize[initialize.index("{") + 1:guard_end]
assert "const bool channel_config_loaded = loadChannelConfigFromNvs(config_);" in guard
assert "config_.mesh_protocol == ::chat::MeshProtocol::Meshtastic" in guard
assert "return false;" in initialize[load_pos:runtime_pos]
assert load_pos < runtime_pos

HARNESS = r'''
#include "chat/domain/channel_persist.h"
#include "chat/domain/chat_types.h"
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <iostream>
#include <vector>

using esp_err_t = int;
using nvs_handle_t = unsigned;
enum nvs_open_mode_t { NVS_READONLY, NVS_READWRITE };
constexpr esp_err_t ESP_OK = 0;
constexpr esp_err_t ESP_ERR_NVS_NOT_FOUND = 1;
constexpr esp_err_t ESP_FAIL = 2;

namespace fixture {
std::vector<uint8_t> blob;
esp_err_t open_result = ESP_OK;
esp_err_t query_result = ESP_OK;
esp_err_t read_result = ESP_OK;
bool key_missing = false;
int opens = 0;
int closes = 0;
int reads = 0;
int writes = 0;

void reset(const std::vector<uint8_t>& value = {}) {
    blob = value;
    open_result = query_result = read_result = ESP_OK;
    key_missing = false;
    opens = closes = reads = writes = 0;
}
}

esp_err_t nvs_open(const char*, nvs_open_mode_t mode, nvs_handle_t* handle) {
    assert(mode == NVS_READONLY);
    if (fixture::open_result != ESP_OK) return fixture::open_result;
    *handle = 1;
    ++fixture::opens;
    return ESP_OK;
}

esp_err_t nvs_get_blob(nvs_handle_t, const char*, void* out, size_t* len) {
    if (fixture::key_missing) return ESP_ERR_NVS_NOT_FOUND;
    if (!out) {
        if (fixture::query_result != ESP_OK) return fixture::query_result;
        *len = fixture::blob.size();
        return ESP_OK;
    }
    ++fixture::reads;
    if (fixture::read_result != ESP_OK) return fixture::read_result;
    if (*len < fixture::blob.size()) return ESP_FAIL;
    std::memcpy(out, fixture::blob.data(), fixture::blob.size());
    *len = fixture::blob.size();
    return ESP_OK;
}

void nvs_close(nvs_handle_t) { ++fixture::closes; }

namespace platform::ui::settings_store {
bool put_blob(const char*, const char*, const uint8_t*, size_t) {
    ++fixture::writes;
    return true;
}
}

namespace app {
struct AppConfig {
    chat::MeshConfig meshtastic_config{};
    chat::MeshProtocol mesh_protocol = chat::MeshProtocol::Meshtastic;
    bool channel_enabled[chat::kMaxChannels]{};
    bool primary_enabled = false;
    bool secondary_enabled = false;
};
}

namespace platform::esp::idf_common {
STORE
LOADER

#define ESP_LOGE(...) ((void)0)
struct FacadeGuardHarness {
    bool initialized_ = false;
    app::AppConfig config_{};
    bool run() {
GUARD
        return true;
    }
};
#undef ESP_LOGE
}

using platform::esp::idf_common::loadChannelConfigFromNvs;

std::vector<uint8_t> validBlob(size_t count = chat::kMaxChannels) {
    chat::ChannelRecord records[chat::kMaxChannels]{};
    for (size_t i = 0; i < chat::kMaxChannels; ++i) {
        records[i].enabled = (i % 2) == 0;
        std::snprintf(records[i].name, sizeof(records[i].name), "slot%zu", i);
        records[i].key_len = 16;
        std::memset(records[i].key, static_cast<int>(0x30 + i), records[i].key_len);
        records[i].channel_id = static_cast<uint32_t>(100 + i);
    }
    return chat::channel_persist::encode(records, count);
}

app::AppConfig markedConfig() {
    app::AppConfig config;
    config.primary_enabled = true;
    config.secondary_enabled = true;
    std::memset(config.meshtastic_config.channels[3].key, 0xA5, 32);
    config.meshtastic_config.channels[3].key_len = 32;
    config.meshtastic_config.channels[3].channel_id = 0xA5A5A5A5;
    return config;
}

void assertUnchangedOnError(const std::vector<uint8_t>& blob) {
    fixture::reset(blob);
    app::AppConfig config = markedConfig();
    const app::AppConfig before = config;
    assert(!loadChannelConfigFromNvs(config));
    assert(std::memcmp(&config, &before, sizeof(config)) == 0);
    assert(fixture::writes == 0 && fixture::opens == fixture::closes);
}

int main() {
    // Missing namespace and missing key are true absence: migrate scalar defaults.
    fixture::reset();
    fixture::open_result = ESP_ERR_NVS_NOT_FOUND;
    app::AppConfig absent = markedConfig();
    assert(loadChannelConfigFromNvs(absent));
    assert(absent.meshtastic_config.channels[0].enabled);
    assert(absent.meshtastic_config.channels[1].enabled);
    assert(fixture::writes == 0 && fixture::opens == fixture::closes);

    // The private store rejects invalid output arguments before decoding.
    fixture::reset(validBlob());
    platform::esp::idf_common::NvsChannelBlobStore store;
    chat::ChannelRecord too_small[chat::kMaxChannels - 1]{};
    assert(store.load(nullptr, chat::kMaxChannels) == -1);
    assert(store.load(too_small, chat::kMaxChannels - 1) == -1);
    assert(fixture::writes == 0 && fixture::opens == fixture::closes);

    fixture::reset();
    fixture::key_missing = true;
    app::AppConfig missing_key = markedConfig();
    assert(loadChannelConfigFromNvs(missing_key));
    assert(fixture::writes == 0 && fixture::opens == fixture::closes);

    // A full eight-slot record preserves disabled slots and refreshes mirrors.
    fixture::reset(validBlob());
    app::AppConfig loaded;
    assert(loadChannelConfigFromNvs(loaded));
    for (size_t i = 0; i < chat::kMaxChannels; ++i) {
        assert(loaded.meshtastic_config.channels[i].enabled == ((i % 2) == 0));
        assert(loaded.channel_enabled[i] == ((i % 2) == 0));
        assert(loaded.meshtastic_config.channels[i].key_len == 16);
        assert(loaded.meshtastic_config.channels[i].key[0] == 0x30 + i);
    }
    assert(loaded.primary_enabled && !loaded.secondary_enabled);
    assert(loaded.meshtastic_config.primary_channel_id == 100);
    assert(loaded.meshtastic_config.secondary_channel_id == 101);
    assert(fixture::writes == 0 && fixture::opens == fixture::closes);

    fixture::reset(validBlob());
    fixture::open_result = ESP_FAIL;
    app::AppConfig open_error = markedConfig();
    const app::AppConfig open_before = open_error;
    assert(!loadChannelConfigFromNvs(open_error));
    assert(std::memcmp(&open_error, &open_before, sizeof(open_error)) == 0);
    assert(fixture::opens == 0 && fixture::closes == 0 && fixture::writes == 0);

    fixture::reset(validBlob());
    fixture::query_result = ESP_FAIL;
    app::AppConfig query_error = markedConfig();
    const app::AppConfig query_before = query_error;
    assert(!loadChannelConfigFromNvs(query_error));
    assert(std::memcmp(&query_error, &query_before, sizeof(query_error)) == 0);
    assert(fixture::opens == fixture::closes && fixture::writes == 0);

    fixture::reset(validBlob());
    fixture::read_result = ESP_FAIL;
    app::AppConfig read_error = markedConfig();
    const app::AppConfig read_before = read_error;
    assert(!loadChannelConfigFromNvs(read_error));
    assert(std::memcmp(&read_error, &read_before, sizeof(read_error)) == 0);
    assert(fixture::opens == fixture::closes && fixture::writes == 0);

    auto bad_magic = validBlob(); bad_magic[0] = 'X'; assertUnchangedOnError(bad_magic);
    auto bad_version = validBlob(); bad_version[3] = 2; assertUnchangedOnError(bad_version);
    std::vector<uint8_t> count_zero{'T','M','H',1,0}; assertUnchangedOnError(count_zero);
    auto count_nine = validBlob(); count_nine[4] = 9; assertUnchangedOnError(count_nine);
    auto oversized = validBlob(); oversized.push_back(0); assertUnchangedOnError(oversized);
    auto truncated = validBlob(); truncated.pop_back(); assertUnchangedOnError(truncated);
    auto trailing = validBlob(1); trailing.push_back(0); assertUnchangedOnError(trailing);

    // Execute the exact pre-runtime facade guard: Meshtastic fails closed, while
    // MeshCore still loads valid stored channels and tolerates an unrelated error.
    fixture::reset(validBlob());
    platform::esp::idf_common::FacadeGuardHarness meshcore_valid;
    meshcore_valid.config_.mesh_protocol = chat::MeshProtocol::MeshCore;
    assert(meshcore_valid.run());
    assert(meshcore_valid.config_.meshtastic_config.channels[7].channel_id == 107);

    fixture::reset(validBlob());
    fixture::read_result = ESP_FAIL;
    platform::esp::idf_common::FacadeGuardHarness meshcore_error;
    meshcore_error.config_.mesh_protocol = chat::MeshProtocol::MeshCore;
    assert(meshcore_error.run());

    fixture::reset(validBlob());
    fixture::read_result = ESP_FAIL;
    platform::esp::idf_common::FacadeGuardHarness meshtastic_error;
    assert(!meshtastic_error.run());

    std::cout << "channel config production loader PASS\n";
}
'''

with tempfile.TemporaryDirectory() as directory:
    temp = Path(directory)
    source = (HARNESS.replace("STORE", store).replace("LOADER", loader)
              .replace("GUARD", guard))
    (temp / "test.cpp").write_text(source)
    subprocess.run([
        "g++", "-std=c++17", "-Wall", "-Wextra", "-Werror", "-fuse-ld=bfd",
        "-I" + str(ROOT / "modules/core_chat/include"),
        str(temp / "test.cpp"),
        str(ROOT / "modules/core_chat/src/domain/channel_persist.cpp"),
        "-o", str(temp / "test"),
    ], check=True)
    subprocess.run([str(temp / "test")], check=True)

print("facade production guard Meshtastic/MeshCore behavior PASS")
