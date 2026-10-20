#include "fakes.h"
#include "adapters/cloud_http.h"

int main() {
  try {
    {
      Update update; update.confirmationOk = false;
      Updater updater(update, "3.6.1");
      CHECK(updater.tick(1, true, true) && updater.view().confirmationFailed && update.confirms == 1);
      for (std::uint32_t now = 2; now < Updater::FirstCheckDelayMs; ++now) updater.tick(now, true, true);
      CHECK(update.confirms == 1);  // A storage failure must not write flash on every UI tick.
      updater.tick(Updater::FirstCheckDelayMs + 1, true, true);
      update.complete(firmwareRelease("3.6.2")); updater.tick(Updater::FirstCheckDelayMs + 2, true, true);
      CHECK(updater.view().confirmationFailed && update.installs.empty());
      update.confirmationOk = true; updater.checkNow();
      updater.tick(Updater::FirstCheckDelayMs + 3, true, true);
      CHECK(!updater.view().confirmationFailed && update.confirms == 3);
      update.complete(firmwareRelease("3.6.2")); updater.tick(Updater::FirstCheckDelayMs + 4, true, true);
      CHECK(update.installs.size() == 1 && update.confirms == 3);
    }
    for (const auto error : {CloudError::Unauthorized, CloudError::Forbidden}) {
      Store store; configured(store); Wifi wifi; Cloud cloud; Update update; Controller app(store, wifi, cloud, update, "3.6.1");
      app.begin(0); wifi.connected(); app.tick(1); cloud.complete({}, error); app.tick(2);
      const auto reason = app.workspace().message;
      wifi.current.link = WifiLink::Failed; app.tick(3);
      wifi.connected("Workshop", false); app.tick(4); wifi.connected(); app.tick(5);
      CHECK(app.workspace().connection == Connection::TokenRejected && app.workspace().message == reason);
      CHECK(cloud.requests.size() == 1);
      // A replacement may be checked, but cancelling it does not revive the rejected saved key.
      CHECK(app.saveCloud("https://other.example", "vm_" + std::string(60, 'b')).ok);
      app.tick(6); CHECK(cloud.requests.size() == 2);
      app.cancelSetup(); cloud.complete(catalog()); app.tick(7);
      CHECK(app.workspace().connection == Connection::TokenRejected && cloud.requests.size() == 2);
      app.refresh(); app.tick(8); CHECK(cloud.requests.size() == 3);
      cloud.complete(catalog()); app.tick(9); CHECK(app.workspace().connection == Connection::Online);
    }
    {
      // Cancelling a candidate restores even an intentionally empty saved workspace.
      Store store; configured(store); store.saved.panelsInitialized = true;
      Wifi wifi; Cloud cloud; Update update; Controller app(store, wifi, cloud, update, "3.6.1");
      app.begin(0); wifi.connected(); app.tick(1); cloud.complete(catalog()); app.tick(2);
      CHECK(app.saveCloud("https://other.example", "vm_" + std::string(60, 'b')).ok);
      app.tick(3); app.cancelSetup(); cloud.complete(catalog("discarded")); app.tick(4);
      CHECK(cloud.requests.size() == 3 && cloud.requests.back().origin == "https://vibemon.io");
      cloud.complete(catalog()); app.tick(5);
      CHECK(app.workspace().connection == Connection::Online && app.workspace().panels[0].id.empty());
    }
    {
      // Empty initialized panels need no background data; explicit refresh and the picker still work.
      Store store; configured(store); Wifi wifi; Cloud cloud; Update update; Controller app(store, wifi, cloud, update, "3.6.1");
      app.begin(0); wifi.connected(); app.tick(1); cloud.complete(catalog()); app.tick(2);
      for (std::size_t panel = 0; panel < PanelCount; ++panel) CHECK(app.select(panel, {}).ok);
      const auto requests = cloud.requests.size();
      for (std::uint32_t now = 10002; now <= 60002; now += PollIntervalMs) {
        app.tick(now);
        if (cloud.busy) { cloud.complete(catalog()); app.tick(now + 1); }
      }
      std::cout << "Empty-panel requests over 60 seconds: " << cloud.requests.size() - requests << '\n';
      CHECK(cloud.requests.size() == requests);
      app.refresh(); app.tick(60004); CHECK(cloud.requests.size() == requests + 1 && cloud.requests.back().scope.empty());
      // A failed check retries after its backoff instead of waiting for another refresh.
      cloud.complete({}, CloudError::Network); app.tick(60005);
      CHECK(app.workspace().connection == Connection::CloudUnavailable);
      app.tick(60005 + 2 * PollIntervalMs); CHECK(cloud.requests.size() == requests + 2);
      cloud.complete(catalog()); app.tick(60006 + 2 * PollIntervalMs);
      CHECK(app.workspace().connection == Connection::Online);
      app.tick(70005); CHECK(cloud.requests.size() == requests + 2 && app.workspace().panels[0].id.empty());
      wifi.current.link = WifiLink::Failed; app.tick(70006);
      wifi.connected(); app.tick(70007);
      CHECK(cloud.requests.size() == requests + 3);
      cloud.complete(catalog()); app.tick(70008);
      CHECK(app.workspace().connection == Connection::Online && app.workspace().panels[0].id.empty());
      // A dormant poll deadline can be older than due()'s signed comparison range.
      app.tick(30u * 86400000u);
      app.browse(true); app.tick(30u * 86400000u + 1);
      CHECK(cloud.requests.size() == requests + 4 && cloud.requests.back().scope.empty());
      cloud.complete(catalog()); app.tick(30u * 86400000u + 2); app.browse(false);
      CHECK(app.select(0, {ItemKind::Resource, "one"}).ok);
      app.tick(30u * 86400000u + 3);
      CHECK(cloud.requests.size() == requests + 5 && cloud.requests.back().scope == std::vector<Selection>({{ItemKind::Resource, "one"}}));
    }
    {
      Store store; configured(store); Wifi wifi; Cloud cloud; Update update; Controller app(store, wifi, cloud, update, "3.6.1");
      app.begin(0); wifi.connected(); app.tick(1); cloud.complete(catalog()); app.tick(2);
      const auto savedPanels = store.saved.panels;
      const auto writes = store.writes;
      CHECK(app.saveCloud("HTTPS://VIBEMON.IO:00443/", "").ok);
      app.tick(3); cloud.complete(catalog()); app.tick(4);
      CHECK(store.saved.panels == savedPanels && store.writes == writes);
    }
    {
      // A candidate server's rate limit does not delay the saved server.
      Store store; configured(store); Wifi wifi; Cloud cloud; Update update; Controller app(store, wifi, cloud, update, "3.6.1");
      app.begin(0); wifi.connected(); app.tick(1); cloud.complete(catalog()); app.tick(2);
      CHECK(app.saveCloud("https://other.example", "vm_" + std::string(60, 'b')).ok);
      app.tick(3); cloud.complete({}, CloudError::RateLimited); cloud.reply->retryAfterMs = 120000; app.tick(4);
      app.refresh(); app.tick(5);
      CHECK(cloud.requests.size() == 3 && cloud.requests.back().origin == "https://vibemon.io");
      cloud.complete(catalog()); app.tick(6);
      // Restoring the saved server must not forget the candidate's delay.
      CHECK(app.saveCloud("https://other.example", "vm_" + std::string(60, 'b')).ok);
      app.tick(7); CHECK(cloud.requests.size() == 3);
      app.tick(120003); CHECK(cloud.requests.size() == 3);
      app.tick(120004); CHECK(cloud.requests.size() == 4 && cloud.requests.back().origin == "https://other.example");
    }
    {
      // Trying and cancelling another server cannot bypass the saved server's delay.
      Store store; configured(store); Wifi wifi; Cloud cloud; Update update; Controller app(store, wifi, cloud, update, "3.6.1");
      app.begin(0); wifi.connected(); app.tick(1);
      cloud.complete({}, CloudError::RateLimited); cloud.reply->retryAfterMs = 120000; app.tick(2);
      CHECK(app.saveCloud("https://other.example", "vm_" + std::string(60, 'b')).ok);
      app.tick(3); CHECK(cloud.requests.size() == 2);
      app.cancelSetup(); cloud.complete(catalog()); app.tick(4);
      CHECK(cloud.requests.size() == 2);
      app.refresh(); app.tick(120001); CHECK(cloud.requests.size() == 2);
      app.tick(120002); CHECK(cloud.requests.size() == 3 && cloud.requests.back().origin == "https://vibemon.io");
    }
    {
      // A cancelled request still carries its server's delay, but cannot change the workspace.
      Store store; configured(store); Wifi wifi; Cloud cloud; Update update; Controller app(store, wifi, cloud, update, "3.6.1");
      app.begin(0); wifi.connected(); app.tick(1); cloud.complete(catalog()); app.tick(2);
      CHECK(app.saveCloud("https://other.example", "vm_" + std::string(60, 'b')).ok);
      app.tick(3); app.cancelSetup();
      cloud.complete({}, CloudError::RateLimited); cloud.reply->retryAfterMs = 120000; app.tick(4);
      CHECK(cloud.requests.size() == 3 && cloud.requests.back().origin == "https://vibemon.io");
      cloud.complete(catalog()); app.tick(5);
      CHECK(app.saveCloud("https://other.example", "vm_" + std::string(60, 'b')).ok);
      app.tick(6); CHECK(cloud.requests.size() == 3 && store.saved.origin == "https://vibemon.io");
      app.tick(120004); CHECK(cloud.requests.size() == 4 && cloud.requests.back().origin == "https://other.example");
    }
    {
      // Losing Wi-Fi while a limited reply waits must not discard its server's delay.
      Store store; configured(store); Wifi wifi; Cloud cloud; Update update; Controller app(store, wifi, cloud, update, "3.6.1");
      app.begin(0); wifi.connected(); app.tick(1); cloud.complete(catalog()); app.tick(2);
      CHECK(app.saveCloud("https://other.example", "vm_" + std::string(60, 'b')).ok);
      app.tick(3); wifi.current.link = WifiLink::Failed;
      cloud.complete({}, CloudError::RateLimited); cloud.reply->retryAfterMs = 120000; app.tick(4);
      app.cancelSetup(); wifi.connected(); app.tick(5); cloud.complete(catalog()); app.tick(6);
      CHECK(app.saveCloud("https://other.example", "vm_" + std::string(60, 'b')).ok);
      app.tick(7); CHECK(cloud.requests.size() == 3);
      app.tick(120004); CHECK(cloud.requests.size() == 4 && cloud.requests.back().origin == "https://other.example");
    }
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
             {CloudError::Memory, Connection::CloudUnavailable}, {CloudError::Server, Connection::CloudUnavailable},
             {CloudError::RateLimited, Connection::CloudUnavailable}, {CloudError::Redirected, Connection::CloudUnavailable}}) {
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
      // A rejected snapshot keeps the previous readings and names the decoder's reason.
      Store store; configured(store); Wifi wifi; Cloud cloud; Update update; Controller app(store, wifi, cloud, update, "3.4.0");
      app.begin(0); wifi.connected(); app.tick(1); cloud.complete(catalog()); app.tick(2);
      app.refresh(); app.tick(3); cloud.complete({}, CloudError::InvalidResponse, "A source contains invalid timestamps."); app.tick(4);
      CHECK(app.workspace().connection == Connection::CloudUnavailable && app.workspace().catalog.items.size() == 2);
      CHECK(app.workspace().message == "The server returned an invalid workspace. A source contains invalid timestamps.");
      app.refresh(); app.tick(5); cloud.complete({}, CloudError::TooLarge, "This device supports up to 128 reporting sources."); app.tick(6);
      CHECK(app.workspace().message == "The workspace exceeds this device's limits. This device supports up to 128 reporting sources.");
    }
    {
      // Retry-After lengthens the backoff; a redirect asks for the server address.
      Store store; configured(store); Wifi wifi; Cloud cloud; Update update; Controller app(store, wifi, cloud, update, "3.4.0");
      app.begin(0); wifi.connected(); app.tick(1);
      cloud.complete({}, CloudError::RateLimited); cloud.reply->retryAfterMs = 120000; app.tick(2);
      CHECK(app.workspace().message == "The server is limiting requests. Your last readings are shown.");
      // Neither a picker nor an explicit refresh asks before the server allows it.
      app.browse(true); app.refresh(); app.tick(3); app.browse(false); app.tick(4);
      app.tick(120001); CHECK(cloud.requests.size() == 1);
      app.tick(120002); CHECK(cloud.requests.size() == 2);
      cloud.complete({}, CloudError::RateLimited); cloud.reply->retryAfterMs = 1000; app.tick(120003);
      app.tick(120003 + 19999); CHECK(cloud.requests.size() == 2);
      app.tick(120003 + 20000); CHECK(cloud.requests.size() == 3);
      cloud.complete({}, CloudError::Redirected); app.tick(200000);
      CHECK(app.workspace().message == "The server redirected the request. Check the server address.");
    }
    {
      // Polls read only the selected panels; the picker and first defaults read every item.
      Store store; configured(store); Wifi wifi; Cloud cloud; Update update; Controller app(store, wifi, cloud, update, "3.4.0");
      app.begin(0); wifi.connected(); app.tick(1);
      CHECK(cloud.requests.back().scope.empty());
      cloud.complete(catalog()); app.tick(2);
      CHECK(app.workspace().catalogComplete && app.workspace().panels[0].id == "one");
      app.tick(2 + PollIntervalMs);
      const std::vector<Selection> panels{{ItemKind::Resource, "one"}, {ItemKind::Account, "codex:work"}};
      CHECK(cloud.requests.size() == 2 && cloud.requests.back().scope == panels);
      Catalog selected = catalog(); selected.items.pop_back();
      app.browse(true);
      cloud.complete(selected); app.tick(3 + PollIntervalMs);
      CHECK(!app.workspace().catalogComplete && app.workspace().message == "Connected");
      // The panel-only reply arrived while browsing, so the full list follows at once.
      app.tick(4 + PollIntervalMs);
      CHECK(cloud.requests.size() == 3 && cloud.requests.back().scope.empty());
      cloud.complete(catalog()); app.tick(5 + PollIntervalMs);
      CHECK(app.workspace().catalogComplete && app.workspace().catalog.items.size() == 2);
      app.tick(5 + 2 * PollIntervalMs); CHECK(cloud.requests.back().scope.empty());
      app.browse(false); cloud.complete(catalog()); app.tick(6 + 2 * PollIntervalMs);
      app.tick(6 + 3 * PollIntervalMs); CHECK(cloud.requests.back().scope == panels);
      cloud.complete({}); app.tick(7 + 3 * PollIntervalMs);
      CHECK(app.workspace().message == "Connected" && !app.workspace().catalogComplete);
      app.browse(true); app.tick(8 + 3 * PollIntervalMs);
      CHECK(cloud.requests.back().scope.empty());
      cloud.complete({}); app.tick(9 + 3 * PollIntervalMs);
      CHECK(app.workspace().message == "Connected. Waiting for your first report.");
    }
    CHECK(sourcesPath({}) == "/api/v1/sources");
    CHECK(sourcesPath({{ItemKind::Resource, "spark"}, {ItemKind::Resource, "k3s.main"}}) == "/api/v1/sources?ids=spark,k3s.main");
    CHECK(sourcesPath({{ItemKind::Account, "codex:work"}}) == "/api/v1/sources?accounts=codex:work");
    CHECK(sourcesPath({{ItemKind::Account, "codex:work"}, {ItemKind::Resource, "spark"}, {ItemKind::Account, "claude:me"}}) ==
          "/api/v1/sources?ids=spark&accounts=codex:work,claude:me");
    {
      // Refresh then picker, in that order, still waits for Retry-After.
      Store store; configured(store); Wifi wifi; Cloud cloud; Update update; Controller app(store, wifi, cloud, update, "3.4.0");
      app.begin(0); wifi.connected(); app.tick(1); cloud.complete(catalog()); app.tick(2);
      app.tick(2 + PollIntervalMs); CHECK(cloud.requests.size() == 2 && !cloud.requests.back().scope.empty());
      cloud.complete(catalog()); app.tick(3 + PollIntervalMs); CHECK(!app.workspace().catalogComplete);
      app.tick(3 + 2 * PollIntervalMs); CHECK(cloud.requests.size() == 3);
      cloud.complete({}, CloudError::RateLimited); cloud.reply->retryAfterMs = 120000; app.tick(4 + 2 * PollIntervalMs);
      app.refresh(); app.browse(true); app.tick(5 + 2 * PollIntervalMs);
      app.tick(3 + 2 * PollIntervalMs + 120000); CHECK(cloud.requests.size() == 3);
      app.tick(4 + 2 * PollIntervalMs + 120000); CHECK(cloud.requests.size() == 4 && cloud.requests.back().scope.empty());
    }
    {
      // An expired Retry-After is forgotten, so Refresh works after a long stop.
      Store store; configured(store); Wifi wifi; Cloud cloud; Update update; Controller app(store, wifi, cloud, update, "3.4.0");
      app.begin(0); wifi.connected(); app.tick(1);
      cloud.complete({}, CloudError::RateLimited); cloud.reply->retryAfterMs = 120000; app.tick(2);
      app.refresh(); app.tick(3); CHECK(cloud.requests.size() == 1);
      app.tick(120002); CHECK(cloud.requests.size() == 2);
      cloud.complete({}, CloudError::Unauthorized); app.tick(120003);
      CHECK(app.workspace().connection == Connection::TokenRejected);
      // 30 days later the old deadline would read as 18 days in the future.
      std::uint32_t now = 120003;
      for (int day = 0; day < 30; ++day) { now += 86400000; app.tick(now); }
      CHECK(cloud.requests.size() == 2);
      app.refresh(); app.tick(now + 1); CHECK(cloud.requests.size() == 3);
    }
    CHECK(cloudStatusError(200) == CloudError::None && cloudStatusError(401) == CloudError::Unauthorized);
    CHECK(cloudStatusError(403) == CloudError::Forbidden && cloudStatusError(429) == CloudError::RateLimited);
    CHECK(cloudStatusError(301) == CloudError::Redirected && cloudStatusError(308) == CloudError::Redirected);
    CHECK(cloudStatusError(404) == CloudError::InvalidResponse && cloudStatusError(503) == CloudError::Server);
    CHECK(retryAfterMs("120") == 120000u && retryAfterMs(" 0 ") == 0u && retryAfterMs("9999999999") == MaxRetryAfterMs);
    CHECK(!retryAfterMs("") && !retryAfterMs("-1") && !retryAfterMs("1.5") && !retryAfterMs("Wed, 21 Oct 2026 07:28:00 GMT"));
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
    {
      // A 304 for the request that returned the displayed catalog keeps it and
      // re-anchors its report clock at X-Server-Time.
      Store store; configured(store); Wifi wifi; Cloud cloud; Update update; Controller app(store, wifi, cloud, update, "3.4.0");
      const std::int64_t t0 = 1790000000000;
      const std::vector<Selection> panels{{ItemKind::Resource, "one"}, {ItemKind::Account, "codex:work"}};
      app.begin(0); wifi.connected(); app.tick(1);
      CHECK(cloud.requests.back().ifNoneMatch.empty());
      cloud.tagged(catalog(), "W/\"all-1\"", t0); app.tick(2);
      // The first defaults change the query, so the full read's ETag is not sent.
      app.tick(2 + PollIntervalMs);
      CHECK(cloud.requests.back().scope == panels && cloud.requests.back().ifNoneMatch.empty());
      cloud.tagged(catalog(), "W/\"panels-1\"", t0 + 10000); app.tick(3 + PollIntervalMs);
      app.tick(3 + 2 * PollIntervalMs);
      CHECK(cloud.requests.size() == 3 && cloud.requests.back().ifNoneMatch == "W/\"panels-1\"");
      const auto revision = app.workspace().revision;
      cloud.unchanged(t0 + 25000); app.tick(4 + 2 * PollIntervalMs);
      CHECK(app.workspace().connection == Connection::Online && app.workspace().catalog.items.size() == 2);
      CHECK(app.snapshotAgeMs(4 + 2 * PollIntervalMs) == 15000 && app.workspace().revision != revision);
      // A server clock behind the cached reply does not make reports younger than it.
      app.tick(4 + 3 * PollIntervalMs); CHECK(cloud.requests.back().ifNoneMatch == "W/\"panels-1\"");
      cloud.unchanged(t0); app.tick(5 + 3 * PollIntervalMs);
      CHECK(app.snapshotAgeMs(5 + 3 * PollIntervalMs) == 0);
      // Query switch: the picker reads every item without the panel ETag, and
      // returning to the panels starts unconditionally again.
      app.browse(true); app.tick(5 + 4 * PollIntervalMs);
      CHECK(cloud.requests.back().scope.empty() && cloud.requests.back().ifNoneMatch.empty());
      cloud.tagged(catalog(), "W/\"all-2\"", t0 + 40000); app.tick(6 + 4 * PollIntervalMs);
      app.tick(6 + 5 * PollIntervalMs); CHECK(cloud.requests.back().ifNoneMatch == "W/\"all-2\"");
      cloud.unchanged(t0 + 50000); app.tick(7 + 5 * PollIntervalMs);
      CHECK(app.workspace().catalogComplete && app.snapshotAgeMs(7 + 5 * PollIntervalMs) == 10000);
      app.browse(false); app.tick(7 + 6 * PollIntervalMs);
      CHECK(cloud.requests.back().scope == panels && cloud.requests.back().ifNoneMatch.empty());
      cloud.tagged(catalog(), "W/\"panels-2\"", t0 + 60000); app.tick(8 + 6 * PollIntervalMs);
      // A reply without an ETag makes the next request unconditional.
      app.tick(8 + 7 * PollIntervalMs); CHECK(cloud.requests.back().ifNoneMatch == "W/\"panels-2\"");
      cloud.complete(catalog()); app.tick(9 + 7 * PollIntervalMs);
      app.tick(9 + 8 * PollIntervalMs); CHECK(cloud.requests.back().ifNoneMatch.empty());
      cloud.tagged(catalog(), "W/\"panels-3\"", t0 + 80000); app.tick(10 + 8 * PollIntervalMs);
      // A new connection generation drops the ETag, even for the same server and key.
      CHECK(app.saveCloud("https://vibemon.io", "").ok);
      app.tick(11 + 8 * PollIntervalMs);
      CHECK(cloud.requests.back().scope.empty() && cloud.requests.back().ifNoneMatch.empty());
      cloud.tagged(catalog(), "W/\"all-3\"", t0 + 90000); app.tick(12 + 8 * PollIntervalMs);
      app.tick(12 + 9 * PollIntervalMs); CHECK(cloud.requests.back().ifNoneMatch.empty());
      cloud.tagged(catalog(), "W/\"panels-4\"", t0 + 100000); app.tick(13 + 9 * PollIntervalMs);
      // A failed decode drops the ETag; a network failure keeps it.
      app.tick(13 + 10 * PollIntervalMs); CHECK(cloud.requests.back().ifNoneMatch == "W/\"panels-4\"");
      cloud.complete({}, CloudError::Network); app.tick(14 + 10 * PollIntervalMs);
      app.refresh(); app.tick(15 + 10 * PollIntervalMs); CHECK(cloud.requests.back().ifNoneMatch == "W/\"panels-4\"");
      cloud.complete({}, CloudError::InvalidResponse, "A source contains invalid timestamps."); app.tick(16 + 10 * PollIntervalMs);
      CHECK(app.workspace().catalog.items.size() == 2);
      app.refresh(); app.tick(17 + 10 * PollIntervalMs); CHECK(cloud.requests.back().ifNoneMatch.empty());
      // A 304 for an unconditional request has no matching cached reply.
      cloud.unchanged(t0 + 120000); app.tick(18 + 10 * PollIntervalMs);
      CHECK(app.workspace().connection == Connection::CloudUnavailable && app.workspace().catalog.items.size() == 2);
      CHECK(app.workspace().message == "The server returned an invalid workspace. It reported no change without a matching earlier reply.");
      app.refresh(); app.tick(19 + 10 * PollIntervalMs); CHECK(cloud.requests.back().ifNoneMatch.empty());
    }
    {
      // A 304 without a cached reply in the controller is rejected even before any snapshot.
      Store store; configured(store); Wifi wifi; Cloud cloud; Update update; Controller app(store, wifi, cloud, update, "3.4.0");
      app.begin(0); wifi.connected(); app.tick(1);
      cloud.unchanged(1790000000000); app.tick(2);
      CHECK(app.workspace().connection == Connection::CloudUnavailable && !app.workspace().hasSnapshot);
    }
    CHECK(entityTag("W/\"abc-123\"") == "W/\"abc-123\"" && entityTag(" \"strong\" ") == "\"strong\"" && entityTag("\"\"") == "\"\"");
    CHECK(entityTag("").empty() && entityTag("abc").empty() && entityTag("W/abc").empty() && entityTag("\"a\"b\"").empty());
    CHECK(entityTag("\"a b\"").empty() && entityTag("\"a\r\nX: y\"").empty() && entityTag("W/\"" + std::string(MaxEntityTagBytes, 'a') + "\"").empty());
    {
      CloudResult result;
      notModifiedResult(true, "2026-10-11T10:00:05.000Z", result);
      CHECK(result.error == CloudError::None && result.notModified && result.serverTimeMs == *utcMilliseconds("2026-10-11T10:00:05.000Z"));
      CloudResult missing;
      notModifiedResult(true, "", missing);
      CHECK(missing.error == CloudError::InvalidResponse && !missing.notModified && !missing.detail.empty());
      CloudResult malformed;
      notModifiedResult(true, "Sun, 11 Oct 2026 10:00:05 GMT", malformed);
      CHECK(malformed.error == CloudError::InvalidResponse && !malformed.notModified);
      CloudResult unconditional;
      notModifiedResult(false, "2026-10-11T10:00:05.000Z", unconditional);
      CHECK(unconditional.error == CloudError::InvalidResponse && !unconditional.notModified);
    }
    std::cout << "PASS controller: setup, verified persistence, four-panel restore, polling, clock gate, stale replies, account isolation, rejected tokens, offline recovery, timer rollover, conditional polling and firmware updates\n";
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
