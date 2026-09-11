#include <fulla/identity/AuthService.h>
#include <fulla/identity/IUserRepository.h>
#include <fulla/common/utils/EmailNormalizer.h>

#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace fulla::identity
{

namespace
{

// PBKDF2-SHA256 parameters -- verbatim match of
// OAuth2Plugin/src/utils/PasswordHasher.cc's constants, so hashes produced
// here are byte-for-byte interchangeable with the existing production
// PasswordHasher (same DB, same stored-hash format:
// "$pbkdf2-sha256$<iterations>$<hex-salt>$<hex-hash>").
constexpr int kPbkdf2Iterations = 310000;
constexpr size_t kPbkdf2KeyLength = 32;
constexpr size_t kPbkdf2SaltLength = 16;

std::string bytesToHex(const unsigned char *data, size_t len)
{
    static const char hexChars[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(len * 2);
    for (size_t i = 0; i < len; ++i)
    {
        hex.push_back(hexChars[data[i] >> 4]);
        hex.push_back(hexChars[data[i] & 0x0F]);
    }
    return hex;
}

std::vector<unsigned char> hexToBytes(const std::string &hex)
{
    std::vector<unsigned char> bytes;
    bytes.reserve(hex.size() / 2);
    for (size_t i = 0; i + 1 < hex.size(); i += 2)
    {
        char hi = hex[i];
        char lo = hex[i + 1];
        auto nibble = [](char c) -> int {
            if (c >= '0' && c <= '9')
                return c - '0';
            if (c >= 'a' && c <= 'f')
                return c - 'a' + 10;
            if (c >= 'A' && c <= 'F')
                return c - 'A' + 10;
            return 0;
        };
        bytes.push_back(static_cast<unsigned char>((nibble(hi) << 4) | nibble(lo)));
    }
    return bytes;
}

// PR #180 review M1: one registration insert attempt. A plain function (not
// a std::function stored in a shared_ptr) makes the retry a plain recursive
// call — no self-referential shared_ptr exists, so there is no capture
// cycle to reason about, and every capture below is a strong, hop-surviving
// copy that releases when the chain terminates.
std::string generateUsername(fulla::common::ports::ICryptoProvider &crypto);

void attemptRegistrationInsert(
  const std::shared_ptr<IUserRepository> &userRepo,
  const std::shared_ptr<fulla::common::ports::ICryptoProvider> &crypto,
  const std::string &passwordHash,
  const std::string &email,
  bool generatedUsername,
  const std::shared_ptr<std::string> &currentUser,
  const std::shared_ptr<int> &attempts,
  const std::shared_ptr<std::function<void(const std::string &errorCode)>> &sharedCb
)
{
    UserData newUser;
    newUser.username = *currentUser;
    newUser.passwordHash = passwordHash;
    newUser.salt = "";  // PBKDF2 embeds its own salt in the hash string
    // Canonical form, mirroring the legacy path: storage, the unique index,
    // and the (normalized) login lookup must agree. (PR #180 review M7.)
    newUser.email = email.empty() ? std::string() : fulla::common::utils::normalizeEmail(email);

    userRepo->create(
      newUser,
      // IUserRepository::create() is responsible for default-role
      // assignment (repository-owned concern -- mirrors
      // OAuth2Server/AuthService.cc's registerUser, which assigns the
      // "user" role as part of the same transaction/continuation
      // chain rather than as a separate caller-driven step). It is
      // also responsible for classifying constraint-violation
      // failures into structured Error_Codes (e.g.
      // VALIDATION_USERNAME_TAKEN/VALIDATION_EMAIL_TAKEN) -- forward
      // verbatim, falling back to INTERNAL_ERROR only if the
      // repository didn't classify the failure.
      [=](std::optional<int32_t> newUserId, std::string errorCode) {
          if (newUserId)
          {
              (*sharedCb)("");
              return;
          }
          // A GENERATED name can collide with an existing one; retry
          // with a fresh name instead of surfacing "username taken" to
          // a user who never typed a username.
          if (errorCode == "VALIDATION_USERNAME_TAKEN" && generatedUsername &&
              ++(*attempts) < 3)
          {
              try
              {
                  *currentUser = generateUsername(*crypto);
              }
              catch (const std::exception &)
              {
                  (*sharedCb)("INTERNAL_ERROR");
                  return;
              }
              attemptRegistrationInsert(
                userRepo, crypto, passwordHash, email, generatedUsername, currentUser, attempts,
                sharedCb
              );
              return;
          }
          (*sharedCb)(errorCode.empty() ? "INTERNAL_ERROR" : errorCode);
      }
    );
}

bool isLegacyHash(const std::string &storedHash)
{
    return storedHash.find("$pbkdf2-sha256$") != 0;
}

// P1-14 audit fix: a fixed, syntactically valid PBKDF2 digest (random salt +
// random hash bytes, never a real credential) burned on the early-return
// paths below so a lookup miss costs the same PBKDF2 work as a hit —
// otherwise response timing enumerates which identifiers exist.
constexpr const char *kTimingEqualizerHash =
  "$pbkdf2-sha256$310000$6f1d9ab04e8c35f27c3a91bd4a70f5e2$"
  "3f9c1e7a2b8d4f60a5c3e19b7d2f8a4c6e0b3d7f1a9c5e2b8d4f6a0c3e7b1d95";

// U-2 (browser-e2e 2026-09-08): generated username for email-first
// registrations that leave the username blank ("user_<8 lowercase hex>",
// charset-safe for Rule.h USERNAME_PATTERN). Uniqueness is enforced by the
// users table; the registerUser retry loop covers the rare collision.
// PR #180 review M2: secureRandomBytes leaves the buffer untouched on
// failure (the contract TotpUtils.cc documents for its PR #157 review
// finding), so the return value MUST be checked — an unchecked read draws
// from uninitialized stack. Fail closed: registration surfaces
// INTERNAL_ERROR rather than creating an account on a broken RNG. (The
// legacy path is fail-closed too, via its fresh-UUID fallback.)
std::string generateUsername(fulla::common::ports::ICryptoProvider &crypto)
{
    unsigned char raw[4];
    if (!crypto.secureRandomBytes(raw, sizeof(raw)))
        throw std::runtime_error("secureRandomBytes failed");
    return "user_" + bytesToHex(raw, sizeof(raw));
}

std::string hashPassword(
  const std::string &password,
  fulla::common::ports::ICryptoProvider &crypto
)
{
    unsigned char salt[kPbkdf2SaltLength];
    crypto.secureRandomBytes(salt, kPbkdf2SaltLength);

    auto derived = crypto.pbkdf2HmacSha256(
      password,
      std::string(reinterpret_cast<char *>(salt), kPbkdf2SaltLength),
      kPbkdf2Iterations,
      kPbkdf2KeyLength
    );
    if (derived.size() != kPbkdf2KeyLength)
    {
        throw std::runtime_error("PBKDF2 hashing failed");
    }

    return "$pbkdf2-sha256$" + std::to_string(kPbkdf2Iterations) + "$" +
           bytesToHex(salt, kPbkdf2SaltLength) + "$" + bytesToHex(derived.data(), derived.size());
}

bool verifyPassword(
  const std::string &password,
  const std::string &storedHash,
  const std::string &legacySalt,
  fulla::common::ports::ICryptoProvider &crypto
)
{
    if (!isLegacyHash(storedHash))
    {
        // Parse "$pbkdf2-sha256$<iterations>$<hexsalt>$<hexhash>"
        std::vector<std::string> parts;
        std::string token;
        std::istringstream stream(storedHash);
        while (std::getline(stream, token, '$'))
        {
            if (!token.empty())
                parts.push_back(token);
        }
        if (parts.size() != 4)
            return false;

        int iterations = 0;
        try
        {
            iterations = std::stoi(parts[1]);
        }
        catch (...)
        {
            return false;
        }
        auto saltBytes = hexToBytes(parts[2]);
        auto expectedHash = hexToBytes(parts[3]);

        auto derived = crypto.pbkdf2HmacSha256(
          password, std::string(saltBytes.begin(), saltBytes.end()), iterations, kPbkdf2KeyLength
        );

        if (derived.size() != expectedHash.size())
            return false;

        int diff = 0;
        for (size_t i = 0; i < derived.size(); ++i)
            diff |= derived[i] ^ expectedHash[i];
        return diff == 0;
    }

    // Legacy SHA-256(password + salt) hex-digest verification.
    std::string inputHash = crypto.sha256Hex(password + legacySalt);
    if (inputHash.length() != storedHash.length())
        return false;

    std::string inputLower = inputHash;
    std::string storedLower = storedHash;
    std::transform(inputLower.begin(), inputLower.end(), inputLower.begin(), ::tolower);
    std::transform(storedLower.begin(), storedLower.end(), storedLower.begin(), ::tolower);

    int diff = 0;
    for (size_t i = 0; i < inputLower.length(); ++i)
        diff |= inputLower[i] ^ storedLower[i];
    return diff == 0;
}

}  // namespace

AuthService::AuthService(
  std::shared_ptr<IUserRepository> userRepo,
  std::shared_ptr<fulla::common::ports::ICryptoProvider> crypto,
  std::shared_ptr<fulla::common::ports::IClock> clock
)
    : userRepo_(std::move(userRepo)), crypto_(std::move(crypto)), clock_(std::move(clock))
{
}

void AuthService::validateUser(
  const std::string &identifier,
  const std::string &password,
  std::function<void(std::optional<AuthResult>)> &&callback
)
{
    if (!userRepo_ || !crypto_ || !clock_)
    {
        callback(std::nullopt);
        return;
    }

    auto sharedCb =
      std::make_shared<std::function<void(std::optional<AuthResult>)>>(std::move(callback));
    auto crypto = crypto_;
    auto clock = clock_;
    auto userRepo = userRepo_;

    // Login-identifier routing: contains '@' -> email lookup, else username.
    // Email identifiers are canonicalized before lookup (mirroring the
    // legacy AuthService): registration stores the canonical form, so a
    // mixed-case login input must not miss the row. Usernames stay
    // case-sensitive by design. (PR #180 review M7.)
    bool isEmail = identifier.find('@') != std::string::npos;
    const std::string lookupKey =
      isEmail ? fulla::common::utils::normalizeEmail(identifier) : identifier;

    // Value-capture the policy flag: async callbacks must not capture
    // `this` (db-operations rule 4 -- do not rely on the instance's
    // process-lifetime binding).
    const bool allowLegacy = allowLegacyHash_;
    const auto legacyRejectionNotifier = legacyHashRejectionNotifier_;
    auto onFound = [allowLegacy, legacyRejectionNotifier, sharedCb, crypto, clock, userRepo, password](std::optional<UserData> found) {
        if (!found)
        {
            // P1-14: same KDF cost as the found path (see kTimingEqualizerHash).
            verifyPassword(password, kTimingEqualizerHash, "", *crypto);
            (*sharedCb)(std::nullopt);
            return;
        }
        UserData user = *found;

        int64_t now = clock->nowSeconds();
        if (user.lockedUntil > now)
        {
            // P1-14: locked accounts also pay the KDF cost.
            verifyPassword(password, kTimingEqualizerHash, "", *crypto);
            (*sharedCb)(std::nullopt);
            return;
        }

        // #103 gate: when the migration window is closed (auth.
        // allow_legacy_hash=false, the assembly default), legacy-format
        // hashes are rejected outright -- no verify, no rehash. This is a
        // POLICY rejection, not a wrong password: it must not advance the
        // lockout counter (a username alone would otherwise let an
        // attacker lock any legacy user out, and a reopened window would
        // still be blocked by locked_until). The optional notifier is the
        // observability hook for the assembly layer (WARN + audit with
        // the internal id); the response stays the generic failure -- no
        // oracle about which rejection fired.
        if (!allowLegacy && isLegacyHash(user.passwordHash))
        {
            if (legacyRejectionNotifier)
                legacyRejectionNotifier(user.id);
            // P1-14: policy rejection pays the KDF cost too.
            verifyPassword(password, kTimingEqualizerHash, "", *crypto);
            (*sharedCb)(std::nullopt);
            return;
        }

        bool valid = verifyPassword(password, user.passwordHash, user.salt, *crypto);
        if (!valid)
        {
            userRepo->incrementFailedLogins(user.id, [](bool) {});
            (*sharedCb)(std::nullopt);
            return;
        }

        if (user.failedLoginCount > 0)
        {
            userRepo->resetFailedLogins(user.id, [](bool) {});
        }

        if (isLegacyHash(user.passwordHash))
        {
            try
            {
                std::string newHash = hashPassword(password, *crypto);
                userRepo->updatePasswordHash(user.id, newHash, [](bool) {});
            }
            catch (const std::exception &)
            {
                // Rehash failure is non-fatal to the login itself -- the
                // legacy hash still verified above.
            }
        }

        AuthResult result;
        result.internalId = user.id;
        result.publicSub = user.publicSub;
        result.emailVerified = user.emailVerified;
        result.mfaEnabled = user.mfaEnabled;
        result.mustChangePassword = user.mustChangePassword;
        (*sharedCb)(result);
    };

    if (isEmail)
        userRepo_->findByEmail(lookupKey, std::move(onFound));
    else
        userRepo_->findByUsername(identifier, std::move(onFound));
}

void AuthService::registerUser(
  const std::string &username,
  const std::string &password,
  const std::string &email,
  std::function<void(const std::string &errorCode)> &&callback
)
{
    if (!userRepo_ || !crypto_)
    {
        callback("INTERNAL_ERROR");
        return;
    }

    std::string passwordHash;
    try
    {
        passwordHash = hashPassword(password, *crypto_);
    }
    catch (const std::exception &)
    {
        callback("INTERNAL_ERROR");
        return;
    }

    // Value-capture the dependencies: async callbacks must not capture
    // `this` (db-operations rule 4 -- do not rely on the instance's
    // process-lifetime binding).
    auto sharedCb =
      std::make_shared<std::function<void(const std::string &errorCode)>>(std::move(callback));
    auto crypto = crypto_;
    auto userRepo = userRepo_;

    // U-2: deliver the registration form's "leave the username blank and one
    // is generated" promise. Storing NULL instead made every empty-username
    // account read username "" in the account center (Drogon maps a NULL
    // column to ""), which defeated the Danger Zone confirm guard.
    const bool generatedUsername = username.empty();
    auto currentUser = std::make_shared<std::string>();
    try
    {
        *currentUser = generatedUsername ? generateUsername(*crypto) : username;
    }
    catch (const std::exception &)
    {
        (*sharedCb)("INTERNAL_ERROR");
        return;
    }
    auto attempts = std::make_shared<int>(0);
    attemptRegistrationInsert(userRepo, crypto, passwordHash, email, generatedUsername, currentUser, attempts, sharedCb);
}

void AuthService::getUserInfo(
  int32_t userId,
  std::function<void(std::optional<Json::Value>)> &&callback
)
{
    if (!userRepo_)
    {
        callback(std::nullopt);
        return;
    }
    userRepo_->getUserInfoWithRoles(userId, std::move(callback));
}

}  // namespace fulla::identity
