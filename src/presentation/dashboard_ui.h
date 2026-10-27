#pragma once

#include "../application/controller.h"
#include "assets.h"
#include "design_tokens.h"
#include <functional>
#include <map>
#include <memory>

namespace vibemon {

enum class Page { Dashboard, Wifi, WifiForm, CloudForm, SignIn, Picker, Settings, Details };

// LVGL objects belong exclusively to the UI task. Network workers publish results through Controller ports.
class DashboardUi {
 public:
  explicit DashboardUi(Controller& controller) : controller_(controller) {}
  ~DashboardUi();
  bool begin();
  void update(std::uint32_t now);
  void dashboard();
  void wifi();
  void cloud();
  // Shows a code to approve on the server's /device page; the device then receives its read token.
  void signIn(const std::string& origin);
  void settings();
  void picker(std::size_t panel);
  void details(std::size_t panel);
  Page page() const { return page_; }
  lv_obj_t* widget(const std::string& name) const;

 private:
  struct Handler { std::function<void(lv_event_t*)> invoke; };
  struct Card {
    lv_obj_t* box = nullptr;
    lv_obj_t* category = nullptr;
    lv_obj_t* name = nullptr;
    lv_obj_t* empty = nullptr;
    lv_obj_t* footer = nullptr;
    std::array<lv_obj_t*, 3> labels{};
    std::array<lv_obj_t*, 3> values{};
    std::array<lv_obj_t*, 3> bars{};
    // Bound moving objects so LVGL's dirty-area queue does not fall back to a full-screen redraw.
    std::array<lv_obj_t*, 4> lights{};
    std::size_t trailLength = 4;
    std::string motionState;
    std::uint32_t motionSince = 0;
    std::uint32_t accent = 0;
  };
  struct DetailRow { std::string key; lv_obj_t* name; lv_obj_t* value; };
  Controller& controller_;
  Fonts fonts_;
  Page page_ = Page::Dashboard;
  lv_obj_t* root_ = nullptr;
  lv_obj_t* connection_ = nullptr;
  lv_obj_t* formMessage_ = nullptr;
  lv_obj_t* firstField_ = nullptr;
  lv_obj_t* secretField_ = nullptr;
  lv_obj_t* keyboard_ = nullptr;
  lv_obj_t* list_ = nullptr;
  lv_obj_t* scanMessage_ = nullptr;
  lv_obj_t* detailName_ = nullptr;
  lv_obj_t* detailCategory_ = nullptr;
  lv_obj_t* detailStatus_ = nullptr;
  lv_obj_t* detailMessage_ = nullptr;
  lv_obj_t* detailReadings_ = nullptr;
  lv_obj_t* detailProjects_ = nullptr;
  std::vector<DetailRow> detailMetrics_;
  std::vector<DetailRow> detailMembers_;
  std::array<Card, PanelCount> cards_{};
  std::vector<std::unique_ptr<Handler>> handlers_;
  std::vector<std::unique_ptr<Handler>> rowHandlers_;
  std::map<std::string, lv_obj_t*> widgets_;
  std::size_t selectedPanel_ = 0;
  int filter_ = 0;
  std::string networkSignature_;
  std::uint32_t lastRevision_ = 0;
  std::uint32_t lastDraw_ = 0;
  std::uint32_t lastMotion_ = 0;
  bool waitingWifi_ = false;
  bool waitingCloud_ = false;
  bool pendingPickerRefresh_ = false;
  std::uint32_t pickerRevision_ = 0;
  bool lastScanning_ = false;
  void clear(Page page);
  void header(const std::string& title, std::function<void()> back);
  lv_obj_t* label(lv_obj_t* parent, const std::string& text, int x, int y, int width, const lv_font_t* font = nullptr, std::uint32_t color = design::Foreground);
  lv_obj_t* button(lv_obj_t* parent, const std::string& text, int x, int y, int width, int height, std::function<void()> clicked, bool primary = false);
  lv_obj_t* field(const std::string& id, const std::string& value, const std::string& placeholder, int y, int maxLength, bool password);
  void event(lv_obj_t* object, lv_event_code_t code, std::function<void(lv_event_t*)> callback, bool row = false);
  void attachKeyboard();
  void focusField(lv_obj_t* input);
  void wifiForm(const std::string& ssid);
  void updateCards(std::uint32_t now);
  void updateCardMotion(std::uint32_t now);
  void updateDetails(std::uint32_t now);
  lv_obj_t* detailLabel(lv_obj_t* parent, const std::string& text, const lv_font_t* font, std::uint32_t color);
  void updateNetworks();
  void updatePicker();
  void updateSignIn();
  std::uint32_t signInStage_ = 0;
  void formStatus(const std::string& message, bool error = false);
  static bool setText(lv_obj_t* object, const std::string& text);
};

}  // namespace vibemon
