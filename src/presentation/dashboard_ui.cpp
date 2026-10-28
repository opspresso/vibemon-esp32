#include "dashboard_ui.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace vibemon {
namespace {
using namespace design;
const char* const TokenKeys[] = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "\n", "a", "b", "c", "d", "e", "f", "\n", "vm_", LV_SYMBOL_LEFT, LV_SYMBOL_RIGHT, LV_SYMBOL_BACKSPACE, LV_SYMBOL_OK, ""};
constexpr auto Key = static_cast<lv_buttonmatrix_ctrl_t>(LV_BUTTONMATRIX_CTRL_NO_REPEAT | 1);
constexpr auto WideKey = static_cast<lv_buttonmatrix_ctrl_t>(2);
constexpr lv_buttonmatrix_ctrl_t TokenControls[] = {Key, Key, Key, Key, Key, Key, Key, Key, Key, Key, Key, Key, Key, Key, Key, Key, Key, Key, Key, WideKey, WideKey};

void box(lv_obj_t* object, int x, int y, int width, int height, std::uint32_t color, int radius = 16) {
  lv_obj_set_align(object, LV_ALIGN_TOP_LEFT);
  lv_obj_set_pos(object, x, y); lv_obj_set_size(object, width, height);
  lv_obj_set_style_bg_color(object, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(object, 0, 0);
  lv_obj_set_style_radius(object, radius, 0);
  lv_obj_set_style_pad_all(object, 0, 0);
  lv_obj_set_scrollable(object, false);
}
void visible(lv_obj_t* object, bool show) {
  lv_obj_set_hidden(object, !show);
}
void wrap(lv_obj_t* object) {
  lv_label_set_long_mode(object, LV_LABEL_LONG_MODE_WRAP);
  lv_obj_set_height(object, LV_SIZE_CONTENT);
}
std::string firmwareStatus(const FirmwareView& firmware) {
  if (firmware.confirmationFailed) return "Confirm failed · tap to retry";
  switch (firmware.state) {
    case FirmwareState::Waiting: return "Update check pending";
    case FirmwareState::Checking: return "Checking for updates";
    case FirmwareState::Current: return "Up to date";
    case FirmwareState::Unavailable: return "Check failed · retrying";
    case FirmwareState::Installing: return "Installing " + firmware.available + " · " + std::to_string(firmware.progress) + "%";
    case FirmwareState::Restarting: return "Restarting into " + firmware.available;
    case FirmwareState::Failed: return firmware.available + " failed · retrying later";
  }
  return {};
}

std::string shortConnection(Connection connection, bool hasSnapshot) {
  switch (connection) {
    case Connection::NeedWifi: return "Set up Wi-Fi";
    case Connection::ConnectingWifi: return "Connecting to Wi-Fi";
    // The saved network is retried automatically; only the readings differ.
    case Connection::WifiUnavailable: return hasSnapshot ? "Offline · last readings" : "Wi-Fi unavailable · retrying";
    case Connection::SettingClock: return "Setting the clock";
    case Connection::ClockUnavailable: return "Check network time";
    case Connection::NeedCloud: return "Connect your workspace";
    case Connection::CheckingCloud: return "Loading your workspace";
    case Connection::Online: return "Workspace connected";
    case Connection::CloudUnavailable: return "Server unavailable";
    case Connection::TokenRejected: return "Check your read token";
    case Connection::StorageError: return "Settings need attention";
  }
  return "Not connected";
}
// App's agent-card.css: three states have their own edge color; the others use their tone color.
std::uint32_t statusColor(const std::string& state) {
  if (state == "thinking") return StateThinking;
  if (state == "planning") return StatePlanning;
  if (state == "packing") return StatePacking;
  const auto tone = stateTone(state);
  if (tone == "warning") return Warning;
  if (tone == "danger") return Danger;
  if (tone == "good") return Good;
  return tone == "active" ? Info : Muted;
}
// Gauge fill, as in App, Web and iOS.
std::uint32_t toneColor(MetricTone tone) {
  return tone == MetricTone::Danger ? Danger : tone == MetricTone::Warning ? Warning : Accent;
}

constexpr int CardWidth = 216, CardHeight = 174, ContentWidth = CardWidth - 2 * PanelPadding;
// Account labels are short words, so plan usage and its reset time get the wider value column.
constexpr int ResourceValueWidth = 88, AccountValueWidth = 124;
// Lights travel on the card border, one pixel inside its outer edge.
lv_point_t edgePoint(float phase) {
  constexpr float pi = 3.14159265f, corner = PanelRadius, radius = corner - 1;
  constexpr float horizontal = CardWidth - 2 * corner, vertical = CardHeight - 2 * corner, arc = pi * radius / 2;
  constexpr float right = CardWidth - corner, bottom = CardHeight - corner;
  float distance = (phase - std::floor(phase)) * (2 * (horizontal + vertical) + 4 * arc);
  auto point = [](float x, float y) { return lv_point_t{static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y))}; };
  if (distance < horizontal) return point(corner + distance, 1);
  distance -= horizontal;
  if (distance < arc) return point(right + radius * std::sin(distance / radius), corner - radius * std::cos(distance / radius));
  distance -= arc;
  if (distance < vertical) return point(CardWidth - 1, corner + distance);
  distance -= vertical;
  if (distance < arc) return point(right + radius * std::cos(distance / radius), bottom + radius * std::sin(distance / radius));
  distance -= arc;
  if (distance < horizontal) return point(right - distance, CardHeight - 1);
  distance -= horizontal;
  if (distance < arc) return point(corner - radius * std::sin(distance / radius), bottom + radius * std::cos(distance / radius));
  distance -= arc;
  if (distance < vertical) return point(1, bottom - distance);
  distance -= vertical;
  return point(corner - radius * std::cos(distance / radius), corner - radius * std::sin(distance / radius));
}
}  // namespace

DashboardUi::~DashboardUi() {
  if (root_) {
    lv_obj_clean(root_);
    lv_obj_set_style_text_font(root_, &lv_font_montserrat_16, 0);
  }
}

bool DashboardUi::begin() {
  if (!fonts_.begin()) return false;
  root_ = lv_screen_active();
  if (controller_.workspace().ssid.empty()) wifi();
  else dashboard();
  return true;
}

lv_obj_t* DashboardUi::widget(const std::string& name) const {
  const auto found = widgets_.find(name);
  return found == widgets_.end() ? nullptr : found->second;
}

