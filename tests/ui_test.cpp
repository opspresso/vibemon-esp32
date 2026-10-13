#include "presentation/dashboard_ui.h"
#include "fakes.h"

#include <filesystem>
#include <fstream>
#include <cstring>
#include <cmath>

namespace {
std::uint32_t timeMs = 0;
std::array<std::uint16_t, 480 * 480> pixels;
std::array<std::uint16_t, 480 * 480> drawBuffer, secondBuffer;
std::size_t flushes = 0;
lv_point_t position{};
bool pressed = false;
lv_indev_t* pointer = nullptr;

double luminance(std::uint32_t color) {
  // Verify contrast after the target panel's RGB565 quantization.
  const double channels[] = {static_cast<double>(((color >> 19) & 31) * 255 / 31),
                             static_cast<double>(((color >> 10) & 63) * 255 / 63),
                             static_cast<double>(((color >> 3) & 31) * 255 / 31)};
  const double weights[] = {0.2126, 0.7152, 0.0722};
  double result = 0;
  for (std::size_t i = 0; i < 3; ++i) {
    const double value = channels[i] / 255;
    result += weights[i] * (value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4));
  }
  return result;
}
double contrast(std::uint32_t first, std::uint32_t second) {
  const auto a = luminance(first), b = luminance(second);
  return (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05);
}

void advance(DashboardUi& ui, std::uint32_t duration = 80) {
  for (std::uint32_t elapsed = 0; elapsed < duration; elapsed += 10) {
    timeMs += 10; ui.update(timeMs); lv_timer_handler();
  }
}
void tap(DashboardUi& ui, const std::string& id) {
  auto* widget = ui.widget(id); CHECK(widget);
  lv_obj_update_layout(lv_screen_active());
  lv_area_t area; lv_obj_get_coords(widget, &area);
  CHECK(area.x1 >= 0 && area.y1 >= 0 && area.x2 < 480 && area.y2 < 480);
  position = {(area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2};
  pressed = true; lv_indev_read(pointer); advance(ui, 60);
  pressed = false; lv_indev_read(pointer); advance(ui, 80);
}
void tapAt(DashboardUi& ui, int x, int y) {
  position = {x, y}; pressed = true; lv_indev_read(pointer); advance(ui, 60);
  pressed = false; lv_indev_read(pointer); advance(ui, 80);
}
void screenshot(const std::string& name) {
  std::filesystem::create_directories("screenshots");
  std::ofstream file("screenshots/" + name + ".ppm", std::ios::binary);
  file << "P6\n480 480\n255\n";
  for (const auto pixel : pixels) {
    const char rgb[] = {static_cast<char>(((pixel >> 11) & 31) * 255 / 31), static_cast<char>(((pixel >> 5) & 63) * 255 / 63), static_cast<char>((pixel & 31) * 255 / 31)};
    file.write(rgb, 3);
  }
}
Catalog demo() {
  Catalog value;
  for (const auto& name : {"K3S demo", "DGX Spark", "EKS demo"}) {
    Monitor item;
    item.selection = {ItemKind::Resource, name == std::string("K3S demo") ? "k3s" : name == std::string("DGX Spark") ? "spark" : "eks"};
    item.name = name; item.category = item.selection.id == "spark" ? "DGX Spark" : "Kubernetes";
    item.health = item.selection.id == "k3s" ? Health::Warning : Health::Ok;
    item.ageMs = 1000;
    item.metrics = {{"cpu", "CPU", "%", 12.4, MetricDisplay::Bar, 0, 100}, {"memory", "Memory", "%", 68.2, MetricDisplay::Bar, 0, 100}, {"nodes", "Ready nodes", "", 4, MetricDisplay::Number, 0, {}}};
    value.items.push_back(item);
  }
  Monitor agent;
  agent.selection = {ItemKind::Account, "codex:work"}; agent.name = "Work account"; agent.category = "Codex";
  agent.health = Health::Ok; agent.ageMs = 1000; agent.activity = "working";
  agent.projects = {agent, agent};
  agent.metrics = {{"context", "Context", "%", 42, MetricDisplay::Bar, 0, 100}, {"session", "Session", "%", {}, MetricDisplay::Bar, 0, 100}, {"week", "Weekly", "%", 24, MetricDisplay::Bar, 0, 100}};
  value.items.push_back(agent);
  return value;
}
}  // namespace

