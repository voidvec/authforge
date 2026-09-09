-- Migration: V031__normalize_user_emails
-- Created: 2026-09-09
-- Purpose: PR #180 review M7 — the legacy registration path stores the
--          canonical (lowercased) email and the legacy login normalizes the
--          identifier before lookup, but the wired identity path did NEITHER,
--          so mixed-case registrants were stored verbatim and only matched
--          exact-case input. Registration and login now canonicalize on both
--          paths; this migration lowercases existing rows so every stored
--          email agrees with the canonical form. Idempotent: the WHERE clause
--          leaves already-lowercase rows untouched.

-- === UP ===
-- A case-variant pair (Alice@x.com + alice@x.com) can coexist under the
-- case-sensitive unique index (V019); collapsing them violates
-- idx_users_email_unique and aborts the migration transaction loudly —
-- that is deliberate: such rows need manual dedup, not a silent merge.
UPDATE users
SET email = lower(email)
WHERE email <> lower(email);

-- === DOWN ===
-- Rollback intentionally restores nothing: the original mixed-case spellings
-- are unrecoverable after normalization, and re-widening the stored forms
-- would reintroduce the lookup mismatch this migration removes.
