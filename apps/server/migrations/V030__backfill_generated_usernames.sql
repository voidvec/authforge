-- Migration: V030__backfill_generated_usernames
-- Created: 2026-09-09
-- Purpose: U-2 (browser-e2e 2026-09-08) — the registration form promised
--          "leave the username blank and one is generated", but the server
--          stored NULL instead. Every such account read back username=''
--          (Drogon maps NULL columns to the type default), which defeated
--          the account-center Danger Zone confirm guard ('' === '' with
--          zero typing) and rendered the "Type <username> to confirm" hint
--          blank. Registration now generates user_<8 lowercase hex> at
--          insert time (identity AuthService + legacy fallback, both with
--          a collision retry); this migration backfills the same shape for
--          existing NULL rows so they get the same protection and a real
--          display name. md5(id || random()) gives every row a distinct
--          input; collision with a pre-existing username would fail the
--          unique constraint loudly (migration transaction aborts) rather
--          than silently leaving NULLs.

-- === UP ===
UPDATE users
SET username = 'user_' || substr(md5(id::text || random()::text), 1, 8)
WHERE username IS NULL;

-- === DOWN ===
-- Rollback intentionally restores nothing: NULL usernames are the defective
-- state this migration eliminates, and generated names may since have been
-- used as login identifiers.
