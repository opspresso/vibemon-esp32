#pragma once

#include "../domain/model.h"
#include <ArduinoJson.h>
#include <string_view>

namespace vibemon {
// A successful decode replaces the complete catalog. Failure preserves the previous one.
Validation decodeCatalog(std::string_view body, Catalog& output, ArduinoJson::Allocator* allocator = nullptr);
}  // namespace vibemon
