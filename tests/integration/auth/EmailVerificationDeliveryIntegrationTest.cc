// tests/integration/auth/EmailVerificationDeliveryIntegrationTest.cc
//
// Issue #198 fix: self-registered users could never verify their email —
// registration never sent the verification email, and the only resend
// endpoint requires a Bearer token the unverified user cannot obtain.
//
// These tests drive the REAL HTTP surface over the live in-process server
// (postgres mode) and assert the new wiring end to end:
//   1. POST /api/register (with email) now creates an
//      email_verification_tokens row for the new user (notifyNewRegistration
//      fired) — async, so poll briefly;
//   2. POST /api/verify-email/resend-by-email (unauthenticated) returns the
//      generic 200 and creates ANOTHER token row;
//   3. the same endpoint answers the identical generic 200 for an unknown
//      email and creates nothing (anti-enumeration);
//   4. GET /api/verify-email?token=<raw> consumes a token and flips
//      users.email_verified — the login-block precondition from the
//      deadlock is lifted.
//
// The raw token is only knowable to the mail recipient, so test 4 mints its
// own token row (raw + hashToken) — the endpoint contract is what is under
// test here, delivery is proven by tests 1-2.

#include <drogon/drogon_test.h>
#include <drogon/drogon.h>
#include <drogon/HttpClient.h>
#include <fulla/drogon/plugin/OAuth2Plugin.h>
#include <fulla/drogon/utils/CryptoUtils.h>
#include <fulla/storage/postgres/models/EmailVerificationTokens.h>
#include <fulla/storage/postgres/models/Users.h>
#include <json/json.h>

#include <chrono>
#include <string>
#include <thread>

using namespace drogon;
using namespace drogon::orm;

