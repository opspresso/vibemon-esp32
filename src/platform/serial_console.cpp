#include "serial_console.h"
#include "esp32_display.h"
#include "esp32_update.h"
#include "../version.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <algorithm>
#include <esp_heap_caps.h>

namespace vibemon {
void SerialConsole::tick() {
  // Bound work per loop so a serial sender cannot monopolize touch processing.
  for (unsigned i = 0; i < 256 && Serial.available(); ++i) {
    const char value = static_cast<char>(Serial.read());
    if (value == '\n') {
      if (overflow_) Serial.println("{\"ok\":false,\"error\":\"Command too long\"}");
      else if (used_) command();
      input_.fill(0); used_ = 0; overflow_ = false;
    } else if (value != '\r') {
      if (used_ + 1 < input_.size()) input_[used_++] = value;
      else overflow_ = true;
    }
  }
}

void SerialConsole::command() {
  JsonDocument request;
  JsonDocument response;
  if (deserializeJson(request, input_.data(), used_, DeserializationOption::NestingLimit(3))) {
    response["ok"] = false; response["error"] = "Invalid JSON command";
  } else {
    if (request["requestId"].is<std::uint32_t>()) response["requestId"] = request["requestId"].as<std::uint32_t>();
    const auto name = request["command"].as<std::string>();
    if (name == "info") {
      const auto& workspace = controller_.workspace();
      response["ok"] = true;
      response["version"] = VIBEMON_VERSION;
      response["board"] = "ESP32-4848S040";
      response["width"] = 480; response["height"] = 480;
      response["internalHeapFree"] = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
      response["internalHeapMinimum"] = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
      response["psramFree"] = ESP.getFreePsram();
      response["connection"] = static_cast<int>(workspace.connection);
      response["message"] = workspace.message;
      response["page"] = screen_.page();
      response["items"] = workspace.catalog.items.size();
      response["hasSnapshot"] = workspace.hasSnapshot;
      response["tokenConfigured"] = workspace.tokenConfigured;
      response["touchPresses"] = display_.touchPresses();
      response["displayFrames"] = display_.completedFrames();
      response["displayRedraws"] = display_.redraws();
      response["renderedPixels"] = display_.renderedPixels();
      response["displayClockHz"] = display_.pixelClock();
      response["firmwareSlot"] = update_.runningSlot();
      response["firmwareState"] = static_cast<int>(workspace.firmware.state);
      response["firmwareAvailable"] = workspace.firmware.available;
      response["firmwareProgress"] = workspace.firmware.progress;
      response["panelsSelected"] = std::count_if(workspace.panels.begin(), workspace.panels.end(), [](const Selection& selection) { return !selection.id.empty(); });
    } else if (name == "display-clock") {
      response["ok"] = request["hz"].is<std::uint32_t>() && display_.setPixelClock(request["hz"].as<std::uint32_t>());
      if (!response["ok"].as<bool>()) response["error"] = "Use 8, 12, 16, 20 or 26 MHz for this diagnostic";
    } else if (name == "configure-wifi" || name == "configure-cloud") {
      const char* first = name == "configure-wifi" ? "ssid" : "origin";
      const char* secret = name == "configure-wifi" ? "password" : "token";
      if (!request[first].is<const char*>() || !request[secret].is<const char*>()) {
        response["ok"] = false; response["error"] = "Missing setup fields";
      } else {
        const auto result = name == "configure-wifi" ? controller_.saveWifi(request[first].as<std::string>(), request[secret].as<std::string>()) :
            controller_.saveCloud(request[first].as<std::string>(), request[secret].as<std::string>());
        response["ok"] = result.ok;
        if (result.ok) { response["queued"] = true; screen_.showDashboard(); }
        else response["error"] = result.message;
      }
    } else if (name == "dashboard") {
      screen_.showDashboard(); response["ok"] = true;
    } else if (name == "refresh") {
      controller_.refresh(); response["ok"] = true;
    } else if (name == "update-check") {
      controller_.checkFirmware(); response["ok"] = true;
    } else { response["ok"] = false; response["error"] = "Unknown command"; }
  }
  serializeJson(response, Serial); Serial.println();
}
}  // namespace vibemon
