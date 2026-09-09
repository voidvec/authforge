#include <drogon/drogon_test.h>
#include <drogon/drogon.h>
#include <drogon/utils/Utilities.h>
#include <fulla/drogon/plugin/OAuth2Plugin.h>
#include <fulla/drogon/utils/CryptoUtils.h>
#include <fulla/storage/postgres/models/Users.h>
#include <fulla/common/utils/EmailNormalizer.h>
#include "HttpTestClient.h"
#include <chrono>
#include <future>
#include <regex>
#include <string>

using namespace drogon;
using namespace drogon::orm;

namespace
{
// Throwaway credential minted at runtime for the dedicated test user (fresh
// per run; nothing reusable leaves the process).
const std::string &registrationPassword()
{
    static const std::string v = fulla::drogon::utils::generateSecureToken(12) + "aA1!";
    return v;
}

// Drop any leftover row so the unique-email index (V019) doesn't trip reuse.
void cleanupEmail(const std::string &email)
{
    auto db = app().getDbClient();
    if (!db)
        return;
    std::promise<void> p;
    db->execSqlAsync(
      "DELETE FROM users WHERE email = $1",
      [&](const Result &) { p.set_value(); },
      [&](const DrogonDbException &) { p.set_value(); },
      fulla::common::utils::normalizeEmail(email)
    );
    p.get_future().get();
}
}  // namespace

// U-2 (browser-e2e 2026-09-08): the registration form promises "leave the
// username blank and one is generated for you". The service used to store
// NULL instead (email-first), which read back as "" in the account center
// and defeated the Danger Zone confirm guard. The contract is now: an
// email-only registration persists a GENERATED username of the shape
// user_<8 lowercase hex> (charset-valid for Rule.h USERNAME_PATTERN) plus a
// canonical email — never NULL.
DROGON_TEST(Integration_P1_Registration_EmailOnly_GeneratesUsername)
{
    auto plugin = app().getPlugin<OAuth2Plugin>();
    if (!plugin || plugin->getStorageType() == "memory")
    {
        CHECK(true);
        return;  // requires PostgreSQL
    }
    auto db = app().getDbClient();
    REQUIRE(db != nullptr);
    if (!fulla::test::http::serverReachable())
    {
        CHECK(true);
        return;
    }

    const std::string uniqueRun = std::to_string(
      std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
      )
        .count()
    );
    const std::string rawEmail = "AliceU2" + uniqueRun + "@Example.COM";
    cleanupEmail(rawEmail);

    // Real registration path: POST /api/register with no username field.
    const std::string form =
      "email=" + drogon::utils::urlEncodeComponent(rawEmail) +
      "&password=" + registrationPassword();
    auto resp = fulla::test::http::sendPostForm("/api/register", form);
    REQUIRE(resp != nullptr);
    CHECK(
      (resp->getStatusCode() == k200OK || resp->getStatusCode() == k201Created)
    );

    // Verify stored shape: generated username + CANONICAL email (both
    // registration paths normalize; PR #180 review M7 restored this
    // assertion once the identity path normalized too, and V031 backfills
    // pre-existing rows).
    const std::string usedEmail = fulla::common::utils::normalizeEmail(rawEmail);
    std::promise<bool> pRead;
    db->execSqlAsync(
      "SELECT username, email FROM users WHERE email = $1",
      [&](const Result &r) {
          bool ok = !r.empty();
          if (ok)
              ok = !r[0]["username"].isNull();
          if (ok)
          {
              const std::regex generatedPattern("^user_[0-9a-f]{8}$");
              ok = std::regex_match(r[0]["username"].as<std::string>(), generatedPattern);
          }
          if (ok)
              ok = r[0]["email"].as<std::string>() == usedEmail;
          pRead.set_value(ok);
      },
      [&](const DrogonDbException &) { pRead.set_value(false); },
      usedEmail
    );
    CHECK(pRead.get_future().get() == true);

    cleanupEmail(rawEmail);
}