void DashboardUi::clear(Page page) {
  lv_obj_clean(root_);
  handlers_.clear(); rowHandlers_.clear(); widgets_.clear();
  cards_ = {};
  connection_ = formMessage_ = firstField_ = secretField_ = keyboard_ = list_ = scanMessage_ = nullptr;
  detailName_ = detailCategory_ = detailStatus_ = detailMessage_ = detailReadings_ = detailProjects_ = nullptr;
  detailMetrics_.clear(); detailMembers_.clear();
  waitingWifi_ = waitingCloud_ = pendingPickerRefresh_ = false;
  page_ = page;
  controller_.browse(page == Page::Picker);
  box(root_, 0, 0, 480, 480, Background, 0);
  lv_obj_set_style_text_color(root_, lv_color_hex(Foreground), 0);
  lv_obj_set_style_text_font(root_, fonts_.normal(), 0);
  lastRevision_ = controller_.workspace().revision;
}

void DashboardUi::event(lv_obj_t* object, lv_event_code_t code, std::function<void(lv_event_t*)> callback, bool row) {
  auto handler = std::make_unique<Handler>(); handler->invoke = std::move(callback);
  lv_obj_add_event_cb(object, [](lv_event_t* event) {
    // Navigation may delete the clicked widget and its handler. Keep this invocation alive.
    auto invoke = static_cast<Handler*>(lv_event_get_user_data(event))->invoke;
    invoke(event);
  }, code, handler.get());
  (row ? rowHandlers_ : handlers_).push_back(std::move(handler));
}

lv_obj_t* DashboardUi::label(lv_obj_t* parent, const std::string& text, int x, int y, int width, const lv_font_t* font, std::uint32_t color) {
  auto* object = lv_label_create(parent);
  lv_obj_set_pos(object, x, y); lv_obj_set_width(object, width);
  lv_obj_set_style_text_color(object, lv_color_hex(color), 0);
  lv_obj_set_style_text_font(object, font ? font : fonts_.normal(), 0);
  lv_obj_set_height(object, lv_font_get_line_height(font ? font : fonts_.normal()));
  lv_label_set_long_mode(object, LV_LABEL_LONG_MODE_DOTS);
  lv_label_set_text(object, text.c_str());
  lv_obj_set_clickable(object, false);
  return object;
}

lv_obj_t* DashboardUi::button(lv_obj_t* parent, const std::string& text, int x, int y, int width, int height, std::function<void()> clicked, bool primary) {
  auto* object = lv_button_create(parent);
  box(object, x, y, width, height, primary ? Accent : Surface, 12);
  lv_obj_set_style_bg_color(object, primary ? lv_color_mix(lv_color_hex(Accent), lv_color_hex(Background), LV_OPA_90) : lv_color_hex(SurfaceRaised), LV_STATE_PRESSED);
  auto* caption = label(object, text, 8, 0, width - 16, fonts_.normal(), primary ? AccentForeground : Foreground);
  lv_obj_align(caption, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, 0);
  event(object, LV_EVENT_CLICKED, [clicked = std::move(clicked)](lv_event_t*) { clicked(); });
  return object;
}

void DashboardUi::header(const std::string& title, std::function<void()> back) {
  widgets_["back"] = button(root_, LV_SYMBOL_LEFT, 12, 10, 44, 44, std::move(back));
  label(root_, title, 70, 20, 396, fonts_.title());
}

bool DashboardUi::setText(lv_obj_t* object, const std::string& text) {
  if (text == lv_label_get_text(object)) return false;
  lv_label_set_text(object, text.c_str());
  return true;
}

void DashboardUi::dashboard() {
  clear(Page::Dashboard);
  auto* mark = lv_image_create(root_); lv_image_set_src(mark, brandImage()); lv_obj_set_pos(mark, 16, 14);
  label(root_, "VibeMon", 58, 11, 300, &lv_font_montserrat_24);
  connection_ = label(root_, "", 59, 41, 344, fonts_.small(), Good);
  widgets_["connection"] = connection_;
  widgets_["settings"] = button(root_, LV_SYMBOL_SETTINGS, 420, 12, 44, 44, [this] { settings(); });
  label(root_, "YOUR WORKSPACE", 16, 72, 210, &lv_font_montserrat_12, Muted);
  auto* hint = label(root_, "Tap a card for details", 238, 71, 226, fonts_.small(), Muted);
  lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_RIGHT, 0);
  for (std::size_t i = 0; i < PanelCount; ++i) {
    auto& card = cards_[i];
    card.box = lv_button_create(root_);
    box(card.box, 16 + static_cast<int>(i % 2) * 232, 94 + static_cast<int>(i / 2) * 186, CardWidth, CardHeight, Surface, PanelRadius);
    lv_obj_set_style_border_width(card.box, 1, 0);
    lv_obj_set_style_border_color(card.box, lv_color_hex(Border), 0);
    lv_obj_set_style_shadow_opa(card.box, LV_OPA_30, 0);
    lv_obj_set_style_shadow_opa(card.box, LV_OPA_60, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(card.box, lv_color_hex(SurfaceRaised), LV_STATE_PRESSED);
    event(card.box, LV_EVENT_CLICKED, [this, i](lv_event_t*) {
      if (controller_.workspace().catalog.find(controller_.workspace().panels[i])) details(i);
      else picker(i);
    });
    widgets_["panel-" + std::to_string(i)] = card.box;
    card.category = label(card.box, "PANEL " + std::to_string(i + 1), PanelPadding, 13, ContentWidth - 20, fonts_.small(), Muted);
    widgets_["panel-category-" + std::to_string(i)] = card.category;
    label(card.box, LV_SYMBOL_RIGHT, CardWidth - PanelPadding - 16, 13, 16, &lv_font_montserrat_12, Muted);
    card.name = label(card.box, "", PanelPadding, 34, ContentWidth, fonts_.title());
    widgets_["panel-name-" + std::to_string(i)] = card.name;
    card.empty = label(card.box, "+\nChoose an item", PanelPadding, 61, ContentWidth, fonts_.normal(), Accent);
    lv_obj_set_style_text_align(card.empty, LV_TEXT_ALIGN_CENTER, 0);
    wrap(card.empty);
    for (std::size_t metric = 0; metric < 3; ++metric) {
      const int y = 67 + static_cast<int>(metric) * 28;
      card.labels[metric] = label(card.box, "", PanelPadding, y + 3, ContentWidth - ResourceValueWidth, fonts_.small(), Muted);
      card.values[metric] = label(card.box, "", CardWidth - PanelPadding - ResourceValueWidth, y + 3, ResourceValueWidth, fonts_.small());
      lv_obj_set_style_text_align(card.values[metric], LV_TEXT_ALIGN_RIGHT, 0);
      card.bars[metric] = lv_bar_create(card.box);
      widgets_["panel-bar-" + std::to_string(i) + "-" + std::to_string(metric)] = card.bars[metric];
      box(card.bars[metric], PanelPadding, y + 23, ContentWidth, 2, Border, 1);
      lv_bar_set_range(card.bars[metric], 0, 1000);
      lv_obj_set_style_bg_color(card.bars[metric], lv_color_hex(Accent), LV_PART_INDICATOR);
      lv_obj_set_style_bg_opa(card.bars[metric], LV_OPA_COVER, LV_PART_INDICATOR);
      lv_obj_set_style_radius(card.bars[metric], 1, LV_PART_INDICATOR);
    }
    card.footer = label(card.box, "", PanelPadding, 152, ContentWidth, fonts_.small(), Muted);
    widgets_["panel-status-" + std::to_string(i)] = card.footer;
    for (std::size_t lightIndex = 0; lightIndex < card.lights.size(); ++lightIndex) {
      auto*& light = card.lights[lightIndex];
      light = lv_obj_create(root_);
      const int size = lightIndex % 4 == 0 ? 8 : 6;
      box(light, 0, 0, size, size, Info, size);
      lv_obj_set_clickable(light, false);
      lv_obj_set_style_shadow_width(light, lightIndex % 4 == 0 ? 14 : 6, 0);
      lv_obj_set_style_shadow_opa(light, LV_OPA_70, 0);
      visible(light, false);
      widgets_["panel-light-" + std::to_string(i) + "-" + std::to_string(lightIndex)] = light;
    }
  }
  label(root_, "A little more aware.", 16, 463, 448, &lv_font_montserrat_12, Muted);
  updateCards(lv_tick_get());
  updateCardMotion(lv_tick_get());
}