int main() {
  try {
    lv_init();
    lv_tick_set_cb([] { return timeMs; });
    auto* display = lv_display_create(480, 480);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, drawBuffer.data(), secondBuffer.data(), sizeof(drawBuffer), LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(display, [](lv_display_t* display, const lv_area_t*, std::uint8_t* data) {
      if (lv_display_flush_is_last(display)) std::memcpy(pixels.data(), data, sizeof(pixels));
      ++flushes; lv_display_flush_ready(display);
    });
    pointer = lv_indev_create(); lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(pointer, [](lv_indev_t*, lv_indev_data_t* data) { data->point = position; data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED; });
    {
      Store store; configured(store); Wifi wifi; Cloud cloud; Controller app(store, wifi, cloud);
      app.begin(0); wifi.connected(); app.tick(1); cloud.complete(demo()); app.tick(2);
      timeMs = 2;
      DashboardUi ui(app); CHECK(ui.begin()); advance(ui); screenshot("dashboard");
      for (const auto color : {design::Foreground, design::Muted, design::Good, design::Warning, design::Danger, design::Accent}) {
        CHECK(contrast(color, design::Background) >= 4.5);
        CHECK(contrast(color, design::Surface) >= 4.5);
      }
      CHECK(contrast(design::AccentForeground, design::Accent) >= 4.5);
      const auto bg = design::Background;
      CHECK(pixels.back() == (((bg >> 19) & 31) << 11 | ((bg >> 10) & 63) << 5 | ((bg >> 3) & 31)));
      CHECK(ui.page() == Page::Dashboard);
      for (std::size_t i = 0; i < PanelCount; ++i) {
        auto* panel = ui.widget("panel-" + std::to_string(i)); CHECK(panel);
        CHECK(lv_obj_get_width(panel) == 216 && lv_obj_get_height(panel) == 174);
      }
      // Once the fixed dashboard is drawn, repeated unchanged updates must not repaint it.
      advance(ui, 250); const auto stable = flushes; advance(ui, 1000); CHECK(flushes == stable);
      tap(ui, "panel-3"); CHECK(ui.page() == Page::Picker); screenshot("picker");
      tap(ui, "filter-2"); CHECK(ui.widget("item-0") && !ui.widget("item-1"));
      tap(ui, "item-0"); CHECK(ui.page() == Page::Dashboard && app.workspace().panels[3].id == "codex:work");
      tap(ui, "panel-0"); tap(ui, "clear-panel"); CHECK(app.workspace().panels[0].id.empty());
      screenshot("empty-panel");
      tap(ui, "settings"); CHECK(ui.page() == Page::Settings); screenshot("settings");
      tap(ui, "cloud-settings"); CHECK(ui.page() == Page::CloudForm);
      CHECK(std::string(lv_textarea_get_text(ui.widget("read-token"))).empty());
      CHECK(lv_textarea_get_password_mode(ui.widget("read-token")));
      lv_area_t keyboardBounds; lv_obj_get_coords(ui.widget("keyboard"), &keyboardBounds);
      CHECK(keyboardBounds.x1 == 0 && keyboardBounds.y1 == 254 && keyboardBounds.x2 == 479 && keyboardBounds.y2 == 479);
      screenshot("cloud-setup");
      tapAt(ui, 25, 290);
      CHECK(std::string(lv_textarea_get_text(ui.widget("read-token"))) == "1");
      lv_textarea_set_text(ui.widget("read-token"), "invalid"); tap(ui, "connect-cloud");
      CHECK(ui.page() == Page::CloudForm && !app.changingCloud()); screenshot("invalid-token");
      ui.wifi(); wifi.found = {{"Studio Wi-Fi", -45, true}, {"한글 네트워크", -60, true}, {"Guest", -75, false}};
      advance(ui, 300); screenshot("wifi-networks");
      wifi.scanError = true; advance(ui, 300);
      CHECK(std::string(lv_label_get_text(ui.widget("scan-status"))).find("Scan failed") == 0);
      wifi.scanError = false; advance(ui, 300);
      tap(ui, "network-0"); CHECK(ui.page() == Page::WifiForm); screenshot("wifi-password");
      CHECK(std::string(lv_textarea_get_text(ui.widget("ssid"))) == "Studio Wi-Fi");
      tapAt(ui, 64, 280);
      CHECK(std::string(lv_textarea_get_text(ui.widget("wifi-password"))) == "q");
      lv_textarea_set_text(ui.widget("wifi-password"), "12345678"); tap(ui, "join-wifi");
      CHECK(app.changingWifi() && wifi.requested == "Studio Wi-Fi");
      wifi.current.link = WifiLink::Failed; app.tick(timeMs); advance(ui); screenshot("wifi-failed");
      CHECK(ui.page() == Page::WifiForm);
      tap(ui, "back"); CHECK(!app.changingWifi() && wifi.requested == "Workshop");
      ui.dashboard(); advance(ui, 130000); screenshot("stale-dashboard");
      const auto* accountStatus = lv_label_get_text(ui.widget("panel-status-3"));
      CHECK(std::string(accountStatus) == "Inactive · 2 projects");
      const auto inactiveColor = lv_obj_get_style_text_color(ui.widget("panel-status-3"), LV_PART_MAIN);
      const auto staleColor = lv_obj_get_style_text_color(ui.widget("panel-status-2"), LV_PART_MAIN);
      CHECK(!lv_color_eq(inactiveColor, staleColor));
    }
    lv_deinit();
    std::cout << "PASS real LVGL UI: four cards, touch selection, category filtering, persistence, setup forms, Unicode, missing values, offline display and idle redraw budget\n";
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
