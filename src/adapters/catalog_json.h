#pragma once

#include "../domain/model.h"
#include <ArduinoJson.h>
#include <string_view>

namespace vibemon {
struct CatalogDecode {
  bool ok = false;
  // The response is valid JSON but exceeds a device limit.
  bool exceedsLimit = false;
  std::string message;
};
// A successful decode replaces the complete catalog. Failure preserves the
// previous one and states the cause without echoing response content.
CatalogDecode decodeCatalog(std::string_view body, Catalog& output, ArduinoJson::Allocator* allocator = nullptr);
}  // namespace vibemon