void DashboardUi::updateCards(std::uint32_t now) {
  const auto& workspace = controller_.workspace();
  // An update interrupts readings and restarts the device, so it takes the status line.
  const bool updating = workspace.firmware.state == FirmwareState::Installing || workspace.firmware.state == FirmwareState::Restarting;
  const auto status = updating ? firmwareStatus(workspace.firmware) : shortConnection(workspace.connection, workspace.hasSnapshot);
  if (setText(connection_, status)) lv_obj_set_style_text_color(connection_, lv_color_hex(updating ? Accent : workspace.connection == Connection::Online ? Good : Warning), 0);
  const auto elapsed = controller_.snapshotAgeMs(now);
  for (std::size_t index = 0; index < PanelCount; ++index) {
    auto& card = cards_[index];
    const auto& selection = workspace.panels[index];
    const auto* item = workspace.catalog.find(selection);
    const auto state = item ? monitorState(*item, elapsed) : "unknown";
    const std::string motionState = item && item->selection.kind == ItemKind::Account ? state : StandbyState;
    if (card.motionState != motionState) {
      card.motionState = motionState; card.motionSince = now;
      const auto motionTone = stateTone(motionState);
      card.trailLength = motionState == "packing" || motionTone == "danger" || motionTone == "warning" ? 2 : 4;
      const auto color = lv_color_hex(statusColor(motionState));
      for (std::size_t i = 0; i < card.lights.size(); ++i) {
        const bool head = i % card.trailLength == 0;
        lv_obj_set_size(card.lights[i], head ? 8 : 6, head ? 8 : 6);
        lv_obj_set_style_shadow_width(card.lights[i], head ? 14 : 6, 0);
        lv_obj_set_style_bg_color(card.lights[i], head ? lv_color_mix(lv_color_white(), color, LV_OPA_50) : color, 0);
        lv_obj_set_style_shadow_color(card.lights[i], color, 0);
      }
    }
    const auto tone = item && item->selection.kind == ItemKind::Account ? stateTone(state) : "neutral";
    const auto edge = lv_color_hex(tone == "neutral" ? Border : statusColor(state));
    const int emphasis = tone == "warning" || tone == "danger" ? 1 : 0;
    const int glow = tone == "neutral" ? 0 : emphasis ? 20 : 14;
    if (!lv_color_eq(lv_obj_get_style_border_color(card.box, LV_PART_MAIN), edge)) lv_obj_set_style_border_color(card.box, edge, 0);
    // Outline emphasis does not change the content box or move its touch targets.
    if (!lv_color_eq(lv_obj_get_style_outline_color(card.box, LV_PART_MAIN), edge)) lv_obj_set_style_outline_color(card.box, edge, 0);
    if (lv_obj_get_style_outline_width(card.box, LV_PART_MAIN) != emphasis) lv_obj_set_style_outline_width(card.box, emphasis, 0);
    if (!lv_color_eq(lv_obj_get_style_shadow_color(card.box, LV_PART_MAIN), edge)) lv_obj_set_style_shadow_color(card.box, edge, 0);
    if (lv_obj_get_style_shadow_width(card.box, LV_PART_MAIN) != glow) lv_obj_set_style_shadow_width(card.box, glow, 0);
    visible(card.empty, !item); visible(card.name, item != nullptr);
    if (!item) {
      setText(card.category, "PANEL " + std::to_string(index + 1));
      setText(card.empty, selection.id.empty() ? "+\nChoose an item" : "Item unavailable\nTap to choose another");
      setText(card.footer, selection.id.empty() ? "Agent or resource" : "Selection kept for reconnect");
      if (card.accent != Muted) { lv_obj_set_style_text_color(card.footer, lv_color_hex(Muted), 0); card.accent = Muted; }
    } else {
      setText(card.category, item->category);
      setText(card.name, monitorTitle(*item, elapsed));
      const auto color = statusColor(state);
      // An account that only a connected service reports has no project, so it has no activity line.
      const auto activity = item->selection.kind == ItemKind::Account ? accountActivityLine(*item, elapsed) : std::string();
      const auto caption = item->selection.kind != ItemKind::Account ? stateLabel(state) + " · " + ageLabel(static_cast<std::uint64_t>(item->ageMs) + elapsed) :
          activity.empty() ? stateLabel(state) : stateLabel(state) + " · " + activity;
      setText(card.footer, caption);
      if (card.accent != color) { lv_obj_set_style_text_color(card.footer, lv_color_hex(color), 0); card.accent = color; }
    }
    for (std::size_t m = 0; m < 3; ++m) {
      const bool available = item && m < item->metrics.size();
      visible(card.labels[m], available); visible(card.values[m], available);
      if (!available) { visible(card.bars[m], false); continue; }
      const auto& metric = item->metrics[m];
      const bool account = item->selection.kind == ItemKind::Account;
      const int valueWidth = account ? AccountValueWidth : ResourceValueWidth, labelWidth = ContentWidth - valueWidth, valueX = CardWidth - PanelPadding - valueWidth;
      if (lv_obj_get_style_width(card.labels[m], LV_PART_MAIN) != labelWidth) lv_obj_set_width(card.labels[m], labelWidth);
      if (lv_obj_get_x(card.values[m]) != valueX) { lv_obj_set_x(card.values[m], valueX); lv_obj_set_width(card.values[m], valueWidth); }
      setText(card.labels[m], metric.label);
      // Plan usage reads like the companion bubble: "38% · 2h5m".
      const auto reset = resetCountdown(metric, static_cast<std::uint64_t>(item->activityAgeMs) + elapsed);
      setText(card.values[m], reset.empty() ? metricText(metric) : metricText(metric) + " · " + reset);
      const bool bar = metric.display == MetricDisplay::Bar && metric.value && metric.maximum;
      visible(card.bars[m], bar);
      if (bar) {
        const auto progress = static_cast<int>(std::lround(std::clamp((*metric.value - metric.minimum) / (*metric.maximum - metric.minimum), 0.0, 1.0) * 1000));
        if (lv_bar_get_value(card.bars[m]) != progress) lv_bar_set_value(card.bars[m], progress, LV_ANIM_OFF);
        const auto fill = lv_color_hex(toneColor(metricTone(metric.key, metric.value)));
        if (!lv_color_eq(lv_obj_get_style_bg_color(card.bars[m], LV_PART_INDICATOR), fill)) lv_obj_set_style_bg_color(card.bars[m], fill, LV_PART_INDICATOR);
      }
    }
  }
}

