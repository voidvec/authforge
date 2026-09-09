#include <fulla/drogon/AuthService.h>
#include <fulla/storage/postgres/models/Users.h>
#include <fulla/storage/postgres/models/Roles.h>
#include <fulla/storage/postgres/models/UserRoles.h>
#include <fulla/drogon/utils/PasswordHasher.h>
#include <fulla/drogon/utils/CryptoUtils.h>
#include <fulla/common/utils/EmailNormalizer.h>
#include <drogon/utils/Utilities.h>
#include <algorithm>
#include <cctype>

using namespace drogon;
using namespace ::drogon::orm;

namespace fulla::drogon::services
{

namespace
{
// U-2 (browser-e2e 2026-09-08): generated username for email-first
// registrations that leave the username blank. Same shape as the identity
// AuthService's generator ("user_<8 lowercase hex>", charset-safe for Rule.h
// USERNAME_PATTERN); uniqueness is enforced by the users table and the
// registerUser retry loop covers the rare collision.
std::string generateUsername()
{
    // hashToken returns UPPERCASE hex; USERNAME_PATTERN allows mixed case,
    // but lowercase matches the identity-side generator byte-for-byte.
    std::string hex = fulla::drogon::utils::hashToken(
      fulla::drogon::utils::generateSecureToken(32)
    );
    std::transform(hex.begin(), hex.end(), hex.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return "user_" + hex.substr(0, 8);
}

// PR #180 review M1: one registration insert attempt. A plain function (not
// a std::function stored in a shared_ptr) makes the retry a plain recursive
// call — no self-referential shared_ptr exists, so there is no capture
// cycle to reason about, and every capture below is a strong, hop-surviving
// copy that releases when the chain terminates.
void attemptRegistrationInsert(
  const std::string &salt,
  const std::string &passwordHash,
  const std::string &email,
  bool generatedUsername,
  const std::shared_ptr<std::string> &currentUser,
  const std::shared_ptr<int> &attempts,
  const std::shared_ptr<std::function<void(const std::string &errorCode)>> &sharedCb
)
{
    drogon_model::fulla_db::Users newUser;
    // CHECK constraint forbids the empty string; generated names and
    // user-typed names are both non-empty here.
    newUser.setUsername(*currentUser);
    newUser.setPasswordHash(passwordHash);
    newUser.setSalt(salt);
    if (!email.empty())
        newUser.setEmail(fulla::common::utils::normalizeEmail(email));

    try
    {
        auto db = app().getDbClient();
        // Start Transaction? For now, just chain.

        auto mapper = Mapper<drogon_model::fulla_db::Users>(db);

        // Async Insert
        mapper.insert(
          newUser,
          [sharedCb, db](const drogon_model::fulla_db::Users &u) {
              // Assign Default Role "user"
              try
              {
                  auto roleMapper = Mapper<drogon_model::fulla_db::Roles>(db);
                  roleMapper.findOne(
                    Criteria(
                      drogon_model::fulla_db::Roles::Cols::_name, CompareOperator::EQ, "user"
                    ),
                    [sharedCb,
                     db,
                     userId = u.getValueOfId()](const drogon_model::fulla_db::Roles &role) {
                        try
                        {
                            auto urMapper = Mapper<drogon_model::fulla_db::UserRoles>(db);
                            drogon_model::fulla_db::UserRoles ur;
                            ur.setUserId(userId);
                            ur.setRoleId(role.getValueOfId());

                            urMapper.insert(
                              ur,
                              [sharedCb](const drogon_model::fulla_db::UserRoles &) {
                                  (*sharedCb)("");  // Success
                              },
                              [sharedCb](const DrogonDbException &e) {
                                  // Recoverable: the user has already been
                                  // created; role assignment is a side effect.
                                  // WARN is correct (not ERROR) because the
                                  // registration itself succeeds.
                                  LOG_WARN << "Assign Role Failed: " << e.base().what();
                                  (*sharedCb)("");  // Treat as success
                                                    // for now (User
                                                    // created), but log
                                                    // warning
                              }
                            );
                        }
                        catch (...)
                        {
                            (*sharedCb)("");
                        }
                    },
                    [sharedCb](const DrogonDbException &e) {
                        // Recoverable: user created without a role.
                        LOG_WARN << "Default Role 'user' not found: " << e.base().what();
                        (*sharedCb)("");  // User created w/o role
                    }
                  );
              }
              catch (...)
              {
                  (*sharedCb)("");
              }
          },
          [sharedCb, salt, passwordHash, email, generatedUsername, currentUser, attempts](
            const DrogonDbException &e) {
              const std::string what = e.base().what();
              LOG_ERROR << "Register Failed: " << what;
              // Map the failing DB constraint to a structured Error_Code so the
              // controller can forward it verbatim. Username conflict is checked
              // before email so a simultaneous conflict reports username first.
              if (what.find("users_username_key") != std::string::npos)
              {
                  // A GENERATED name can collide with an existing one; retry
                  // with a fresh name instead of surfacing "username taken"
                  // to a user who never typed a username.
                  if (generatedUsername && ++(*attempts) < 3)
                  {
                      *currentUser = generateUsername();
                      attemptRegistrationInsert(
                        salt, passwordHash, email, generatedUsername, currentUser, attempts, sharedCb
                      );
                      return;
                  }
                  (*sharedCb)("VALIDATION_USERNAME_TAKEN");
              }
              else if (what.find("idx_users_email_unique") != std::string::npos)
                  (*sharedCb)("VALIDATION_EMAIL_TAKEN");
              else
                  (*sharedCb)("VALIDATION_INVALID_INPUT");  // unrecognized constraint
          }
        );
    }
    catch (const std::exception &e)
    {
        LOG_ERROR << "Register Init Failed: " << e.what();
        (*sharedCb)("INTERNAL_ERROR");
    }
    catch (...)
    {
        LOG_ERROR << "Register Init Unknown Exception";
        (*sharedCb)("INTERNAL_ERROR");
    }
}
}  // namespace

void AuthService::validateUser(
  const std::string &identifier,
  const std::string &password,
  std::function<void(std::optional<AuthResult>)> &&callback
)
{
    auto sharedCb =
      std::make_shared<std::function<void(std::optional<AuthResult>)>>(std::move(callback));
    try
    {
        auto mapper = Mapper<drogon_model::fulla_db::Users>(app().getDbClient());

        // 登录标识分流：含 @ 视为 email（先归一再查），否则按 username 查
        // USERNAME_PATTERN 不允许 @，二者天然互斥
        bool isEmail = identifier.find('@') != std::string::npos;
        std::string lookupKey =
          isEmail ? fulla::common::utils::normalizeEmail(identifier) : identifier;
        auto criteria =
          isEmail
            ? Criteria(drogon_model::fulla_db::Users::Cols::_email, CompareOperator::EQ, lookupKey)
            : Criteria(
                drogon_model::fulla_db::Users::Cols::_username, CompareOperator::EQ, lookupKey
              );

        // Find user by login identifier (email or username)
        mapper.findOne(
          criteria,
          [sharedCb, password, identifier](const drogon_model::fulla_db::Users &user) {
              // Account lockout check
              auto now = std::chrono::duration_cast<std::chrono::seconds>(
                           std::chrono::system_clock::now().time_since_epoch()
              )
                           .count();

              int64_t lockedUntil = 0;
              int failedCount = 0;
              try
              {
                  // These columns may not exist in older schemas
                  lockedUntil = user.getValueOfLockedUntil();
                  failedCount = user.getValueOfFailedLoginCount();
              }
              catch (...)
              {
              }

              if (lockedUntil > now)
              {
                  LOG_WARN << "Account locked for user: " << identifier << " until " << lockedUntil;
                  (*sharedCb)(std::nullopt);
                  return;
              }

              // Compute Hash using PasswordHasher (PBKDF2 only; legacy
              // SHA-256 hashes fail verification since #103 — this legacy
              // fallback path has no migration window by design)
              std::string salt = user.getValueOfSalt();
              std::string dbHash = user.getValueOfPasswordHash();

              bool valid = fulla::common::utils::PasswordHasher::verify(password, dbHash, salt);

              if (valid)
              {
                  // Reset failed login count on success
                  if (failedCount > 0)
                  {
                      auto db = app().getDbClient();
                      auto resetUser = std::make_shared<drogon_model::fulla_db::Users>(user);
                      resetUser->setFailedLoginCount(0);
                      resetUser->setLockedUntil(0);
                      Mapper<drogon_model::fulla_db::Users>(db).update(
                        *resetUser,
                        [resetUser](const size_t) {},
                        [resetUser](const ::drogon::orm::DrogonDbException &e) {
                            LOG_ERROR << "Failed to reset failed login counter: "
                                      << e.base().what();
                        }
                      );
                  }

                  // #103: the legacy->PBKDF2 rehash-on-login block that used
                  // to live here is gone — with legacy verification retired,
                  // a legacy hash can no longer verify, making the upgrade
                  // path dead code. Legacy users migrate on the identity
                  // login path (window reopen) or via password reset.

                  AuthResult result;
                  result.internalId = user.getValueOfId();
                  result.publicSub = user.getValueOfPublicSub();
                  try
                  {
                      result.emailVerified = user.getValueOfEmailVerified();
                  }
                  catch (...)
                  {
                  }
                  try
                  {
                      result.mfaEnabled = user.getValueOfMfaEnabled();
                  }
                  catch (...)
                  {
                  }
                  try
                  {
                      result.mustChangePassword = user.getValueOfMustChangePassword();
                  }
                  catch (...)
                  {
                  }
                  (*sharedCb)(result);
              }
              else
              {
                  // Login failed - increment failed count and potentially lock
                  int newFailedCount = failedCount + 1;
                  int64_t newLockedUntil = 0;

                  // Progressive backoff: 5 fails = 1min, 10 = 5min, 15 = 30min, 20+ = 1hr
                  if (newFailedCount >= 20)
                      newLockedUntil = now + 3600;
                  else if (newFailedCount >= 15)
                      newLockedUntil = now + 1800;
                  else if (newFailedCount >= 10)
                      newLockedUntil = now + 300;
                  else if (newFailedCount >= 5)
                      newLockedUntil = now + 60;

                  auto db = app().getDbClient();
                  auto failedUser = std::make_shared<drogon_model::fulla_db::Users>(user);
                  failedUser->setFailedLoginCount(newFailedCount);
                  failedUser->setLockedUntil(newLockedUntil);
                  failedUser->setLastFailedLogin(now);
                  Mapper<drogon_model::fulla_db::Users>(db).update(
                    *failedUser,
                    [failedUser](const size_t) {},
                    [failedUser](const ::drogon::orm::DrogonDbException &e) {
                        LOG_WARN << "Failed to update failed login count: " << e.base().what();
                    }
                  );

                  (*sharedCb)(std::nullopt);
              }
          },
          [sharedCb](const DrogonDbException &e) {
              LOG_ERROR << "Validate User Failed: " << e.base().what();
              (*sharedCb)(std::nullopt);
          }
        );
    }
    catch (const std::exception &e)
    {
        LOG_ERROR << "Validate User Init Failed: " << e.what();
        (*sharedCb)(std::nullopt);
    }
    catch (...)
    {
        LOG_ERROR << "Validate User Init Unknown Exception";
        (*sharedCb)(std::nullopt);
    }
}

void AuthService::registerUser(
  const std::string &username,
  const std::string &password,
  const std::string &email,
  std::function<void(const std::string &errorCode)> &&callback
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const std::string &errorCode)>>(std::move(callback));
    // Hash Password with Argon2id
    std::string salt = "";  // Argon2id embeds its own salt
    std::string passwordHash;
    try
    {
        passwordHash = fulla::common::utils::PasswordHasher::hash(password);
    }
    catch (const std::exception &e)
    {
        LOG_ERROR << "Password hashing failed: " << e.what();
        (*sharedCb)("INTERNAL_ERROR");
        return;
    }

