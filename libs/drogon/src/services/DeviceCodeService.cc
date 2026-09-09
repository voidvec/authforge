#include <fulla/drogon/services/DeviceCodeService.h>

#include <fulla/storage/postgres/models/Oauth2DeviceCodes.h>

#include <drogon/drogon.h>
#include <drogon/orm/Exception.h>

namespace fulla::drogon::services
{

using namespace ::drogon::orm;
using namespace ::drogon_model::fulla_db;

void DeviceCodeService::createDeviceCode(
  const std::string &deviceCodeHash,
  const std::string &userCode,
  const std::string &clientId,
  const std::string &scope,
  int64_t expiresAt,
  int32_t intervalSeconds,
  ::drogon::orm::DbClientPtr db,
  std::function<void(bool)> &&callback
)
{
    Oauth2DeviceCodes code;
    code.setDeviceCodeHash(deviceCodeHash);
    code.setClientId(clientId);
    code.setScope(scope);
    code.setExpiresAt(expiresAt);
    code.setIntervalSeconds(intervalSeconds);
    code.setUserCode(userCode);
    code.setStatus("pending");

    try
    {
        Mapper<Oauth2DeviceCodes> mapper(db);
        auto sharedCb = std::make_shared<std::function<void(bool)>>(std::move(callback));
        mapper.insert(
          code,
          [sharedCb](const Oauth2DeviceCodes &) { (*sharedCb)(true); },
          [sharedCb](const DrogonDbException &e) {
              LOG_ERROR << "DeviceCodeService::createDeviceCode failed: " << e.base().what();
              (*sharedCb)(false);
          }
        );
    }
    catch (const std::exception &e)
    {
        LOG_ERROR << "DeviceCodeService::createDeviceCode Exception: " << e.what();
        if (callback)
            callback(false);
    }
    catch (...)
    {
        LOG_ERROR << "DeviceCodeService::createDeviceCode Unknown Exception";
        if (callback)
            callback(false);
    }
}

void DeviceCodeService::findByUserCode(
  const std::string &userCode,
  ::drogon::orm::DbClientPtr db,
  std::function<void(bool, std::shared_ptr<Oauth2DeviceCodes>)> &&callback
)
{
    // The callback is moved ONCE into a shared_ptr so both the row-found and
    // exception lambdas hold a live copy. A previous version moved the same
    // callback into each lambda; whichever lambda lost the race held an empty
    // std::function, and calling it aborted the event loop (bad_function_call)
    // — the crash formerly triggered by every /oauth2/device/approve miss.
    auto sharedCb =
      std::make_shared<std::function<void(bool, std::shared_ptr<Oauth2DeviceCodes>)>>(
        std::move(callback)
      );

    try
    {
        Mapper<Oauth2DeviceCodes> mapper(db);
        Criteria crit(Oauth2DeviceCodes::Cols::_user_code, CompareOperator::EQ, userCode);
        mapper.findOne(
          crit,
          [sharedCb](const Oauth2DeviceCodes &code) {
              (*sharedCb)(true, std::make_shared<Oauth2DeviceCodes>(code));
          },
          [sharedCb](const DrogonDbException &e) {
              // findOne reports "no such row" (and "more than one row") as
              // UnexpectedRows; that is a miss for the caller, not a DB fault.
              if (dynamic_cast<const UnexpectedRows *>(&e) != nullptr)
              {
                  (*sharedCb)(true, nullptr);
                  return;
              }
              LOG_ERROR << "DeviceCodeService::findByUserCode failed: " << e.base().what();
              (*sharedCb)(false, nullptr);
          }
        );
    }
    catch (const std::exception &e)
    {
        LOG_ERROR << "DeviceCodeService::findByUserCode Exception: " << e.what();
        (*sharedCb)(false, nullptr);
    }
    catch (...)
    {
        LOG_ERROR << "DeviceCodeService::findByUserCode Unknown Exception";
        (*sharedCb)(false, nullptr);
    }
}

void DeviceCodeService::markApproved(
  const std::string &deviceCodeHash,
  const std::string &userId,
  ::drogon::orm::DbClientPtr db,
  std::function<void(bool)> &&callback
)
{
    // Same shared-callback discipline as findByUserCode: the callback is moved
    // once; every lambda below captures the shared_ptr by value.
    auto sharedCb = std::make_shared<std::function<void(bool)>>(std::move(callback));

    try
    {
        Mapper<Oauth2DeviceCodes> mapper(db);
        Criteria crit(Oauth2DeviceCodes::Cols::_device_code_hash, CompareOperator::EQ, deviceCodeHash);
        mapper.findOne(
          crit,
          [db, userId, sharedCb](const Oauth2DeviceCodes &code) {
              Oauth2DeviceCodes updated = code;
              updated.setUserId(userId);
              updated.setStatus("approved");
              // Async callback scope: needs its own Mapper try/catch — the
              // outer one no longer covers us here (db-operations.md).
              try
              {
                  Mapper<Oauth2DeviceCodes>(db).update(
                    updated,
                    [sharedCb](const size_t) { (*sharedCb)(true); },
                    [sharedCb](const DrogonDbException &e) {
                        LOG_ERROR << "DeviceCodeService::markApproved update failed: "
                                  << e.base().what();
                        (*sharedCb)(false);
                    }
                  );
              }
              catch (const std::exception &e)
              {
                  LOG_ERROR << "DeviceCodeService::markApproved update Exception: " << e.what();
                  (*sharedCb)(false);
              }
              catch (...)
              {
                  LOG_ERROR << "DeviceCodeService::markApproved update Unknown Exception";
                  (*sharedCb)(false);
              }
          },
          [sharedCb](const DrogonDbException &e) {
              LOG_ERROR << "DeviceCodeService::markApproved find failed: " << e.base().what();
              (*sharedCb)(false);
          }
        );
    }
    catch (const std::exception &e)
    {
        LOG_ERROR << "DeviceCodeService::markApproved Exception: " << e.what();
        (*sharedCb)(false);
    }
    catch (...)
    {
        LOG_ERROR << "DeviceCodeService::markApproved Unknown Exception";
        (*sharedCb)(false);
    }
}

}  // namespace fulla::drogon::services