void DashboardUi::updateCardMotion(std::uint32_t now) {
  for (auto& card : cards_) {
    if (!card.box) continue;
    const auto& state = card.motionState;
    const auto tone = stateTone(state);
    const auto elapsed = static_cast<std::uint32_t>(now - card.motionSince);
    const bool waiting = tone == "warning";
    // The App's --vm-agent-* durations: active states travel, thinking also breathes, input requests
    // pulse late in each wait cycle, errors run fast, and completion makes one sweep.
    const bool moving = tone == "active" || needsAttention(state) || (state == "done" && elapsed < AgentCompleteMs);
    const std::uint32_t period = state == "planning" ? AgentPlanningMs : state == "packing" ? AgentPackingMs : state == "start" ? AgentStartMs :
        tone == "danger" ? AgentDangerMs : state == "done" ? AgentCompleteMs : AgentOrbitMs;
    const float phase = static_cast<float>(elapsed % period) / period;
    const float breathe = .5f - .5f * std::cos(static_cast<float>(elapsed % AgentBreatheMs) / AgentBreatheMs * 6.2831853f);
    const float wait = static_cast<float>(elapsed % AgentWaitMs) / AgentWaitMs;
    const float waitPulse = wait < .75f ? 0 : .5f - .5f * std::cos((wait - .75f) / .25f * 6.2831853f);
    const float level = waiting ? .4f + .6f * waitPulse : state == "thinking" ? .4f + .6f * breathe : 1;
    for (std::size_t i = 0; i < card.lights.size(); ++i) {
      auto* light = card.lights[i];
      visible(light, moving);
      if (!moving) continue;
      const float direction = state == "packing" ? -1 : 1;
      const float head = waiting ? .04f : direction * phase;
      const auto trail = i % card.trailLength;
      const auto point = edgePoint(head + (i / card.trailLength) * .5f - direction * trail * (card.trailLength == 2 ? .018f : .012f));
      const int size = lv_obj_get_width(light);
      lv_obj_set_pos(light, lv_obj_get_x(card.box) + point.x - size / 2, lv_obj_get_y(card.box) + point.y - size / 2);
      constexpr int opacity[] = {255, 170, 95, 45};
      const auto alpha = static_cast<lv_opa_t>(opacity[trail] * level);
      if (lv_obj_get_style_opa(light, LV_PART_MAIN) != alpha) lv_obj_set_style_opa(light, alpha, 0);
    }
  }
}

lv_obj_t* DashboardUi::detailLabel(lv_obj_t* parent, const std::string& text, const lv_font_t* font, std::uint32_t color) {
  auto* value = label(parent, text, 0, 0, 408, font, color);
  lv_obj_set_width(value, LV_PCT(100));
  wrap(value);
  return value;
}

