#pragma once
#include "../application/controller.h"
#include <array>
#include <functional>

namespace vibemon {
class EspDisplay;
class EspUpdate;
// The screen that USB commands report and open. The sketch connects it to the UI.
struct ConsoleScreen {
  std::function<int()> page;
  std::function<void()> showDashboard;
};
// USB setup uses the same validation and verified persistence as the touchscreen.
// Commands and stored secrets are never echoed to the serial output.
class SerialConsole {
 public:
  SerialConsole(Controller& controller, ConsoleScreen screen, EspDisplay& display, EspUpdate& update)
      : controller_(controller), screen_(std::move(screen)), display_(display), update_(update) {}
  void tick();
 private:
  Controller& controller_;
  ConsoleScreen screen_;
  EspDisplay& display_;
  EspUpdate& update_;
  std::array<char, 1024> input_{};
  std::size_t used_ = 0;
  bool overflow_ = false;
  void command();
};
}  // namespace vibemon
