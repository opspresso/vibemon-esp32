#pragma once
#include "../application/controller.h"
#include "../presentation/dashboard_ui.h"
#include <array>

namespace vibemon {
class EspDisplay;
class EspUpdate;
// USB setup uses the same validation and verified persistence as the touchscreen.
// Commands and stored secrets are never echoed to the serial output.
class SerialConsole {
 public:
  SerialConsole(Controller& controller, DashboardUi& ui, EspDisplay& display, EspUpdate& update)
      : controller_(controller), ui_(ui), display_(display), update_(update) {}
  void tick();
 private:
  Controller& controller_;
  DashboardUi& ui_;
  EspDisplay& display_;
  EspUpdate& update_;
  std::array<char, 1024> input_{};
  std::size_t used_ = 0;
  bool overflow_ = false;
  void command();
};
}  // namespace vibemon
