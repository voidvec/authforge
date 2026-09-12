#include <fulla/drogon/controllers/EmailVerificationController.h>
#include <fulla/drogon/services/EmailVerificationService.h>
#include <fulla/drogon/observability/openapi/OpenApiGenerator.h>

#include <drogon/drogon.h>

namespace fulla::drogon::controllers
{

namespace
{
struct EmailVerificationControllerDocs
{
    EmailVerificationControllerDocs()
    {
        ::fulla::drogon::observability::openapi::EndpointInfo verifyEmail;
        verifyEmail.path = "/api/verify-email";
        verifyEmail.method = "GET";
        verifyEmail.summary = "Verify Email";
        verifyEmail.description = "Verify an email address using a token.";
        verifyEmail.tags = {"User Verification"};
        verifyEmail.requiresAuth = false;
        ::fulla::drogon::observability::openapi::OpenApiGenerator::addEndpoint(verifyEmail);

        ::fulla::drogon::observability::openapi::EndpointInfo resendEmail;
        resendEmail.path = "/api/verify-email/resend";
        resendEmail.method = "POST";
        resendEmail.summary = "Resend Verification Email";
        resendEmail.description = "Resend the email verification link.";
        resendEmail.tags = {"User Verification"};
        resendEmail.requiresAuth = false;
        ::fulla::drogon::observability::openapi::OpenApiGenerator::addEndpoint(resendEmail);

        ::fulla::drogon::observability::openapi::EndpointInfo resendByEmail;
        resendByEmail.path = "/api/verify-email/resend-by-email";
        resendByEmail.method = "POST";
        resendByEmail.summary = "Resend Verification Email (by email address)";
        resendByEmail.description =
          "Resend the email verification link, identifying the account by "
          "email address. Unauthenticated (issue #198): a self-registered "
          "user holds no token yet, so the Bearer-gated /resend is "
          "unreachable for them. Rate-limited per (ip, email); the response "
          "is identical for unknown, already-verified, and emailed "
          "addresses (anti-enumeration).";
        resendByEmail.tags = {"User Verification"};
        resendByEmail.requiresAuth = false;
        ::fulla::drogon::observability::openapi::OpenApiGenerator::addEndpoint(resendByEmail);
    }
};

EmailVerificationControllerDocs docs_;
}  // namespace

}  // namespace fulla::drogon::controllers

namespace fulla::drogon::controllers
{

// Task B5: business logic extracted to EmailVerificationService.
// The controller only parses the request and delegates to the service.

void EmailVerificationController::verify(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));
    services::EmailVerificationService::verifyToken(req, sharedCb);
}

void EmailVerificationController::resend(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));
    services::EmailVerificationService::resendVerification(req, sharedCb);
}

void EmailVerificationController::resendByEmail(
  const ::drogon::HttpRequestPtr &req,
  std::function<void(const ::drogon::HttpResponsePtr &)> &&callback
)
{
    auto sharedCb =
      std::make_shared<std::function<void(const ::drogon::HttpResponsePtr &)>>(std::move(callback));
    services::EmailVerificationService::requestVerificationByEmail(req, sharedCb);
}

}  // namespace fulla::drogon::controllers
