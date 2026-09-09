-- DEV ONLY: Sample OAuth2 client for Vue frontend development
-- DO NOT use in production!

-- U-6 (browser-e2e 2026-09-08): both loopback HOST spellings are registered.
-- The user portal's .env uses VITE_REDIRECT_URI=http://localhost:5173/callback
-- while this seed used to register only 127.0.0.1 — the MFA verify leg
-- strictly validates redirect_uri, so the drift locked every MFA-enabled
-- user out with a misleading "incorrect username or password".
INSERT INTO oauth2_clients (client_id, client_type, client_secret, salt, name, redirect_uris, allowed_grant_types, token_endpoint_auth_method)
VALUES (
    'vue-client',
    'PUBLIC',
    '42a121b66fb9f1d4f73125788f42eb6799110c6aeae5a9a12a2fed5307a0088d',
    'random_salt',
    'Vue Front-end Client',
    'http://127.0.0.1:5173/callback,http://localhost:5173/callback,http://127.0.0.1:8080/callback,http://localhost:8080/callback',
    'authorization_code,refresh_token',
    'none'
)
ON CONFLICT (client_id) DO NOTHING;

-- Grant default scopes to vue-client
INSERT INTO oauth2_client_scopes (client_id, scope_name)
SELECT 'vue-client', name
FROM oauth2_scopes
WHERE is_default = TRUE
ON CONFLICT (client_id, scope_name) DO NOTHING;