    // U-2: deliver the registration form's "leave the username blank and one
    // is generated" promise (previously stored NULL, which read back as ""
    // in the account center and defeated the Danger Zone confirm guard).
    const bool generatedUsername = username.empty();
    auto currentUser = std::make_shared<std::string>(
      generatedUsername ? generateUsername() : username
    );
    auto attempts = std::make_shared<int>(0);
    attemptRegistrationInsert(salt, passwordHash, email, generatedUsername, currentUser, attempts, sharedCb);
}

void AuthService::getUserInfo(
  int userId,
  std::function<void(std::optional<Json::Value> userInfo)> &&callback
)
{
    auto sharedCb = std::make_shared<std::function<void(std::optional<Json::Value> userInfo)>>(
      std::move(callback)
    );

    try
    {
        auto db = app().getDbClient();
        auto userMapper = Mapper<drogon_model::fulla_db::Users>(db);

        userMapper.findByPrimaryKey(
          userId,
          [sharedCb, db, userId](const drogon_model::fulla_db::Users &user) {
              // Fetch roles via UserRoles → Roles (split JOIN into two Mapper queries)
              Mapper<drogon_model::fulla_db::UserRoles> urMapper(db);
              urMapper.findBy(
                Criteria(
                  drogon_model::fulla_db::UserRoles::Cols::_user_id, CompareOperator::EQ, userId
                ),
                [sharedCb, db, user](
                  const std::vector<drogon_model::fulla_db::UserRoles> &userRoles
                ) {
                    if (userRoles.empty())
                    {
                        Json::Value json;
                        json["sub"] = user.getValueOfPublicSub();
                        std::string displayName = user.getValueOfUsername();
                        json["name"] = displayName.empty() ? user.getValueOfEmail() : displayName;
                        json["email"] = user.getValueOfEmail();
                        json["roles"] = Json::Value(Json::arrayValue);
                        (*sharedCb)(json);
                        return;
                    }

                    std::vector<int32_t> roleIds;
                    for (const auto &ur : userRoles)
                        roleIds.push_back(ur.getValueOfRoleId());

                    Mapper<drogon_model::fulla_db::Roles> roleMapper(db);
                    roleMapper.findBy(
                      Criteria(
                        drogon_model::fulla_db::Roles::Cols::_id, CompareOperator::In, roleIds
                      ),
                      [sharedCb, user](const std::vector<drogon_model::fulla_db::Roles> &roles) {
                          Json::Value json;
                          json["sub"] = user.getValueOfPublicSub();
                          std::string displayName = user.getValueOfUsername();
                          json["name"] = displayName.empty() ? user.getValueOfEmail() : displayName;
                          json["email"] = user.getValueOfEmail();
                          Json::Value rj(Json::arrayValue);
                          for (const auto &r : roles)
                              rj.append(r.getValueOfName());
                          json["roles"] = rj;
                          (*sharedCb)(json);
                      },
                      [sharedCb, user](const DrogonDbException &e) {
                          // Recoverable degradation: roles can't be loaded, so
                          // the user proceeds with an empty role set.
                          LOG_WARN << "User " << user.getValueOfPublicSub()
                                   << " has no roles: " << e.base().what();
                          Json::Value json;
                          json["sub"] = user.getValueOfPublicSub();
                          std::string displayName = user.getValueOfUsername();
                          json["name"] = displayName.empty() ? user.getValueOfEmail() : displayName;
                          json["email"] = user.getValueOfEmail();
                          json["roles"] = Json::Value(Json::arrayValue);
                          (*sharedCb)(json);
                      }
                    );
                },
                [sharedCb, user, userId](const DrogonDbException &e) {
                    LOG_WARN << "Failed to fetch roles for user " << userId << ": "
                             << e.base().what();
                    Json::Value json;
                    json["sub"] = user.getValueOfPublicSub();
                    std::string displayName = user.getValueOfUsername();
                    json["name"] = displayName.empty() ? user.getValueOfEmail() : displayName;
                    json["email"] = user.getValueOfEmail();
                    json["roles"] = Json::Value(Json::arrayValue);
                    (*sharedCb)(json);
                }
              );
          },
          [sharedCb](const DrogonDbException &e) {
              LOG_ERROR << "Get User Info Failed: " << e.base().what();
              (*sharedCb)(std::nullopt);
          }
        );
    }
    catch (const std::exception &e)
    {
        LOG_ERROR << "Get User Info Init Failed: " << e.what();
        (*sharedCb)(std::nullopt);
    }
    catch (...)
    {
        LOG_ERROR << "Get User Info Init Unknown Exception";
        (*sharedCb)(std::nullopt);
    }
}

}  // namespace fulla::drogon::services
