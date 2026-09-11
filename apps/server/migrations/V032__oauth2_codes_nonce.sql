-- Migration: V032__oauth2_codes_nonce
-- Created: 2026-09-11
-- Purpose: Security audit 2026-09 P0-1 — persist the OIDC nonce on the
--          authorization code. Controllers already thread the request's
--          nonce into generateAuthorizationCode and the OAuth2AuthCode DTO
--          carries it, but no repository ever wrote it and the column did
--          not exist: the id_token's nonce claim was silently dropped at
--          code exchange (OIDC Core 3.1.3.7 requires the echo; 11.4 relies
--          on it as the injection/replay mitigation). Idempotent so the
--          startup auto-migration replays safely.

-- === UP ===
ALTER TABLE oauth2_codes ADD COLUMN IF NOT EXISTS nonce VARCHAR(512);

-- === DOWN ===
-- ALTER TABLE oauth2_codes DROP COLUMN IF EXISTS nonce;
