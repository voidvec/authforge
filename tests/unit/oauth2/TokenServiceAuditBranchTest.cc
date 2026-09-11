// Coverage additions (PR #197 security-audit fix batch): the new defensive
// branches in libs/oauth2 had no direct coverage and tripped the CI coverage
// ratchet (oauth2 87.8% < 88.8% baseline):
//   - TokenCrypto::generateSecureToken must return "" (not a deterministic
//     zero-buffer encoding) when the CSPRNG fails, and TokenService must
//     refuse issuance on that empty result in all three issuance paths;
//   - generateAuthorizationCode must reject a nonce longer than the
//     VARCHAR(512) oauth2_codes column before the insert can fail;
//   - JwkManager::loadPemInto must refuse RSA keys shorter than 2048 bits.
// The memory grant repository stores the full DTO, so the 512-char nonce
// boundary can also be round-tripped without a database.

#include <drogon/drogon_test.h>
#include <fulla/common/ports/ICryptoProvider.h>
#include <fulla/drogon/adapters/OpenSslCryptoProvider.h>
#include <fulla/oauth2/jwk/JwkManager.h>
#include <fulla/oauth2/protocol/TokenCrypto.h>
#include <fulla/oauth2/protocol/TokenService.h>
#include <fulla/storage/memory/MemoryRepositoryBundle.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <string>
#include <vector>

namespace
{

// ICryptoProvider wrapper whose secureRandomBytes starts failing after a
// configurable number of successful calls (failFrom == -1: never). Every
// other primitive forwards to the real OpenSSL provider, so hashes and
// encodes stay byte-accurate.
class FlakyCrypto final : public fulla::common::ports::ICryptoProvider
{
  public:
    explicit FlakyCrypto(int failFrom) : failFrom_(failFrom) {}

    std::vector<unsigned char> sha256(const std::string &data) override
    {
        return real_.sha256(data);
    }
    std::string sha256Hex(const std::string &data) override { return real_.sha256Hex(data); }
    bool secureRandomBytes(unsigned char *buffer, size_t length) override
    {
        if (failFrom_ >= 0 && calls_ >= failFrom_)
            return false;
        ++calls_;
        return real_.secureRandomBytes(buffer, length);
    }
    std::string base64UrlEncode(const unsigned char *bytes, size_t length) override
    {
        return real_.base64UrlEncode(bytes, length);
    }
    std::string base64UrlEncode(const std::string &data) override
    {
        return real_.base64UrlEncode(data);
    }
    std::vector<unsigned char> base64UrlDecode(const std::string &encoded) override
    {
        return real_.base64UrlDecode(encoded);
    }
    std::vector<unsigned char> hmacSha256(const std::string &key, const std::string &data) override
    {
        return real_.hmacSha256(key, data);
    }
    std::vector<unsigned char> pbkdf2HmacSha256(
      const std::string &p, const std::string &s, int i, size_t k
    ) override
    {
        return real_.pbkdf2HmacSha256(p, s, i, k);
    }
    std::vector<unsigned char> rsaSign(
      const std::string &pem, const std::string &alg, const std::string &data
    ) override
    {
        return real_.rsaSign(pem, alg, data);
    }

  private:
    fulla::drogon::adapters::OpenSslCryptoProvider real_;
    int failFrom_;
    int calls_ = 0;
};

std::shared_ptr<fulla::oauth2::protocol::TokenService> makeService(
  const std::shared_ptr<fulla::common::ports::ICryptoProvider> &crypto,
  fulla::storage::memory::MemoryRepositoryBundle &bundle)
{
    Json::Value clientsConfig;
    Json::Value client;
    client["client_type"] = "PUBLIC";
    client["redirect_uri"] = "http://cb";
    Json::Value scopes(Json::arrayValue);
    scopes.append("openid");
    client["allowed_scopes"] = scopes;
    clientsConfig["c"] = client;
    bundle.initFromConfig(clientsConfig);
    return std::make_shared<fulla::oauth2::protocol::TokenService>(
      bundle.clientRepository(), bundle.grantRepository(), bundle.tokenRepository(), crypto
    );
}

// Generate an RSA key of the requested bit size and write it as a <kid>.pem
// inside dir, plus the active_kid marker — the keystore-dir layout
// JwkManager::init consumes. Returns false on any OpenSSL/filesystem error.
bool writeWeakKey(const std::filesystem::path &dir, const std::string &kid, int bits)
{
    EVP_PKEY_CTX *ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    if (!ctx)
        return false;
    bool ok = false;
    do
    {
        if (EVP_PKEY_keygen_init(ctx) <= 0)
            break;
        if (EVP_PKEY_CTX_set_rsa_keygen_bits(ctx, bits) <= 0)
            break;
        EVP_PKEY *pkey = nullptr;
        if (EVP_PKEY_keygen(ctx, &pkey) <= 0)
            break;
        std::filesystem::create_directories(dir);
        std::ofstream pem(dir / (kid + ".pem"), std::ios::binary);
        BIO *bio = BIO_new(BIO_s_mem());
        if (!bio || PEM_write_bio_PrivateKey(bio, pkey, nullptr, nullptr, 0, nullptr, nullptr) != 1)
        {
            if (bio)
                BIO_free(bio);
            EVP_PKEY_free(pkey);
            break;
        }
        char *data = nullptr;
        long len = BIO_get_mem_data(bio, &data);
        pem.write(data, len);
        BIO_free(bio);
        EVP_PKEY_free(pkey);
        pem.close();
        std::ofstream marker(dir / "active_kid", std::ios::binary);
        marker << kid;
        ok = true;
    } while (false);
    EVP_PKEY_CTX_free(ctx);
    return ok;
}

}  // namespace

