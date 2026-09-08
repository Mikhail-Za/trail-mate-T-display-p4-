#pragma once

#include "chat/domain/chat_types.h"

#include <cstddef>
#include <cstdint>

namespace platform::esp::radio {

bool initializePacketIds(uint32_t node, const chat::MeshConfig& config);
bool allocatePacketId(uint32_t node, const uint8_t* psk, size_t psk_len, uint32_t& out);

} // namespace platform::esp::radio
