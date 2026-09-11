#include <fulla/oauth2/protocol/TokenCrypto.h>

#include <cctype>
#include <vector>

namespace fulla::oauth2::protocol
{

std::string generateSecureToken(fulla::common::ports::ICryptoProvider &crypto, size_t bytes)
{
    std::vector<unsigned char> buffer(bytes);
    if (!crypto.secureRandomBytes(buffer.data(), bytes))
    {
        // P2-1 audit fix: CSPRNG failure must NOT mint tokens. The previous
        // behavior base64url-encoded the untouched (zero) buffer — a fully
        // deterministic, guessable credential. Return "" so callers treat an
        // empty token as issuance failure (fail-closed).
        return "";
    }
    return crypto.base64UrlEncode(buffer.data(), buffer.size());
}

std::string hashToken(
  fulla::common::ports::ICryptoProvider &crypto,
  const std::string &rawToken
)
{
    std::string hex = crypto.sha256Hex(rawToken);
    for (char &c : hex)
    {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return hex;
}

}  // namespace fulla::oauth2::protocol
