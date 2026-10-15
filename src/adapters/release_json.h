#pragma once
#include "../domain/firmware.h"

namespace vibemon {
constexpr std::size_t MaxReleaseBytes = 2048;
// Reads ota.json: {"board","version","url","size","sha256"}. Rejects other boards.
bool decodeRelease(std::string_view json, FirmwareRelease& output);
}  // namespace vibemon
