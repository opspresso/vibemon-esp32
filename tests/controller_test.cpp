#include "fakes.h"

int main() {
  try {
    {
      Store store; Wifi wifi; Cloud cloud; Controller app(store, wifi, cloud);
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
      Store rebootedStore = store; Wifi rebootedWifi; Cloud rebootedCloud;
      Controller rebooted(rebootedStore, rebootedWifi, rebootedCloud); rebooted.begin(0);
      CHECK(rebooted.workspace().panels[0].id.empty() && rebooted.workspace().panels[1].id == "codex:work");
    }
    {
      Store store; configured(store); Wifi wifi; Cloud cloud; Controller app(store, wifi, cloud);
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
      Store store; configured(store); Wifi wifi; Cloud cloud; Controller app(store, wifi, cloud);
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
    {
      Store store; configured(store); Wifi wifi; Cloud cloud; Controller app(store, wifi, cloud);
      app.begin(0); wifi.connected(); app.tick(1); cloud.complete(catalog()); app.tick(2);
      const auto before = store.saved;
      CHECK(app.saveWifi("Other", "wrong-pass").ok);
      wifi.current.link = WifiLink::Failed; app.tick(3);
      CHECK(store.saved.ssid == before.ssid && app.workspace().connection == Connection::WifiUnavailable);
      CHECK(app.workspace().hasSnapshot && app.workspace().catalog.items.size() == 2);
      app.retryWifi(); CHECK(wifi.requested == "Workshop");
    }
    {
      Store store; configured(store); Wifi wifi; Cloud cloud; Controller app(store, wifi, cloud);
      app.begin(0); wifi.connected(); app.tick(1);
      store.writable = false; cloud.complete(catalog()); app.tick(2);
      CHECK(app.workspace().connection == Connection::StorageError && !app.workspace().hasSnapshot);
      app.tick(100000); CHECK(cloud.requests.size() == 1 && store.writes == 1);
      store.writable = true; app.refresh(); app.tick(100001); cloud.complete(catalog()); app.tick(100002);
      CHECK(app.workspace().connection == Connection::Online && store.writes == 2);
    }
    {
      Store store; configured(store); store.saved.origin = "http://unsafe.example";
      Wifi wifi; Cloud cloud; Controller app(store, wifi, cloud); app.begin(0);
      CHECK(app.workspace().connection == Connection::StorageError && wifi.connects == 0 && !app.workspace().tokenConfigured);
    }
    {
      Store store; configured(store); Wifi wifi; Cloud cloud; Controller app(store, wifi, cloud);
      const auto start = std::numeric_limits<std::uint32_t>::max() - 5;
      app.begin(start); wifi.connected(); app.tick(start); cloud.complete(catalog()); app.tick(start + 1);
      app.tick(2); CHECK(cloud.requests.size() == 1);
      app.tick(9996); CHECK(cloud.requests.size() == 2);
    }
    std::cout << "PASS controller: setup, verified persistence, four-panel restore, polling, clock gate, stale replies, account isolation, rejected tokens, offline recovery and timer rollover\n";
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
