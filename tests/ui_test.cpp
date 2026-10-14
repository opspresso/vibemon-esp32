#include "presentation/dashboard_ui.h"
#include "fakes.h"

#include <filesystem>
#include <fstream>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace {
std::uint32_t timeMs = 0;
std::array<std::uint16_t, 480 * 480> pixels;
std::array<std::uint16_t, 480 * 480> drawBuffer, secondBuffer;
std::size_t flushes = 0;
std::size_t flushedPixels = 0;
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
void swipeUp(DashboardUi& ui) {
  position = {400, 360}; pressed = true; lv_indev_read(pointer); advance(ui, 40);
  for (int y = 350; y >= 150; y -= 10) { position.y = y; lv_indev_read(pointer); advance(ui, 10); }
  pressed = false; lv_indev_read(pointer); advance(ui, 300);
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
void labels(lv_obj_t* parent, std::vector<lv_obj_t*>& result) {
  if (lv_obj_check_type(parent, &lv_label_class)) result.push_back(parent);
  for (std::uint32_t i = 0; i < lv_obj_get_child_count(parent); ++i) labels(lv_obj_get_child(parent, i), result);
}
lv_obj_t* findText(lv_obj_t* parent, const std::string& text) {
  std::vector<lv_obj_t*> values; labels(parent, values);
  for (auto* value : values) if (text == lv_label_get_text(value)) return value;
  return nullptr;
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
  agent.selection = {ItemKind::Account, "codex:work"}; agent.name = "Work account"; agent.category = "codex · work";
  agent.health = Health::Ok; agent.ageMs = 1000; agent.activity = "working";
  agent.sourceId = "current"; agent.project = "web-console";
  auto review = static_cast<ReportState>(agent); review.sourceId = "review"; review.project = "release-review"; review.activity = "notification";
  agent.projects = {agent, review};
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
    lv_display_set_flush_cb(display, [](lv_display_t* display, const lv_area_t* area, std::uint8_t* data) {
      if (lv_display_flush_is_last(display)) std::memcpy(pixels.data(), data, sizeof(pixels));
      flushedPixels += static_cast<std::size_t>(lv_area_get_width(area) * lv_area_get_height(area));
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
      lv_font_glyph_dsc_t glyph{};
      CHECK(!lv_font_get_glyph_dsc(&lv_font_montserrat_12, &glyph, 0xB7, 0) || glyph.is_placeholder);
      const auto* categoryFont = lv_obj_get_style_text_font(ui.widget("panel-category-1"), LV_PART_MAIN);
      CHECK(lv_font_get_glyph_dsc(categoryFont, &glyph, 0xB7, 0) && !glyph.is_placeholder);
      CHECK(lv_font_get_glyph_dsc(categoryFont, &glyph, 0xD55C, 0) && !glyph.is_placeholder);
      // Animated lights must refresh bounded regions, not the whole dashboard.
      advance(ui, 250); const auto stable = flushes; const auto pixelsBefore = flushedPixels;
      advance(ui, 1000); CHECK(flushes > stable);
      CHECK(flushedPixels - pixelsBefore < 480 * 480 * 4);
      tap(ui, "panel-3"); CHECK(ui.page() == Page::Details); screenshot("resource-details");
      tap(ui, "change-item"); CHECK(ui.page() == Page::Picker); screenshot("picker");
      tap(ui, "filter-2"); CHECK(ui.widget("item-0") && !ui.widget("item-1"));
      tap(ui, "item-0"); CHECK(ui.page() == Page::Dashboard && app.workspace().panels[3].id == "codex:work");
      CHECK(std::string(lv_label_get_text(ui.widget("panel-name-3"))) == "release-review");
      CHECK(lv_obj_get_style_outline_width(ui.widget("panel-3"), LV_PART_MAIN) == 1);
      CHECK(lv_obj_get_style_shadow_width(ui.widget("panel-3"), LV_PART_MAIN) == 20);
      lv_area_t activeTitleBounds; lv_obj_get_coords(ui.widget("panel-name-3"), &activeTitleBounds);
      CHECK(lv_color_eq(lv_obj_get_style_border_color(ui.widget("panel-3"), LV_PART_MAIN), lv_obj_get_style_text_color(ui.widget("panel-status-3"), LV_PART_MAIN)));
      tap(ui, "panel-0"); tap(ui, "change-item"); tap(ui, "clear-panel"); CHECK(app.workspace().panels[0].id.empty());
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
      ui.dashboard(); advance(ui, 130000);
      // Resources are stale; coding projects expire later on their event clock (working -> idle -> sleep).
      timeMs += 470000; advance(ui); screenshot("stale-dashboard");
      const auto* accountStatus = lv_label_get_text(ui.widget("panel-status-3"));
      CHECK(std::string(accountStatus) == "Inactive · 2 projects");
      CHECK(std::string(lv_label_get_text(ui.widget("panel-name-3"))) == "web-console");
      CHECK(lv_obj_get_style_border_width(ui.widget("panel-3"), LV_PART_MAIN) == 1);
      CHECK(lv_obj_get_style_outline_width(ui.widget("panel-3"), LV_PART_MAIN) == 0);
      CHECK(lv_obj_get_style_shadow_width(ui.widget("panel-3"), LV_PART_MAIN) == 0);
      lv_area_t inactiveTitleBounds; lv_obj_get_coords(ui.widget("panel-name-3"), &inactiveTitleBounds);
      CHECK(activeTitleBounds.x1 == inactiveTitleBounds.x1 && activeTitleBounds.y1 == inactiveTitleBounds.y1);
      CHECK(lv_color_eq(lv_obj_get_style_border_color(ui.widget("panel-3"), LV_PART_MAIN), lv_obj_get_style_border_color(ui.widget("panel-2"), LV_PART_MAIN)));
      const auto inactiveColor = lv_obj_get_style_text_color(ui.widget("panel-status-3"), LV_PART_MAIN);
      const auto staleColor = lv_obj_get_style_text_color(ui.widget("panel-status-2"), LV_PART_MAIN);
      CHECK(!lv_color_eq(inactiveColor, staleColor));
      advance(ui, 500); const auto inactiveFlushes = flushes; advance(ui, 1000);
      CHECK(flushes == inactiveFlushes);
    }
    {
      Store store; configured(store); Wifi wifi; Cloud cloud; Controller app(store, wifi, cloud);
      app.begin(timeMs); wifi.connected(); app.tick(timeMs);
      auto catalog = demo();
      auto& resource = catalog.items[0];
      resource.name.clear(); for (int i = 0; i < 40; ++i) resource.name += "긴이름";
      resource.statusMessage = "A long status explanation stays available with every reading.";
      resource.metrics.clear();
      std::string longLabel; for (int i = 0; i < 24; ++i) longLabel += "메트릭";
      for (int i = 0; i < 32; ++i) resource.metrics.push_back({"metric" + std::to_string(i), longLabel + std::to_string(i), "requests/second!", 9007199254740991.0, MetricDisplay::Number, 0, {}});
      cloud.complete(catalog); app.tick(++timeMs);
      DashboardUi ui(app); CHECK(ui.begin()); advance(ui); screenshot("long-dashboard");
      tap(ui, "panel-0"); CHECK(ui.page() == Page::Details);
      auto* list = ui.widget("detail-list");
      CHECK(findText(list, resource.name)); CHECK(findText(list, resource.statusMessage));
      CHECK(findText(list, "9,007,199,254,740,991 requests/second!"));
      screenshot("long-details");
      std::vector<lv_obj_t*> text; labels(list, text);
      CHECK(text.size() >= 68);
      for (auto* value : text) {
        CHECK(lv_label_get_long_mode(value) == LV_LABEL_LONG_MODE_WRAP);
        CHECK(lv_obj_get_width(value) <= 416);
        lv_area_t bounds; lv_obj_get_coords(value, &bounds);
        CHECK(bounds.x1 >= 32 && bounds.x2 <= 447);
        lv_point_t measured;
        lv_text_get_size(&measured, lv_label_get_text(value), lv_obj_get_style_text_font(value, LV_PART_MAIN),
            lv_obj_get_style_text_letter_space(value, LV_PART_MAIN), lv_obj_get_style_text_line_space(value, LV_PART_MAIN),
            lv_obj_get_content_width(value), LV_TEXT_FLAG_NONE);
        CHECK(measured.x <= lv_obj_get_content_width(value) && measured.y <= lv_obj_get_content_height(value));
      }
      swipeUp(ui); CHECK(lv_obj_get_scroll_y(list) > 0);
      for (int wait = 0; wait < 3000 && lv_obj_is_scrolling(list); wait += 10) advance(ui, 10);
      CHECK(!lv_obj_is_scrolling(list));
      lv_obj_scroll_to_y(list, lv_obj_get_scroll_y(list) + lv_obj_get_scroll_bottom(list), LV_ANIM_OFF);
      advance(ui); screenshot("long-details-end");
      auto* last = findText(list, longLabel + "31"); CHECK(last);
      lv_area_t bounds; lv_obj_get_coords(last, &bounds); CHECK(bounds.y1 >= 70 && bounds.y2 < 404);
      const auto offset = lv_obj_get_scroll_y(list);
      app.refresh(); app.tick(++timeMs); catalog.items[0].metrics.back().value = 0;
      cloud.complete(catalog); app.tick(++timeMs); advance(ui);
      CHECK(findText(list, "0 requests/second!")); CHECK(lv_obj_get_scroll_y(list) == offset);
      advance(ui, 300); const auto stable = flushes; advance(ui, 1000); CHECK(flushes == stable);
      tap(ui, "back"); tap(ui, "panel-1");
      list = ui.widget("detail-list");
      CHECK(findText(list, "web-console") && findText(list, "release-review"));
      CHECK(findText(list, "Working") && findText(list, "Needs input"));
      screenshot("account-details");
      lv_obj_scroll_to_view_recursive(findText(list, "Needs input"), LV_ANIM_OFF); advance(ui);
      screenshot("account-details-end");
    }
    for (const std::string state : {"working", "thinking", "planning", "packing", "notification", "alert", "done", "idle", "inactive"}) {
      Store store; configured(store);
      auto account = demo().items.back();
      account.activity = state == "inactive" ? "working" : state;
      if (state == "inactive") account.activityAgeMs = 600000;
      account.projects = {account};
      Catalog catalog; catalog.items = {account};
      store.saved.panels.fill(account.selection); store.saved.panelsInitialized = true;
      Wifi wifi; Cloud cloud; Controller app(store, wifi, cloud);
      app.begin(timeMs); wifi.connected(); app.tick(timeMs); cloud.complete(catalog); app.tick(++timeMs);
      DashboardUi ui(app); CHECK(ui.begin()); advance(ui, 250);
      auto* light = ui.widget("panel-light-0-0"); CHECK(light);
      const bool active = state != "idle" && state != "inactive";
      CHECK(lv_obj_is_hidden(light) != active);
      const auto x = lv_obj_get_x(light), y = lv_obj_get_y(light);
      const auto opacity = lv_obj_get_style_opa(light, LV_PART_MAIN);
      const auto before = flushedPixels;
      screenshot("motion-" + state + "-a"); advance(ui, 800);
      const auto painted = flushedPixels - before;
      CHECK(painted < 480 * 480 * 4);
      if (!active) CHECK(painted == 0);
      else if (state == "notification") CHECK(lv_obj_get_style_opa(light, LV_PART_MAIN) != opacity);
      else CHECK(lv_obj_get_x(light) != x || lv_obj_get_y(light) != y);
      screenshot("motion-" + state + "-b");
      if (state == "done") {
        advance(ui, 800); CHECK(lv_obj_is_hidden(light));
        const auto settled = flushes; advance(ui, 800); CHECK(flushes == settled);
      }
      std::cout << "Motion " << state << ": " << painted << " pixels / 800ms across four cards\n";
      if (state == "working") {
        tap(ui, "panel-light-0-0"); CHECK(ui.page() == Page::Details);
        CHECK(!ui.widget("panel-light-0-0"));
        advance(ui, 250); const auto resting = flushes; advance(ui, 800); CHECK(flushes == resting);
      }
    }
    lv_deinit();
    std::cout << "PASS real LVGL UI: four cards, touch selection, category filtering, persistence, setup forms, Unicode, missing values, offline display and idle redraw budget\n";
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