void DashboardUi::details(std::size_t panel) {
  clear(Page::Details); selectedPanel_ = panel;
  header("Details", [this] { dashboard(); });
  list_ = lv_obj_create(root_); box(list_, 16, 70, 448, 334, Surface, 12);
  lv_obj_set_style_pad_all(list_, 16, 0); lv_obj_set_style_pad_row(list_, 12, 0);
  lv_obj_set_scrollable(list_, true); lv_obj_set_scroll_dir(list_, LV_DIR_VER);
  lv_obj_set_flex_flow(list_, LV_FLEX_FLOW_COLUMN);
  widgets_["detail-list"] = list_;
  detailCategory_ = detailLabel(list_, "", fonts_.small(), Muted);
  detailName_ = detailLabel(list_, "", fonts_.title(), Foreground);
  detailStatus_ = detailLabel(list_, "", fonts_.normal(), Muted);
  detailMessage_ = detailLabel(list_, "", fonts_.normal(), Muted);
  widgets_["detail-name"] = detailName_; widgets_["detail-status"] = detailStatus_;
  widgets_["detail-message"] = detailMessage_;
  auto section = [this] {
    auto* section = lv_obj_create(list_); box(section, 0, 0, 416, 1, Surface, 0);
    lv_obj_set_width(section, LV_PCT(100)); lv_obj_set_height(section, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_row(section, 12, 0); lv_obj_set_flex_flow(section, LV_FLEX_FLOW_COLUMN);
    return section;
  };
  detailReadings_ = section(); detailProjects_ = section();
  widgets_["change-item"] = button(root_, "Change item", 16, 420, 448, 48, [this] { picker(selectedPanel_); });
  updateDetails(lv_tick_get());
}

void DashboardUi::updateDetails(std::uint32_t now) {
  const auto& workspace = controller_.workspace();
  const auto* item = workspace.catalog.find(workspace.panels[selectedPanel_]);
  if (!item) {
    setText(detailName_, "Item unavailable"); setText(detailCategory_, "Selection kept for reconnect");
    setText(detailStatus_, "Choose another item or wait for its next report.");
    const auto muted = lv_color_hex(Muted);
    if (!lv_color_eq(lv_obj_get_style_text_color(detailStatus_, LV_PART_MAIN), muted)) lv_obj_set_style_text_color(detailStatus_, muted, 0);
    visible(detailMessage_, false); visible(detailReadings_, false); visible(detailProjects_, false);
    return;
  }
  const auto elapsed = controller_.snapshotAgeMs(now);
  const auto state = monitorState(*item, elapsed);
  setText(detailName_, monitorTitle(*item, elapsed)); setText(detailCategory_, item->category);
  setText(detailStatus_, stateLabel(state) + " · " + ageLabel(static_cast<std::uint64_t>(item->ageMs) + elapsed));
  const auto color = lv_color_hex(statusColor(state));
  if (!lv_color_eq(lv_obj_get_style_text_color(detailStatus_, LV_PART_MAIN), color)) lv_obj_set_style_text_color(detailStatus_, color, 0);
  setText(detailMessage_, item->statusMessage); visible(detailMessage_, !item->statusMessage.empty());
  auto updateRows = [this](lv_obj_t* section, auto& rows, const auto& values, const auto& key, const auto& name, const auto& value, bool projects) {
    const bool changed = rows.size() != values.size() || !std::equal(rows.begin(), rows.end(), values.begin(),
        [&key](const auto& row, const auto& input) { return row.key == key(input); });
    visible(section, !values.empty());
    if (changed) {
      lv_obj_clean(section); rows.clear();
      if (projects) detailLabel(section, "Reporting projects", fonts_.normal(), Foreground);
      for (const auto& input : values) {
        auto* group = lv_obj_create(section); box(group, 0, 0, 416, 1, Surface, 0);
        lv_obj_set_width(group, LV_PCT(100)); lv_obj_set_height(group, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_row(group, 3, 0); lv_obj_set_flex_flow(group, LV_FLEX_FLOW_COLUMN);
        rows.push_back({key(input), detailLabel(group, "", fonts_.small(), Muted), detailLabel(group, "", fonts_.normal(), Foreground)});
      }
    }
    for (std::size_t i = 0; i < values.size(); ++i) {
      setText(rows[i].name, name(values[i])); setText(rows[i].value, value(values[i]));
    }
  };
  const auto observationAge = static_cast<std::uint64_t>(item->activityAgeMs) + elapsed;
  updateRows(detailReadings_, detailMetrics_, item->metrics, [](const Metric& metric) { return metric.key; },
      [](const Metric& metric) { return metric.label; }, [observationAge](const Metric& metric) {
        const auto reset = describeReset(resetCountdown(metric, observationAge));
        return reset.empty() ? metricText(metric, false) : metricText(metric, false) + " · " + reset;
      }, false);
  updateRows(detailProjects_, detailMembers_, item->projects, [](const ReportState& project) { return project.sourceId; },
      [](const ReportState& project) { return project.project.empty() ? project.sourceId : project.project; },
      [elapsed](const ReportState& project) { return stateLabel(projectState(project, elapsed)); }, true);
  for (std::size_t i = 0; i < item->projects.size(); ++i) {
    const auto projectColor = lv_color_hex(statusColor(projectState(item->projects[i], elapsed)));
    if (!lv_color_eq(lv_obj_get_style_text_color(detailMembers_[i].value, LV_PART_MAIN), projectColor))
      lv_obj_set_style_text_color(detailMembers_[i].value, projectColor, 0);
  }
}

void DashboardUi::settings() {
  clear(Page::Settings); header("Your device", [this] { dashboard(); });
  const auto& workspace = controller_.workspace();
  label(root_, "CONNECTIONS", 20, 79, 440, &lv_font_montserrat_12, Muted);
  auto* network = button(root_, "", 16, 108, 448, 76, [this] { wifi(); });
  widgets_["wifi-settings"] = network;
  label(network, "Wi-Fi", 16, 13, 380, fonts_.normal());
  label(network, workspace.ssid.empty() ? "Not configured" : workspace.ssid, 16, 44, 380, fonts_.small(), Muted);
  label(network, LV_SYMBOL_RIGHT, 410, 29, 24, fonts_.normal(), Muted);
  auto* server = button(root_, "", 16, 202, 448, 76, [this] { cloud(); });
  widgets_["cloud-settings"] = server;
  label(server, "VibeMon workspace", 16, 13, 380, fonts_.normal());
  label(server, workspace.origin + (workspace.tokenConfigured ? " · token saved" : " · add read token"), 16, 44, 380, fonts_.small(), Muted);
  label(server, LV_SYMBOL_RIGHT, 410, 29, 24, fonts_.normal(), Muted);
  widgets_["refresh"] = button(root_, LV_SYMBOL_REFRESH "  Refresh readings", 16, 304, 448, 48, [this] { controller_.refresh(); dashboard(); });
  auto* message = label(root_, workspace.message, 20, 362, 440, fonts_.small(), workspace.connection == Connection::Online ? Muted : Warning);
  wrap(message);
  label(root_, "FIRMWARE", 20, 404, 440, &lv_font_montserrat_12, Muted);
  // Tapping starts a check now; a newer release then installs and restarts.
  auto* firmware = button(root_, "", 16, 426, 448, 46, [this] { controller_.checkFirmware(); });
  widgets_["firmware"] = firmware;
  label(firmware, "VibeMon Touch " + controller_.firmwareVersion(), 16, 13, 160, fonts_.small());
  widgets_["firmware-status"] = label(firmware, firmwareStatus(workspace.firmware), 176, 13, 256, fonts_.small(), workspace.firmware.confirmationFailed ? Warning : Muted);
  lv_obj_set_style_text_align(widgets_["firmware-status"], LV_TEXT_ALIGN_RIGHT, 0);
}

void DashboardUi::wifi() {
  clear(Page::Wifi); header("Connect Wi-Fi", [this] { dashboard(); });
  label(root_, "Choose a 2.4 GHz network.", 20, 72, 440, fonts_.normal(), Muted);
  scanMessage_ = label(root_, "Scanning nearby networks...", 20, 106, 440, fonts_.small(), Accent);
  widgets_["scan-status"] = scanMessage_;
  list_ = lv_obj_create(root_); box(list_, 16, 137, 448, 260, Background, 0);
  lv_obj_set_scrollable(list_, true); lv_obj_set_scroll_dir(list_, LV_DIR_VER);
  widgets_["network-list"] = list_;
  widgets_["manual-network"] = button(root_, "Hidden network", 16, 420, 278, 48, [this] { wifiForm(""); });
  widgets_["scan"] = button(root_, LV_SYMBOL_REFRESH "  Scan", 308, 420, 156, 48, [this] { controller_.scanWifi(); networkSignature_.clear(); });
  controller_.scanWifi(); networkSignature_.clear(); updateNetworks();
}

void DashboardUi::updateNetworks() {
  const auto& networks = controller_.networks();
  const bool scanning = controller_.scanning();
  setText(scanMessage_, scanning ? "Scanning nearby networks..." : controller_.scanFailed() ? "Scan failed. Try Scan again or enter a network." : networks.empty() ? "No networks found. Enter a network manually." : "Select your network to continue.");
  std::string signature;
  for (const auto& network : networks) signature += network.ssid + '\n' + std::to_string(network.signal) + '\n';
  if (signature == networkSignature_ && scanning == lastScanning_) return;
  networkSignature_ = std::move(signature); lastScanning_ = scanning;
  lv_obj_clean(list_); rowHandlers_.clear();
  for (auto i = widgets_.begin(); i != widgets_.end();) { if (i->first.rfind("network-", 0) == 0 && i->first != "network-list") i = widgets_.erase(i); else ++i; }
  for (std::size_t i = 0; i < networks.size(); ++i) {
    const auto& network = networks[i];
    auto* row = lv_button_create(list_); box(row, 0, static_cast<int>(i) * 70, 448, 62, Surface, 12);
    label(row, network.ssid, 14, 9, 386, fonts_.normal());
    label(row, std::string(network.secured ? "Secured" : "Open network") + " · " + (network.signal > -65 ? "Strong signal" : network.signal > -80 ? "Fair signal" : "Weak signal"), 14, 34, 386, fonts_.small(), Muted);
    event(row, LV_EVENT_CLICKED, [this, ssid = network.ssid](lv_event_t*) { wifiForm(ssid); }, true);
    widgets_["network-" + std::to_string(i)] = row;
  }
}

lv_obj_t* DashboardUi::field(const std::string& id, const std::string& value, const std::string& placeholder, int y, int maxLength, bool password) {
  auto* input = lv_textarea_create(root_);
  box(input, 16, y, 448, 48, Surface, 10);
  lv_obj_set_style_border_color(input, lv_color_hex(Border), 0); lv_obj_set_style_border_width(input, 1, 0);
  lv_obj_set_style_border_color(input, lv_color_hex(Accent), LV_STATE_FOCUSED);
  lv_obj_set_style_pad_all(input, 12, 0); lv_obj_set_style_text_font(input, fonts_.normal(), 0);
  lv_textarea_set_one_line(input, true); lv_textarea_set_max_length(input, maxLength);
  lv_textarea_set_placeholder_text(input, placeholder.c_str()); lv_textarea_set_password_mode(input, password);
  lv_textarea_set_text(input, value.c_str());
  event(input, LV_EVENT_FOCUSED, [this, input](lv_event_t*) { focusField(input); });
  event(input, LV_EVENT_CLICKED, [this, input](lv_event_t*) { focusField(input); });
  widgets_[id] = input;
  return input;
}

void DashboardUi::focusField(lv_obj_t* input) {
  if (!keyboard_) return;
  const bool token = page_ == Page::CloudForm && input == secretField_;
  lv_keyboard_set_mode(keyboard_, token ? LV_KEYBOARD_MODE_USER_1 : LV_KEYBOARD_MODE_TEXT_LOWER);
  lv_obj_set_style_pad_column(keyboard_, token ? 3 : 4, 0);
  lv_keyboard_set_textarea(keyboard_, input);
  visible(keyboard_, true);
}

void DashboardUi::attachKeyboard() {
  keyboard_ = lv_keyboard_create(root_);
  lv_obj_align(keyboard_, LV_ALIGN_TOP_LEFT, 0, 254); lv_obj_set_size(keyboard_, 480, 226);
  lv_obj_set_style_bg_color(keyboard_, lv_color_hex(Background), 0); lv_obj_set_style_bg_opa(keyboard_, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(keyboard_, 4, 0); lv_obj_set_style_pad_row(keyboard_, 5, 0); lv_obj_set_style_pad_column(keyboard_, 4, 0);
  lv_obj_set_style_bg_color(keyboard_, lv_color_hex(Surface), LV_PART_ITEMS);
  lv_obj_set_style_bg_opa(keyboard_, LV_OPA_COVER, LV_PART_ITEMS);
  lv_obj_set_style_radius(keyboard_, 7, LV_PART_ITEMS);
  lv_obj_set_style_text_color(keyboard_, lv_color_hex(Foreground), LV_PART_ITEMS);
  lv_obj_set_style_text_font(keyboard_, &lv_font_montserrat_16, LV_PART_ITEMS);
  lv_obj_set_style_bg_color(keyboard_, lv_color_hex(Border), static_cast<lv_style_selector_t>(LV_PART_ITEMS) | static_cast<lv_style_selector_t>(LV_STATE_PRESSED));
  lv_keyboard_set_map(keyboard_, LV_KEYBOARD_MODE_USER_1, TokenKeys, TokenControls);
  focusField(secretField_);
  widgets_["keyboard"] = keyboard_;
  event(keyboard_, LV_EVENT_READY, [this](lv_event_t*) { visible(keyboard_, false); });
  event(keyboard_, LV_EVENT_CANCEL, [this](lv_event_t*) { visible(keyboard_, false); });
}

void DashboardUi::formStatus(const std::string& message, bool error) {
  if (!formMessage_) return;
  setText(formMessage_, message); lv_obj_set_style_text_color(formMessage_, lv_color_hex(error ? Danger : Muted), 0);
}

void DashboardUi::wifiForm(const std::string& ssid) {
  clear(Page::WifiForm); header("Join your network", [this] { controller_.cancelSetup(); wifi(); });
  firstField_ = field("ssid", ssid, "Network name", 61, 32, false);
  secretField_ = field("wifi-password", "", "Password · blank for open networks", 113, 64, true);
  formMessage_ = label(root_, "Your password stays on this device.", 20, 164, 440, fonts_.small(), Muted);
  wrap(formMessage_);
  widgets_["join-wifi"] = button(root_, "Connect Wi-Fi", 16, 202, 448, 44, [this] {
    const auto result = controller_.saveWifi(lv_textarea_get_text(firstField_), lv_textarea_get_text(secretField_));
    waitingWifi_ = result.ok; formStatus(result.ok ? "Connecting..." : result.message, !result.ok);
  }, true);
  attachKeyboard();
  if (ssid.empty()) focusField(firstField_);
}

void DashboardUi::cloud() {
  clear(Page::CloudForm); header("Connect VibeMon", [this] { controller_.cancelSetup(); settings(); });
  // Signing in with a code uses the server in the field, so another server works the same way.
  widgets_["use-code"] = button(root_, "Use a code", 330, 10, 134, 44, [this] {
    const std::string origin = lv_textarea_get_text(firstField_);
    controller_.cancelSetup(); signIn(origin);
  });
  firstField_ = field("server", controller_.workspace().origin, "https://vibemon.io", 61, MaxOriginBytes, false);
  secretField_ = field("read-token", "", controller_.workspace().tokenConfigured ? "Read token · blank keeps it on the same server" : "Read token from vibemon.io/account", 113, 63, true);
  formMessage_ = label(root_, "Use a token with read permission.", 20, 164, 440, fonts_.small(), Muted);
  wrap(formMessage_);
  widgets_["connect-cloud"] = button(root_, "Connect workspace", 16, 202, 448, 44, [this] {
    const auto result = controller_.saveCloud(lv_textarea_get_text(firstField_), lv_textarea_get_text(secretField_));
    waitingCloud_ = result.ok; formStatus(result.ok ? "Checking your read token..." : result.message, !result.ok);
  }, true);
  attachKeyboard();
}

void DashboardUi::signIn(const std::string& origin) {
  clear(Page::SignIn); header("Sign in with a code", [this] { controller_.cancelSignIn(); cloud(); });
  // The steps appear only while a code is waiting for approval.
  auto* steps = lv_obj_create(root_); box(steps, 0, 70, 480, 200, Background, 0);
  widgets_["sign-in-steps"] = steps;
  // Scanners need dark modules on a light field with a quiet zone around them.
  auto* field = lv_obj_create(steps); box(field, 16, 0, 196, 196, 0xffffff, 12);
  auto* code = lv_qrcode_create(field);
  lv_qrcode_set_size(code, 172);
  lv_qrcode_set_dark_color(code, lv_color_hex(0x000000));
  lv_qrcode_set_light_color(code, lv_color_hex(0xffffff));
  lv_obj_center(code);
  widgets_["sign-in-qr"] = code;
  label(steps, "Scan the code, or open", 228, 4, 236, fonts_.small(), Muted);
  widgets_["sign-in-page"] = label(steps, "", 228, 26, 236, fonts_.normal());
  label(steps, "and enter", 228, 66, 236, fonts_.small(), Muted);
  widgets_["sign-in-code"] = label(steps, "", 228, 88, 236, &lv_font_montserrat_28);
  auto* allow = label(steps, "Sign in, then select Allow. This device receives a read-only token.", 228, 134, 236, fonts_.small(), Muted);
  wrap(allow);
  formMessage_ = label(root_, "", 20, 290, 440, fonts_.normal(), Muted);
  wrap(formMessage_);
  widgets_["new-code"] = button(root_, LV_SYMBOL_REFRESH " New code", 16, 420, 216, 48, [this, origin] {
    const auto result = controller_.startSignIn(origin);
    if (!result.ok) formStatus(result.message, true);
  });
  widgets_["enter-token"] = button(root_, "Enter a token", 248, 420, 216, 48, [this] { controller_.cancelSignIn(); cloud(); });
  signInStage_ = static_cast<std::uint32_t>(-1);
  const auto started = controller_.startSignIn(origin);
  updateSignIn();
  if (!started.ok) formStatus(started.message, true);
}

void DashboardUi::updateSignIn() {
  const auto& view = controller_.signIn();
  const auto stage = static_cast<std::uint32_t>(view.stage);
  const bool waiting = view.stage == SignInStage::Waiting;
  visible(widget("sign-in-steps"), waiting);
  // Without steps, the status line takes their place below the header.
  lv_obj_set_y(formMessage_, waiting ? 290 : 80);
  // The page address without its scheme fits the narrow column.
  const auto page = view.verificationUri.rfind("https://", 0) == 0 ? view.verificationUri.substr(8) : view.verificationUri;
  setText(widget("sign-in-page"), waiting ? page : "");
  setText(widget("sign-in-code"), waiting ? view.userCode : "");
  if (waiting && stage != signInStage_) {
    const auto& link = view.verificationUriComplete;
    lv_qrcode_update(widget("sign-in-qr"), link.data(), static_cast<std::uint32_t>(link.size()));
  }
  signInStage_ = stage;
  if (view.stage == SignInStage::Approved) {
    // The approved token goes through the same check as one entered by hand.
    const auto& workspace = controller_.workspace();
    if (!controller_.changingCloud() && workspace.connection == Connection::Online) { dashboard(); return; }
    const bool failed = !controller_.changingCloud();
    visible(widget("new-code"), failed);
    formStatus(failed ? workspace.message : view.message, failed);
    return;
  }
  visible(widget("new-code"), view.stage == SignInStage::Failed);
  const auto message = view.message.empty() && waiting ? "Waiting for approval..." : view.message;
  formStatus(message, view.stage == SignInStage::Failed);
}

void DashboardUi::picker(std::size_t panel) {
  clear(Page::Picker); selectedPanel_ = panel; filter_ = 0;
  header("Choose for panel " + std::to_string(panel + 1), [this] { dashboard(); });
  const char* labels[] = {"All", "Agents", "Resources"};
  for (int i = 0; i < 3; ++i) {
    auto* tab = button(root_, labels[i], 16 + i * 152, 70, 144, 44, [this, i] {
      filter_ = i;
      for (int index = 0; index < 3; ++index) lv_obj_set_style_bg_color(widget("filter-" + std::to_string(index)), lv_color_hex(index == i ? Border : Surface), 0);
      updatePicker();
    });
    widgets_["filter-" + std::to_string(i)] = tab;
    if (i == 0) lv_obj_set_style_bg_color(tab, lv_color_hex(Border), 0);
  }
  list_ = lv_obj_create(root_); box(list_, 16, 128, 448, 246, Background, 0);
  lv_obj_set_scrollable(list_, true); lv_obj_set_scroll_dir(list_, LV_DIR_VER);
  widgets_["item-list"] = list_;
  widgets_["clear-panel"] = button(root_, "Leave empty", 16, 420, 278, 48, [this] {
    const auto result = controller_.select(selectedPanel_, {});
    if (result.ok) dashboard(); else formStatus(result.message, true);
  });
  widgets_["refresh-list"] = button(root_, LV_SYMBOL_REFRESH " Refresh", 308, 420, 156, 48, [this] {
    controller_.refresh(); pendingPickerRefresh_ = true; pickerRevision_ = controller_.workspace().catalogRevision;
  });
  formMessage_ = label(root_, "", 20, 382, 440, fonts_.small(), Danger);
  wrap(formMessage_);
  // Polls between pickers read only the selected panels; list the rest when they arrive.
  const auto& workspace = controller_.workspace();
  pendingPickerRefresh_ = !workspace.catalogComplete || std::none_of(workspace.panels.begin(), workspace.panels.end(), [](const Selection& panel) { return !panel.id.empty(); });
  pickerRevision_ = workspace.catalogRevision;
  updatePicker();
}

void DashboardUi::updatePicker() {
  lv_obj_clean(list_); rowHandlers_.clear();
  for (auto i = widgets_.begin(); i != widgets_.end();) { if (i->first.rfind("item-", 0) == 0 && i->first != "item-list") i = widgets_.erase(i); else ++i; }
  const auto& workspace = controller_.workspace();
  int rowIndex = 0;
  int rowY = 0;
  for (const auto& item : workspace.catalog.items) {
    if ((filter_ == 1 && item.selection.kind != ItemKind::Account) || (filter_ == 2 && item.selection.kind != ItemKind::Resource)) continue;
    auto* row = lv_button_create(list_); box(row, 0, rowY, 448, 68, Surface, 12);
    const bool selected = workspace.panels[selectedPanel_] == item.selection;
    if (selected) { lv_obj_set_style_border_width(row, 1, 0); lv_obj_set_style_border_color(row, lv_color_hex(Accent), 0); }
    auto* name = label(row, item.name, 14, 11, 386, fonts_.normal());
    wrap(name); lv_obj_update_layout(name);
    const int detailY = 19 + lv_obj_get_height(name);
    // An account category already names its compact account; resources add their source ID.
    const auto detailText = item.selection.kind == ItemKind::Account ? item.category : item.category + " · " + item.selection.id;
    auto* detail = label(row, detailText, 14, detailY, 396, fonts_.small(), Muted);
    wrap(detail); lv_obj_update_layout(detail);
    const int height = std::max(68, detailY + static_cast<int>(lv_obj_get_height(detail)) + 12);
    lv_obj_set_height(row, height);
    rowY += height + 8;
    if (selected) label(row, LV_SYMBOL_OK, 411, 12, 24, fonts_.normal(), Accent);
    event(row, LV_EVENT_CLICKED, [this, selection = item.selection](lv_event_t*) {
      const auto result = controller_.select(selectedPanel_, selection);
      if (result.ok) dashboard(); else formStatus(result.message, true);
    }, true);
    widgets_["item-" + std::to_string(rowIndex++)] = row;
  }
  if (!rowIndex) {
    auto* empty = label(list_, workspace.catalogComplete ? "No items yet.\nSend reports to this account, then refresh." : "Loading your items...", 12, 8, 420, fonts_.normal(), Muted);
    wrap(empty);
  }
}

void DashboardUi::update(std::uint32_t now) {
  // Move only small light objects; the static card and text retain their pixels.
  if (page_ == Page::Dashboard && static_cast<std::uint32_t>(now - lastMotion_) >= 34) {
    lastMotion_ = now; updateCardMotion(now);
  }
  const auto& workspace = controller_.workspace();
  const bool changed = lastRevision_ != workspace.revision;
  if (!changed && static_cast<std::uint32_t>(now - lastDraw_) < 250) return;
  lastRevision_ = workspace.revision; lastDraw_ = now;
  if (waitingWifi_) {
    if (workspace.connection == Connection::WifiUnavailable || workspace.connection == Connection::StorageError) {
      waitingWifi_ = false; formStatus(workspace.message, true);
    } else if (!controller_.changingWifi()) {
      waitingWifi_ = false;
      if (workspace.tokenConfigured) dashboard(); else signIn(workspace.origin);
      return;
    }
  }
  if (waitingCloud_ && !controller_.changingCloud()) {
    waitingCloud_ = false;
    if (workspace.connection == Connection::Online) { dashboard(); return; }
    formStatus(workspace.message, true);
  } else if (waitingCloud_ && changed) {
    formStatus(workspace.message, workspace.connection == Connection::WifiUnavailable || workspace.connection == Connection::ClockUnavailable);
  }
  if (page_ == Page::Dashboard) {
    // Only a missing configuration needs setup. A saved Wi-Fi network that is
    // unavailable is retried in the background, so the dashboard stays and
    // returns to live readings when the network does.
    if (changed && !workspace.hasSnapshot && workspace.connection == Connection::NeedCloud) { signIn(workspace.origin); return; }
    updateCards(now);
  } else if (page_ == Page::Details) updateDetails(now);
  else if (page_ == Page::Wifi) updateNetworks();
  else if (page_ == Page::SignIn && changed) updateSignIn();
  else if (page_ == Page::Settings && changed) {
    setText(widget("firmware-status"), firmwareStatus(workspace.firmware));
    lv_obj_set_style_text_color(widget("firmware-status"), lv_color_hex(workspace.firmware.confirmationFailed ? Warning : Muted), 0);
  }
  else if (page_ == Page::Picker && pendingPickerRefresh_ && !workspace.refreshing && changed) {
    if (workspace.catalogComplete && workspace.catalogRevision != pickerRevision_) {
      pendingPickerRefresh_ = false; formStatus(""); updatePicker();
    } else if (workspace.connection != Connection::Online) {
      // A failed read or another state update cannot fulfill the requested catalog refresh.
      formStatus(workspace.message, true);
    }
  }
}

}  // namespace vibemon
