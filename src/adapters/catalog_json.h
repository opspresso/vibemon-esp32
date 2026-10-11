#pragma once

#include "../domain/model.h"
#include <ArduinoJson.h>

namespace vibemon {
// A successful decode replaces the complete catalog. Failure preserves the previous one.
Validation decodeCatalog(const std::string& body, Catalog& output, ArduinoJson::Allocator* allocator = nullptr);
}  // namespace vibemon
