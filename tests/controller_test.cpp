#include "fakes.h"

int main() {
  try {
    {
      Store store; Wifi wifi; Cloud cloud; Update update; Controller app(store, wifi, cloud, update, "3.4.0");
      app.begin(0);
      CHECK(app.workspace().connection == Connection::NeedWifi && wifi.scans == 1);
      CHECK(!app.saveWifi("", "password").ok && store.writes == 0);
      CHECK(app.saveWifi("Workshop", "test-password").ok && store.writes == 0);
      wifi.connected("Workshop", false); app.tick(100);
      CHECK(store.writes == 1 && app.workspace().connection == Connection::SettingClock && cloud.requests.empty());
      wifi.connected(); app.tick(200);
      CHECK(app.workspace().connection == Connection::NeedCloud);
      CHECK(!app.saveCloud("http://vibemon.io", "vm_" + std::string(60, 'b')).ok);
      CHECK(app.saveCloud("vibemon.io", "vm_" + std::string(60, 'b')).ok);
      app.tick(300); CHECK(cloud.requests.size() == 1 && store.writes == 1);
      cloud.complete(catalog()); app.tick(400);
      CHECK(app.workspace().connection == Connection::Online && store.writes == 2);
      CHECK(app.workspace().panels[0].id == "one" && app.workspace().panels[1].id == "codex:work");
      CHECK(store.saved.panelsInitialized && app.workspace().tokenConfigured);
      for (std::uint32_t tick = 401; tick < 10400; ++tick) app.tick(tick);
      CHECK(cloud.requests.size() == 1 && store.writes == 2);
      app.tick(10400); CHECK(cloud.requests.size() == 2);
      cloud.complete(catalog()); app.tick(10401); CHECK(store.writes == 2);
      CHECK(app.select(0, {ItemKind::Resource, "one"}).ok && store.writes == 2);
      CHECK(!app.select(4, {}).ok && !app.select(0, {ItemKind::Resource, "missing"}).ok);
      CHECK(app.select(0, {}).ok && store.writes == 3);
      app.refresh(); app.tick(10402); cloud.complete(catalog()); app.tick(10403);
      CHECK(app.workspace().panels[0].id.empty());
      Store rebootedStore = store; Wifi rebootedWifi; Cloud rebootedCloud; Update rebootedUpdate;
      Controller rebooted(rebootedStore, rebootedWifi, rebootedCloud, rebootedUpdate, "3.4.0"); rebooted.begin(0);
      CHECK(rebooted.workspace().panels[0].id.empty() && rebooted.workspace().panels[1].id == "codex:work");
    }
    {
      Store store; configured(store); Wifi wifi; Cloud cloud; Update update; Controller app(store, wifi, cloud, update, "3.4.0");
      app.begin(0); wifi.connected(); app.tick(1);
      CHECK(cloud.requests.size() == 1);
      CHECK(app.saveCloud("https://other.example", "vm_" + std::string(60, 'b')).ok);
      app.tick(2); CHECK(cloud.requests.size() == 1);
      cloud.complete(catalog("previous-owner")); app.tick(3);
      CHECK(!app.workspace().hasSnapshot && cloud.requests.size() == 2);
      CHECK(cloud.requests.back().origin == "https://other.example");
      cloud.complete(catalog("new-owner")); app.tick(4);
      CHECK(app.workspace().catalog.find({ItemKind::Resource, "new-owner"}));
      CHECK(!app.workspace().catalog.find({ItemKind::Resource, "previous-owner"}));
      CHECK(app.workspace().panels[0].id == "new-owner" && store.saved.origin == "https://other.example");
      CHECK(!app.saveCloud("https://third.example", "").ok);
      CHECK(app.saveCloud("https://other.example", "").ok);
    }
    {
      Store store; configured(store); Wifi wifi; Cloud cloud; Update update; Controller app(store, wifi, cloud, update, "3.4.0");
      app.begin(0); wifi.connected(); app.tick(1); cloud.complete(catalog()); app.tick(2);
      CHECK(app.saveCloud("https://wrong.example", "vm_" + std::string(60, 'b')).ok);
      app.tick(3); cloud.complete({}, CloudError::Unauthorized); app.tick(4);
      CHECK(app.workspace().connection == Connection::TokenRejected && store.saved.origin == "https://vibemon.io");
      CHECK(app.workspace().catalog.find({ItemKind::Resource, "one"}));
      app.refresh(); app.tick(5); CHECK(cloud.requests.back().origin == "https://vibemon.io");
      cloud.complete({}, CloudError::Forbidden); app.tick(6);
      const auto count = cloud.requests.size();
      app.tick(600000); CHECK(cloud.requests.size() == count);
      CHECK(app.workspace().connection == Connection::TokenRejected);
    }
    for (const auto& [error, expected] : std::initializer_list<std::pair<CloudError, Connection>>{
             {CloudError::Unauthorized, Connection::TokenRejected}, {CloudError::Forbidden, Connection::TokenRejected},
             {CloudError::Network, Connection::CloudUnavailable}, {CloudError::Tls, Connection::CloudUnavailable},
             {CloudError::InvalidResponse, Connection::CloudUnavailable}, {CloudError::TooLarge, Connection::CloudUnavailable},
             {CloudError::Memory, Connection::CloudUnavailable}, {CloudError::Server, Connection::CloudUnavailable}}) {
      // Without a saved token, a failed first setup keeps its cause instead of asking for setup again.
      Store store; store.result = LoadResult::Loaded; store.saved.ssid = "Workshop"; store.saved.wifiPassword = "test-password";
      Wifi wifi; Cloud cloud; Update update; Controller app(store, wifi, cloud, update, "3.4.0");
      app.begin(0); wifi.connected(); app.tick(1);
      CHECK(app.workspace().connection == Connection::NeedCloud);
      CHECK(app.saveCloud("vibemon.io", "vm_" + std::string(60, 'b')).ok);
      app.tick(2); cloud.complete({}, error); app.tick(3);
      const auto message = app.workspace().message;
      CHECK(app.workspace().connection == expected && !app.changingCloud());
      CHECK(message != "Enter your server address and a read token.");
      app.tick(100000);
      CHECK(app.workspace().connection == expected && app.workspace().message == message && cloud.requests.size() == 1);
      app.cancelSetup();
      CHECK(app.workspace().connection == Connection::NeedCloud && store.writes == 0);
    }
    {
      Store store; configured(store); Wifi wifi; Cloud cloud; Update update; Controller app(store, wifi, cloud, update, "3.4.0");
      app.begin(0); wifi.connected(); app.tick(1); cloud.complete(catalog()); app.tick(2);
      const auto before = store.saved;
      CHECK(app.saveWifi("Other", "wrong-pass").ok);
      wifi.current.link = WifiLink::Failed; app.tick(3);
      CHECK(store.saved.ssid == before.ssid && app.workspace().connection == Connection::WifiUnavailable);
      CHECK(app.workspace().hasSnapshot && app.workspace().catalog.items.size() == 2);
      app.retryWifi(); CHECK(wifi.requested == "Workshop");
    }
    {
      Store store; configured(store); Wifi wifi; Cloud cloud; Update update; Controller app(store, wifi, cloud, update, "3.4.0");
      app.begin(0); wifi.connected(); app.tick(1);
      store.writable = false; cloud.complete(catalog()); app.tick(2);
      CHECK(app.workspace().connection == Connection::StorageError && !app.workspace().hasSnapshot);
      app.tick(100000); CHECK(cloud.requests.size() == 1 && store.writes == 1);
      store.writable = true; app.refresh(); app.tick(100001); cloud.complete(catalog()); app.tick(100002);
      CHECK(app.workspace().connection == Connection::Online && store.writes == 2);
    }
    {
      Store store; configured(store); store.saved.origin = "http://unsafe.example";
      Wifi wifi; Cloud cloud; Update update; Controller app(store, wifi, cloud, update, "3.4.0"); app.begin(0);
      CHECK(app.workspace().connection == Connection::StorageError && wifi.connects == 0 && !app.workspace().tokenConfigured);
    }
    {
      Store store; configured(store); Wifi wifi; Cloud cloud; Update update; Controller app(store, wifi, cloud, update, "3.4.0");
      const auto start = std::numeric_limits<std::uint32_t>::max() - 5;
      app.begin(start); wifi.connected(); app.tick(start); cloud.complete(catalog()); app.tick(start + 1);
      app.tick(2); CHECK(cloud.requests.size() == 1);
      app.tick(9996); CHECK(cloud.requests.size() == 2);
    }
    {
      Store store; Wifi wifi; Cloud cloud; Update update; Controller app(store, wifi, cloud, update, "3.4.0"); app.begin(0);
      CHECK(app.saveWifi("Unverified", "test-password").ok);
      app.cancelSetup();
      CHECK(!app.changingWifi() && wifi.requested.empty() && store.writes == 0);
      CHECK(app.workspace().connection == Connection::NeedWifi);
    }
    {
      Store store; configured(store); Wifi wifi; Cloud cloud; Update update; Controller app(store, wifi, cloud, update, "3.4.0");
      app.begin(0); wifi.connected(); app.tick(1); cloud.complete(catalog()); app.tick(2);
      const auto writes = store.writes;
      CHECK(app.saveWifi("Workshop", "test-password").ok);
      wifi.connected(); app.tick(3); CHECK(store.writes == writes);
      cloud.complete(catalog()); app.tick(4);
      CHECK(app.saveCloud("https://other.example", "vm_" + std::string(60, 'b')).ok);
      CHECK(!app.select(0, {}).ok);
      app.tick(5); app.cancelSetup(); cloud.complete(catalog("canceled-owner")); app.tick(6);
      CHECK(!app.changingCloud() && store.saved.origin == "https://vibemon.io");
      CHECK(!app.workspace().catalog.find({ItemKind::Resource, "canceled-owner"}));
      CHECK(cloud.requests.back().origin == "https://vibemon.io");
    }
    {
      Store store; configured(store); Wifi wifi; Cloud cloud; Update update; Controller app(store, wifi, cloud, update, "3.4.0");
      app.begin(0); wifi.connected("Workshop", false); app.tick(1); app.tick(30001);
      CHECK(app.workspace().connection == Connection::ClockUnavailable && cloud.requests.empty());
      wifi.connected(); app.tick(30002); CHECK(cloud.requests.size() == 1);
    }
    {
      Store store; configured(store); Wifi wifi; Cloud cloud; Update update; Controller app(store, wifi, cloud, update, "3.4.0");
      app.begin(0); wifi.connected(); app.tick(1);
      auto fresh = catalog();
      fresh.items[0].ageMs = 0; fresh.items[0].health = Health::Ok;
      ReportState project; project.ageMs = 0; project.activity = "working"; project.health = Health::Ok;
      fresh.items[1].projects = {project};
      cloud.complete(fresh); app.tick(2);
      CHECK(app.snapshotAgeMs(2) == 0);
      wifi.current.link = WifiLink::Failed;
      constexpr std::uint64_t day = 86400000;
      for (std::uint64_t elapsed = day; elapsed <= 60 * day; elapsed += day) {
        const auto now = static_cast<std::uint32_t>(elapsed + 2);
        app.tick(now);
        CHECK(app.snapshotAgeMs(now) == elapsed);
        CHECK(app.snapshotAgeMs(now + 5) == elapsed + 5);
        CHECK(monitorState(app.workspace().catalog.items[0], app.snapshotAgeMs(now)) == "stale");
        CHECK(monitorState(app.workspace().catalog.items[1], app.snapshotAgeMs(now)) == "inactive");
      }
      const auto reconnected = static_cast<std::uint32_t>(60 * day + 3);
      wifi.connected(); app.tick(reconnected); cloud.complete(fresh); app.tick(reconnected + 1);
      CHECK(app.snapshotAgeMs(reconnected + 1) == 0);
      CHECK(monitorState(app.workspace().catalog.items[0], app.snapshotAgeMs(reconnected + 1)) == "ok");
    }
    {
      // A newer release installs, holds workspace polling and restarts into the new image.
      Store store; configured(store); Wifi wifi; Cloud cloud; Update update; Controller app(store, wifi, cloud, update, "3.4.0");
      app.begin(0); wifi.connected(); app.tick(1); cloud.complete(catalog()); app.tick(2);
      CHECK(app.workspace().connection == Connection::Online && update.checks == 0);
      app.tick(3); CHECK(update.confirms == 1);
      app.tick(Updater::FirstCheckDelayMs); CHECK(update.checks == 0);
      app.tick(Updater::FirstCheckDelayMs + 1);
      CHECK(update.checks == 1 && app.workspace().firmware.state == FirmwareState::Checking);
      cloud.complete(catalog()); update.complete(firmwareRelease("3.4.0"));
      app.tick(30002); CHECK(app.workspace().firmware.state == FirmwareState::Current && update.installs.empty());
      app.checkFirmware(); app.tick(30003); CHECK(update.checks == 2);
      update.complete(firmwareRelease("3.3.9")); app.tick(30004);
      CHECK(app.workspace().firmware.state == FirmwareState::Current && update.installs.empty());
      app.checkFirmware(); app.tick(30005); update.complete(firmwareRelease("3.5.0")); app.tick(30006);
      CHECK(update.installs.size() == 1 && update.installs[0].version == "3.5.0");
      CHECK(app.workspace().firmware.state == FirmwareState::Installing && app.workspace().firmware.available == "3.5.0");
      const auto requests = cloud.requests.size();
      app.tick(90000); CHECK(cloud.requests.size() == requests);
      update.percent = 40; const auto revision = app.workspace().revision; app.tick(90001);
      CHECK(app.workspace().firmware.progress == 40 && app.workspace().revision != revision);
      update.complete(firmwareRelease("3.5.0")); app.tick(90002);
      CHECK(app.workspace().firmware.state == FirmwareState::Restarting && update.restarts == 0);
      app.tick(90002 + Updater::RestartDelayMs - 1); CHECK(update.restarts == 0);
      app.tick(90002 + Updater::RestartDelayMs); CHECK(update.restarts == 1 && cloud.requests.size() == requests);
    }
    {
      // Failures retry later; a reached release server confirms the running image.
      Store store; configured(store); Wifi wifi; Cloud cloud; Update update; Controller app(store, wifi, cloud, update, "3.4.0");
      app.begin(0); wifi.connected("Workshop", false); app.tick(1);
      app.tick(Updater::FirstCheckDelayMs + 10); CHECK(update.checks == 0);
      wifi.connected(); app.tick(Updater::FirstCheckDelayMs + 11);
      const std::uint32_t first = 2 * Updater::FirstCheckDelayMs + 11;
      app.tick(first); CHECK(update.checks == 1 && update.confirms == 0);
      update.complete({}, UpdateError::Network); app.tick(first + 1);
      CHECK(app.workspace().firmware.state == FirmwareState::Unavailable && update.confirms == 0);
      app.tick(first + Updater::RetryDelayMs); CHECK(update.checks == 1);
      app.tick(first + 1 + Updater::RetryDelayMs); CHECK(update.checks == 2);
      update.complete(firmwareRelease("3.5.0")); app.tick(first + 2 + Updater::RetryDelayMs);
      CHECK(update.confirms == 1 && update.installs.size() == 1);
      update.complete({}, UpdateError::Integrity); app.tick(first + 3 + Updater::RetryDelayMs);
      CHECK(app.workspace().firmware.state == FirmwareState::Failed && app.workspace().firmware.available == "3.5.0");
      CHECK(update.restarts == 0);
      cloud.complete(catalog()); app.tick(first + 4 + Updater::RetryDelayMs);
      const auto requests = cloud.requests.size();
      app.tick(first + 5 + Updater::RetryDelayMs + PollIntervalMs); CHECK(cloud.requests.size() == requests + 1);
    }
    std::cout << "PASS controller: setup, verified persistence, four-panel restore, polling, clock gate, stale replies, account isolation, rejected tokens, offline recovery, timer rollover and firmware updates\n";
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
