#include "fakes.h"
#include "adapters/device_auth.h"

namespace {
const std::string Origin = "https://vibemon.io";
const std::string Token = "vm_" + std::string(60, 'b');
const std::string Started = R"({"device_code":"device-secret","user_code":"BCDF-GHJK","verification_uri":"https://vibemon.io/device",)"
                            R"("verification_uri_complete":"https://vibemon.io/device?code=BCDF-GHJK","expires_in":600,"interval":5})";

std::string replaced(std::string text, const std::string& from, const std::string& to) {
  text.replace(text.find(from), from.size(), to);
  return text;
}

// Wi-Fi joined with a valid clock and no saved workspace.
struct Setup {
  Store store; Wifi wifi; Cloud cloud; Update update;
  Controller app{store, wifi, cloud, update, "3.10.0"};
  std::uint32_t now = 1;
  Setup() {
    store.result = LoadResult::Loaded; store.saved.ssid = "Workshop"; store.saved.wifiPassword = "test-password";
    app.begin(now); wifi.connected(); app.tick(++now);
  }
  void at(std::uint32_t time) { now = time; app.tick(now); }
};
}  // namespace

int main() {
  try {
    {
      // Request bodies are form-encoded and name only this client and the read scope.
      CHECK(deviceStartBody("Desk 1A2B") == "client_id=vibemon-touch&scope=read&device_name=Desk+1A2B");
      CHECK(deviceTokenBody("a/b") == "grant_type=urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Adevice_code&client_id=vibemon-touch&device_code=a%2Fb");
      const auto started = decodeDeviceStart(200, Started, Origin);
      CHECK(started.error == SignInError::None && started.code.userCode == "BCDF-GHJK" && started.code.deviceCode == "device-secret");
      CHECK(started.code.verificationUriComplete == "https://vibemon.io/device?code=BCDF-GHJK" && started.code.intervalS == 5 && started.code.expiresInS == 600);
      // A code is approved only on the server that issued it.
      CHECK(decodeDeviceStart(200, Started, "https://other.example").error == SignInError::InvalidResponse);
      CHECK(decodeDeviceStart(200, replaced(Started, "https://vibemon.io/device\",", "https://vibemon.io.evil/device\","), Origin).error == SignInError::InvalidResponse);
      for (const auto& [from, to] : std::initializer_list<std::pair<std::string, std::string>>{
               {"\"BCDF-GHJK\",\"verification_uri\"", "\"BCDE-GHJK\",\"verification_uri\""}, {"\"interval\":5", "\"interval\":0"},
               {"\"expires_in\":600", "\"expires_in\":\"600\""}, {"\"device-secret\"", "\"\""}}) {
        CHECK(decodeDeviceStart(200, replaced(Started, from, to), Origin).error == SignInError::InvalidResponse);
      }
      CHECK(decodeDeviceStart(404, "", Origin).error == SignInError::Unsupported);
      CHECK(decodeDeviceStart(400, R"({"error":"invalid_client"})", Origin).error == SignInError::Unsupported);
      CHECK(decodeDeviceStart(429, "", Origin).error == SignInError::RateLimited);
      CHECK(decodeDeviceStart(503, "", Origin).error == SignInError::Server);
      CHECK(decodeDeviceStart(302, "", Origin).error == SignInError::Redirected);
      const auto approved = decodeDeviceToken(200, R"({"access_token":")" + Token + R"(","token_type":"Bearer","scope":"read"})");
      CHECK(approved.error == SignInError::None && approved.token == Token);
      // A token with more than read permission, or of another format, is not accepted.
      CHECK(decodeDeviceToken(200, R"({"access_token":")" + Token + R"(","token_type":"Bearer","scope":"read write"})").error == SignInError::InvalidResponse);
      CHECK(decodeDeviceToken(200, R"({"access_token":"vm_short","token_type":"Bearer","scope":"read"})").error == SignInError::InvalidResponse);
      CHECK(decodeDeviceToken(400, R"({"error":"authorization_pending"})").error == SignInError::Pending);
      CHECK(decodeDeviceToken(400, R"({"error":"slow_down"})").error == SignInError::SlowDown);
      CHECK(decodeDeviceToken(400, R"({"error":"access_denied"})").error == SignInError::Denied);
      CHECK(decodeDeviceToken(400, R"({"error":"expired_token"})").error == SignInError::Expired);
      CHECK(decodeDeviceToken(400, R"({"error":"invalid_grant"})").error == SignInError::Expired);
      CHECK(decodeDeviceToken(400, "not json").error == SignInError::InvalidResponse);
    }
    {
      // Approval hands the token to the usual check, which saves it after the workspace answers.
      Setup setup; auto& cloud = setup.cloud; auto& app = setup.app;
      CHECK(!app.startSignIn("http://vibemon.io").ok);
      CHECK(app.startSignIn("vibemon.io").ok && app.signIn().stage == SignInStage::Requesting);
      setup.at(10);
      CHECK(cloud.signIns.size() == 1 && cloud.signIns[0].origin == Origin && cloud.signIns[0].deviceCode.empty());
      cloud.signedIn(Cloud::code()); setup.at(20);
      CHECK(app.signIn().stage == SignInStage::Waiting && app.signIn().userCode == "BCDF-GHJK");
      CHECK(app.signIn().verificationUriComplete == "https://vibemon.io/device?code=BCDF-GHJK");
      setup.at(5019); CHECK(cloud.signIns.size() == 1);  // Polls wait for the server's interval.
      setup.at(5020); CHECK(cloud.signIns.size() == 2 && cloud.signIns[1].deviceCode == "device-secret");
      cloud.signedIn(Cloud::error(SignInError::Pending)); setup.at(5030);
      setup.at(10029); CHECK(cloud.signIns.size() == 2);
      setup.at(10030); CHECK(cloud.signIns.size() == 3);
      // slow_down adds 5 seconds to every later interval.
      cloud.signedIn(Cloud::error(SignInError::SlowDown)); setup.at(10040);
      setup.at(20039); CHECK(cloud.signIns.size() == 3);
      setup.at(20040); CHECK(cloud.signIns.size() == 4);
      // A lost connection keeps the code and retries.
      cloud.signedIn(Cloud::error(SignInError::Network)); setup.at(20050);
      CHECK(app.signIn().stage == SignInStage::Waiting && !app.signIn().message.empty() && app.signIn().userCode == "BCDF-GHJK");
      setup.at(30050); CHECK(cloud.signIns.size() == 5);
      SignInResult granted; granted.token = Token;
      cloud.signedIn(granted); setup.at(30060);
      CHECK(app.signIn().stage == SignInStage::Approved && app.changingCloud());
      CHECK(cloud.requests.size() == 1 && cloud.requests[0].token == Token && cloud.requests[0].origin == Origin);
      CHECK(setup.store.saved.readToken.empty());  // Nothing is saved before the workspace answers.
      cloud.complete(catalog()); setup.at(30070);
      CHECK(app.workspace().connection == Connection::Online && setup.store.saved.readToken == Token && setup.store.saved.origin == Origin);
    }
    {
      // Denial, expiry and an unsupported server end the attempt with a reason.
      Setup setup; auto& cloud = setup.cloud; auto& app = setup.app;
      CHECK(app.startSignIn(Origin).ok); setup.at(10);
      cloud.signedIn(Cloud::code()); setup.at(20);
      setup.at(5020); cloud.signedIn(Cloud::error(SignInError::Denied)); setup.at(5030);
      CHECK(app.signIn().stage == SignInStage::Failed && app.signIn().message.find("denied") != std::string::npos);
      CHECK(cloud.requests.empty() && setup.store.saved.readToken.empty());
      // The code expires on the device's clock even while no poll can be sent.
      Setup other; auto& otherCloud = other.cloud;
      CHECK(other.app.startSignIn(Origin).ok); other.at(10);
      otherCloud.signedIn(Cloud::code(5, 60)); other.at(20);
      otherCloud.busy = true; other.at(60020);
      CHECK(other.app.signIn().stage == SignInStage::Failed && other.app.signIn().message.find("expired") != std::string::npos);
      Setup legacy;
      CHECK(legacy.app.startSignIn(Origin).ok); legacy.at(10);
      legacy.cloud.signedIn(Cloud::error(SignInError::Unsupported)); legacy.at(20);
      CHECK(legacy.app.signIn().stage == SignInStage::Failed && legacy.app.signIn().message.find("read token") != std::string::npos);
    }
    {
      // A reply to a cancelled or replaced attempt is ignored.
      Setup setup; auto& cloud = setup.cloud; auto& app = setup.app;
      CHECK(app.startSignIn(Origin).ok); setup.at(10);
      app.cancelSignIn();
      cloud.signedIn(Cloud::code()); setup.at(20);
      CHECK(app.signIn().stage == SignInStage::Idle && app.signIn().userCode.empty() && cloud.signIns.size() == 1);
      CHECK(app.startSignIn(Origin).ok); setup.at(30);
      CHECK(cloud.signIns.size() == 2 && cloud.signIns[1].attempt != cloud.signIns[0].attempt);
      // Sign-in waits while a workspace read holds the connection.
      Setup busy; busy.cloud.busy = true;
      CHECK(busy.app.startSignIn(Origin).ok); busy.at(10); CHECK(busy.cloud.signIns.empty());
      busy.cloud.busy = false; busy.at(20); CHECK(busy.cloud.signIns.size() == 1);
      // Without Wi-Fi and a clock there is nothing to sign in to.
      Store store; Wifi wifi; Cloud offline; Update update; Controller app2(store, wifi, offline, update, "3.10.0");
      app2.begin(1);
      CHECK(!app2.startSignIn(Origin).ok);
    }
    std::cout << "PASS sign-in: device codes, polling intervals, slow-down, retries, approval, denial, expiry, unsupported servers and cancelled attempts\n";
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