namespace
{
constexpr const char *kBaseUrl = "http://127.0.0.1:5555";

std::string uniqueSuffix()
{
    return std::to_string(
      std::chrono::system_clock::now().time_since_epoch().count());
}

bool parseBody(const HttpResponsePtr &resp, Json::Value &out)
{
    const std::string body(resp->getBody());
    Json::CharReaderBuilder builder;
    const std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
    std::string errs;
    return reader->parse(body.data(), body.data() + body.size(), &out, &errs);
}

HttpResponsePtr sendReq(const HttpRequestPtr &req)
{
    try
    {
        auto client = HttpClient::newHttpClient(kBaseUrl);
        auto [result, resp] = client->sendRequest(req, /*timeout=*/30.0);
        if (result != ReqResult::Ok || resp == nullptr)
            return nullptr;
        return resp;
    }
    catch (const std::exception &e)
    {
        LOG_WARN << "sendReq failed (server likely unreachable): " << e.what();
        return nullptr;
    }
}

bool serverReachable()
{
    for (int attempt = 0; attempt < 20; ++attempt)
    {
        auto req = HttpRequest::newHttpRequest();
        req->setMethod(Post);
        req->setPath("/nonexistent-probe");
        if (sendReq(req) != nullptr)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    return false;
}

// The CI aggregate runs this binary against a MEMORY-storage server (no
// db_clients configured): app().getDbClient() returns a null client there
// and any database-backed assertion would crash the whole aggregate. These
// tests are postgres-backed by design -- skip cleanly in memory mode.
bool postgresStorage()
{
    auto *plugin = app().getPlugin<OAuth2Plugin>();
    return plugin != nullptr && plugin->getStorageType() != "memory";
}

// Registers a user over HTTP with the given email. Returns true on 200.
bool registerUser(const std::string &username,
                  const std::string &password,
                  const std::string &email)
{
    Json::Value body;
    body["username"] = username;
    body["password"] = password;
    body["email"] = email;
    auto req = HttpRequest::newHttpJsonRequest(body);
    req->setMethod(Post);
    req->setPath("/api/register");
    auto resp = sendReq(req);
    return resp != nullptr && resp->getStatusCode() == k200OK;
}

// Counts verification tokens belonging to the user with the given email.
int countTokensForEmail(const std::string &email)
{
    auto db = app().getDbClient();
    auto fut = db->execSqlAsyncFuture(
      "SELECT count(*) AS n FROM email_verification_tokens v "
      "JOIN users u ON u.id = v.user_id WHERE u.email = $1",
      email);
    auto result = fut.get();
    if (result.empty())
        return -1;
    return result[0]["n"].as<int>();
}

// Polls until the token count for the email reaches `expected` (async
// delivery), or the deadline lapses.
bool waitForTokenCount(const std::string &email, int expected)
{
    for (int i = 0; i < 25; ++i)
    {
        if (countTokensForEmail(email) >= expected)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    return false;
}

// True when users.email_verified is set for the email.
bool emailVerified(const std::string &email)
{
    auto db = app().getDbClient();
    auto fut = db->execSqlAsyncFuture(
      "SELECT email_verified FROM users WHERE email = $1", email);
    auto result = fut.get();
    return !result.empty() && result[0]["email_verified"].as<bool>();
}

// Inserts a known verification token directly (raw + hash) for the user.
void mintVerificationToken(const std::string &email, const std::string &rawToken)
{
    auto db = app().getDbClient();
    auto fut = db->execSqlAsyncFuture(
      "INSERT INTO email_verification_tokens (token_hash, user_id, email, expires_at) "
      "SELECT $1, id, $2, extract(epoch from now())::bigint + 3600 "
      "FROM users WHERE email = $3",
      fulla::drogon::utils::hashToken(rawToken),
      email,
      email);
    fut.get();
}

// Leave-no-trace: later ctest entries share this database, and stray rows
// shift their branching (the coverage ratchet is sensitive to exactly that).
// Deleting the user cascades to tokens/roles/mappings (ON DELETE CASCADE).
void deleteUserByEmail(const std::string &email)
{
    auto db = app().getDbClient();
    auto fut = db->execSqlAsyncFuture("DELETE FROM users WHERE email = $1", email);
    fut.get();
}
}  // namespace

DROGON_TEST(Integration_P1_EmailVerification_RegistrationSendsVerificationEmail)
{
    if (!serverReachable())
    {
        LOG_INFO << "server unreachable - skipping live HTTP test";
        return;
    }
    const std::string suffix = uniqueSuffix();
    if (!postgresStorage())
    {
        LOG_INFO << "memory storage mode - skipping postgres-backed test";
        return;
    }
    const std::string username = "evreg" + suffix;
    const std::string email = "evreg" + suffix + "@example.com";

    CHECK(registerUser(username, "EvReg9-Password!", email));
    // Issue #198 core behavior: registration with an email creates a
    // verification token without any authenticated follow-up.
    CHECK(waitForTokenCount(email, 1));    deleteUserByEmail(email);
}

DROGON_TEST(Integration_P1_EmailVerification_ResendByEmail_Unauthenticated)
{
    if (!serverReachable())
    {
        LOG_INFO << "server unreachable - skipping live HTTP test";
        return;
    }
    const std::string suffix = uniqueSuffix();
    const std::string username = "evresend" + suffix;
    const std::string email = "evresend" + suffix + "@example.com";
    if (!postgresStorage())
    {
        LOG_INFO << "memory storage mode - skipping postgres-backed test";
        return;
    }
    REQUIRE(registerUser(username, "EvResend9-Password!", email));
    REQUIRE(waitForTokenCount(email, 1));

    // No Authorization header at all — the Bearer-gated /resend would 401.
    Json::Value body;
    body["email"] = email;
    auto req = HttpRequest::newHttpJsonRequest(body);
    req->setMethod(Post);
    req->setPath("/api/verify-email/resend-by-email");
    auto resp = sendReq(req);
    REQUIRE(resp != nullptr);
    CHECK(resp->getStatusCode() == k200OK);
    Json::Value parsed;
    REQUIRE(parseBody(resp, parsed));
    CHECK(parsed.get("message", "").asString().find("verification link has been sent") !=
          std::string::npos);

    // A second token row was created for the same user.
    CHECK(waitForTokenCount(email, 2));    deleteUserByEmail(email);
}

DROGON_TEST(Integration_P1_EmailVerification_ResendByEmail_UnknownEmail_AntiEnumeration)
{
    if (!serverReachable())
    {
        LOG_INFO << "server unreachable - skipping live HTTP test";
        return;
    }
    if (!postgresStorage())
    {
        LOG_INFO << "memory storage mode - skipping postgres-backed test";
        return;
    }
    const std::string email = "nobody-ev" + uniqueSuffix() + "@example.com";

    Json::Value body;
    body["email"] = email;
    auto req = HttpRequest::newHttpJsonRequest(body);
    req->setMethod(Post);
    req->setPath("/api/verify-email/resend-by-email");
    auto resp = sendReq(req);
    REQUIRE(resp != nullptr);
    // Identical generic 200 — the caller cannot distinguish known/unknown.
    CHECK(resp->getStatusCode() == k200OK);
    Json::Value parsed;
    REQUIRE(parseBody(resp, parsed));
    CHECK(parsed.get("message", "").asString().find("verification link has been sent") !=
          std::string::npos);
    CHECK(countTokensForEmail(email) == 0);
}

DROGON_TEST(Integration_P1_EmailVerification_VerifyTokenFlipsEmailVerified)
{
    if (!serverReachable())
    {
        LOG_INFO << "server unreachable - skipping live HTTP test";
        return;
    }
    const std::string suffix = uniqueSuffix();
    const std::string username = "evverify" + suffix;
    const std::string email = "evverify" + suffix + "@example.com";
    if (!postgresStorage())
    {
        LOG_INFO << "memory storage mode - skipping postgres-backed test";
        return;
    }
    REQUIRE(registerUser(username, "EvVerify9-Password!", email));

    // Mint a known token directly (raw token is only knowable to the mail
    // recipient in production) and consume it through the public endpoint.
    const std::string rawToken =
      "evverify-raw-" + suffix + "-abcdefghijklmnop";
    mintVerificationToken(email, rawToken);

    auto req = HttpRequest::newHttpRequest();
    req->setMethod(Get);
    req->setPath(std::string("/api/verify-email?token=") + rawToken);
    auto resp = sendReq(req);
    REQUIRE(resp != nullptr);
    CHECK(resp->getStatusCode() == k200OK);

    // The deadlock precondition is lifted: the account is verified.
    CHECK(emailVerified(email));

    // Token consumption is one-shot: replaying must fail.
    auto replay = sendReq(req);
    REQUIRE(replay != nullptr);
    CHECK(replay->getStatusCode() == k400BadRequest);    deleteUserByEmail(email);
}