DROGON_TEST(Unit_OAuth2_TokenCrypto_CsprngFailure_ReturnsEmpty)
{
    FlakyCrypto crypto(0);  // every secureRandomBytes call fails
    CHECK(fulla::oauth2::protocol::generateSecureToken(crypto).empty());
    // The happy path still yields a non-empty token.
    FlakyCrypto healthy(-1);
    CHECK(!fulla::oauth2::protocol::generateSecureToken(healthy).empty());
}

DROGON_TEST(Unit_OAuth2_GenerateCode_CsprngFailure_Rejected)
{
    fulla::storage::memory::MemoryRepositoryBundle bundle;
    auto svc = makeService(std::make_shared<FlakyCrypto>(0), bundle);
    svc->generateAuthorizationCode(
      "c", "alice", "openid", "http://cb", "", "", "n",
      [&](bool success, std::string code, std::string error) {
          CHECK(!success);
          CHECK(code.empty());
          CHECK(error == "CSPRNG failure");
      });
}

DROGON_TEST(Unit_OAuth2_GenerateCode_OverlongNonce_Rejected)
{
    fulla::storage::memory::MemoryRepositoryBundle bundle;
    auto crypto = std::make_shared<FlakyCrypto>(-1);
    auto svc = makeService(crypto, bundle);
    svc->generateAuthorizationCode(
      "c", "alice", "openid", "http://cb", "", "", std::string(513, 'n'),
      [&](bool success, std::string code, std::string error) {
          CHECK(!success);
          CHECK(code.empty());
          CHECK(error.find("512") != std::string::npos);
      });
    // 512 (the column width) is still accepted and round-trips through the
    // memory grant repository (full-struct storage).
    std::string capturedCode;
    svc->generateAuthorizationCode(
      "c", "alice", "openid", "http://cb", "", "", std::string(512, 'n'),
      [&](bool success, std::string code, std::string) {
          CHECK(success);
          capturedCode = code;
      });
    auto grants = bundle.grantRepository();
    grants->getAuthCode(
      fulla::oauth2::protocol::hashToken(*crypto, capturedCode),
      [&](std::optional<fulla::oauth2::model::OAuth2AuthCode> stored) {
          REQUIRE(stored.has_value());
          CHECK(stored->nonce.size() == 512);
      });
}

DROGON_TEST(Unit_OAuth2_ExchangeCode_CsprngFailure_ServerError)
{
    fulla::storage::memory::MemoryRepositoryBundle bundle;
    // Call #1 mints the code; the exchange then needs three draws
    // (access/refresh/family) — failing from call #2 kills it mid-exchange.
    auto svc = makeService(std::make_shared<FlakyCrypto>(2), bundle);
    std::string code;
    svc->generateAuthorizationCode(
      "c", "alice", "openid", "http://cb", "", "", "n",
      [&](bool success, std::string c, std::string) {
          CHECK(success);
          code = c;
      });
    REQUIRE(!code.empty());
    svc->exchangeCodeForToken(code, "c", "", "http://cb", "", [&](const Json::Value &result) {
        CHECK(result["error"].asString() == "server_error");
    });
}

DROGON_TEST(Unit_OAuth2_JwkManager_RejectsSub2048RsaKey)
{
    const auto dir =
      std::filesystem::temp_directory_path() / (std::string("fulla_jwk_weak_") + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::remove_all(dir);
    REQUIRE(writeWeakKey(dir, "weak-kid", 512));

    fulla::oauth2::JwkManager manager(nullptr);
    Json::Value cfg;
    cfg["signing_keystore_dir"] = dir.string();
    CHECK(!manager.init(cfg));  // hard failure: weak key refused, no fallback
    CHECK(!manager.isInitialized());

    std::filesystem::remove_all(dir);
}

DROGON_TEST(Unit_OAuth2_JwkManager_Accepts2048RsaKey)
{
    const auto dir =
      std::filesystem::temp_directory_path() / (std::string("fulla_jwk_ok_") + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::remove_all(dir);
    REQUIRE(writeWeakKey(dir, "good-kid", 2048));

    fulla::oauth2::JwkManager manager(nullptr);
    Json::Value cfg;
    cfg["signing_keystore_dir"] = dir.string();
    CHECK(manager.init(cfg));
    CHECK(manager.isInitialized());

    std::filesystem::remove_all(dir);
}
