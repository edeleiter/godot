# Authentication System

## Overview

The Claude module uses OAuth authentication via Anthropic, matching the Claude Code VS Code extension experience. Users authenticate through their browser without needing to manage API keys.

## Authentication Flow

### Primary: OAuth via Anthropic

```
┌─────────────────┐     ┌─────────────────┐     ┌─────────────────┐
│   Godot Editor  │     │     Browser     │     │  Anthropic API  │
└────────┬────────┘     └────────┬────────┘     └────────┬────────┘
         │                       │                       │
         │ 1. Click "Sign In"    │                       │
         ├──────────────────────►│                       │
         │   OS::shell_open()    │                       │
         │                       │ 2. OAuth Flow         │
         │                       ├──────────────────────►│
         │                       │                       │
         │                       │ 3. User Authorizes    │
         │                       │◄──────────────────────┤
         │                       │                       │
         │ 4a. Redirect to       │                       │
         │     localhost:PORT    │                       │
         │◄──────────────────────┤                       │
         │                       │                       │
         │ 5. Exchange code      │                       │
         │   for token           │                       │
         ├───────────────────────┼──────────────────────►│
         │                       │                       │
         │ 6. Receive tokens     │                       │
         │◄──────────────────────┼───────────────────────┤
         │                       │                       │
         │ 7. Store securely     │                       │
         │   & close browser     │                       │
         └───────────────────────┴───────────────────────┘
```

### Fallback: Device Code Flow

For environments where localhost redirect isn't possible:

```
1. Godot requests device code from Anthropic
2. User visits URL and enters code in browser
3. Godot polls for authorization completion
4. Tokens received and stored
```

## Implementation

### ClaudeAuth Class

```cpp
// modules/claude/api/claude_auth.h

#ifndef CLAUDE_AUTH_H
#define CLAUDE_AUTH_H

#include "core/object/ref_counted.h"
#include "scene/main/http_request.h"

class TCPServer;

class ClaudeAuth : public RefCounted {
    GDCLASS(ClaudeAuth, RefCounted);

public:
    enum AuthState {
        AUTH_STATE_SIGNED_OUT,
        AUTH_STATE_AWAITING_BROWSER,
        AUTH_STATE_EXCHANGING_TOKEN,
        AUTH_STATE_SIGNED_IN,
        AUTH_STATE_ERROR,
    };

private:
    // OAuth configuration
    String client_id = "godot-claude-module";
    String auth_endpoint = "https://console.anthropic.com/oauth/authorize";
    String token_endpoint = "https://api.anthropic.com/oauth/token";
    String redirect_uri;

    // Tokens
    String access_token;
    String refresh_token;
    uint64_t token_expiry = 0;

    // State
    AuthState state = AUTH_STATE_SIGNED_OUT;
    String oauth_state; // CSRF protection
    String code_verifier; // PKCE

    // Local server for OAuth redirect
    Ref<TCPServer> local_server;
    int local_port = 0;
    HTTPRequest *token_request = nullptr;

    // Internal methods
    String _generate_state();
    String _generate_code_verifier();
    String _generate_code_challenge(const String &p_verifier);
    void _start_local_server();
    void _stop_local_server();
    void _poll_local_server();
    void _handle_oauth_callback(const String &p_code);
    void _on_token_received(int p_result, int p_code,
                            const PackedStringArray &p_headers,
                            const PackedByteArray &p_body);
    void _save_tokens();
    void _load_tokens();
    Error _refresh_access_token();

protected:
    static void _bind_methods();
    void _notification(int p_what);

public:
    // Authentication
    Error start_oauth_flow();
    void cancel_oauth_flow();
    Error sign_out();

    // Token access
    String get_access_token();
    bool is_signed_in() const;
    bool needs_refresh() const;

    // State
    AuthState get_state() const { return state; }
    String get_user_email() const;

    ClaudeAuth();
    ~ClaudeAuth();
};

VARIANT_ENUM_CAST(ClaudeAuth::AuthState);

#endif
```

### OAuth Flow Implementation

```cpp
Error ClaudeAuth::start_oauth_flow() {
    ERR_FAIL_COND_V(state == AUTH_STATE_AWAITING_BROWSER, ERR_BUSY);

    // Generate PKCE codes
    oauth_state = _generate_state();
    code_verifier = _generate_code_verifier();
    String code_challenge = _generate_code_challenge(code_verifier);

    // Start local server to receive callback
    _start_local_server();
    redirect_uri = vformat("http://127.0.0.1:%d/callback", local_port);

    // Build authorization URL
    String auth_url = auth_endpoint;
    auth_url += "?response_type=code";
    auth_url += "&client_id=" + client_id.uri_encode();
    auth_url += "&redirect_uri=" + redirect_uri.uri_encode();
    auth_url += "&state=" + oauth_state.uri_encode();
    auth_url += "&code_challenge=" + code_challenge.uri_encode();
    auth_url += "&code_challenge_method=S256";
    auth_url += "&scope=chat";

    // Open browser
    state = AUTH_STATE_AWAITING_BROWSER;
    OS::get_singleton()->shell_open(auth_url);

    emit_signal("auth_state_changed", state);
    return OK;
}

void ClaudeAuth::_start_local_server() {
    local_server.instantiate();

    // Find available port in range 49152-65535
    for (int port = 49152; port < 65535; port++) {
        if (local_server->listen(port, "127.0.0.1") == OK) {
            local_port = port;
            return;
        }
    }

    ERR_FAIL_MSG("Could not find available port for OAuth callback");
}

void ClaudeAuth::_poll_local_server() {
    if (state != AUTH_STATE_AWAITING_BROWSER || !local_server.is_valid()) {
        return;
    }

    if (!local_server->is_connection_available()) {
        return;
    }

    Ref<StreamPeerTCP> connection = local_server->take_connection();
    if (!connection.is_valid()) {
        return;
    }

    // Read HTTP request
    String request;
    while (connection->get_available_bytes() > 0) {
        request += connection->get_utf8_string(connection->get_available_bytes());
    }

    // Parse request for code and state
    // GET /callback?code=XXX&state=YYY HTTP/1.1
    int query_start = request.find("?");
    int query_end = request.find(" HTTP");
    if (query_start == -1 || query_end == -1) {
        _send_error_response(connection, "Invalid callback");
        return;
    }

    String query = request.substr(query_start + 1, query_end - query_start - 1);
    Dictionary params = _parse_query_string(query);

    // Verify state
    if (params.get("state", "") != oauth_state) {
        _send_error_response(connection, "State mismatch - possible CSRF attack");
        return;
    }

    String code = params.get("code", "");
    if (code.is_empty()) {
        String error = params.get("error", "Unknown error");
        _send_error_response(connection, error);
        return;
    }

    // Send success response and close browser
    _send_success_response(connection);
    connection->disconnect_from_host();

    // Exchange code for tokens
    _handle_oauth_callback(code);
}

void ClaudeAuth::_handle_oauth_callback(const String &p_code) {
    _stop_local_server();
    state = AUTH_STATE_EXCHANGING_TOKEN;
    emit_signal("auth_state_changed", state);

    // Exchange authorization code for tokens
    Dictionary body;
    body["grant_type"] = "authorization_code";
    body["client_id"] = client_id;
    body["code"] = p_code;
    body["redirect_uri"] = redirect_uri;
    body["code_verifier"] = code_verifier;

    PackedStringArray headers;
    headers.push_back("Content-Type: application/json");

    token_request = memnew(HTTPRequest);
    token_request->set_use_threads(true);
    token_request->connect("request_completed",
        callable_mp(this, &ClaudeAuth::_on_token_received));

    // Add to scene tree for processing
    EditorNode::get_singleton()->add_child(token_request);

    token_request->request(token_endpoint, headers,
        HTTPClient::METHOD_POST, JSON::stringify(body));
}

void ClaudeAuth::_on_token_received(int p_result, int p_code,
                                    const PackedStringArray &p_headers,
                                    const PackedByteArray &p_body) {
    // Cleanup request node
    if (token_request) {
        token_request->queue_free();
        token_request = nullptr;
    }

    if (p_result != HTTPRequest::RESULT_SUCCESS || p_code != 200) {
        state = AUTH_STATE_ERROR;
        emit_signal("auth_state_changed", state);
        emit_signal("auth_error", "Failed to exchange token");
        return;
    }

    // Parse response
    String body_text = String::utf8((const char *)p_body.ptr(), p_body.size());
    JSON json;
    if (json.parse(body_text) != OK) {
        state = AUTH_STATE_ERROR;
        emit_signal("auth_error", "Invalid token response");
        return;
    }

    Dictionary response = json.get_data();
    access_token = response.get("access_token", "");
    refresh_token = response.get("refresh_token", "");

    int expires_in = response.get("expires_in", 3600);
    token_expiry = OS::get_singleton()->get_unix_time() + expires_in - 60; // 1 min buffer

    if (access_token.is_empty()) {
        state = AUTH_STATE_ERROR;
        emit_signal("auth_error", "No access token received");
        return;
    }

    // Save tokens securely
    _save_tokens();

    state = AUTH_STATE_SIGNED_IN;
    emit_signal("auth_state_changed", state);
    emit_signal("signed_in");
}
```

### Secure Token Storage

```cpp
void ClaudeAuth::_save_tokens() {
    // Encrypt tokens before storage
    String key = _get_machine_key(); // Derive from machine-specific data
    String encrypted_access = _encrypt(access_token, key);
    String encrypted_refresh = _encrypt(refresh_token, key);

    // Store in editor settings
    EditorSettings *settings = EditorSettings::get_singleton();
    settings->set("claude/auth/access_token", encrypted_access);
    settings->set("claude/auth/refresh_token", encrypted_refresh);
    settings->set("claude/auth/token_expiry", token_expiry);
}

void ClaudeAuth::_load_tokens() {
    EditorSettings *settings = EditorSettings::get_singleton();

    String encrypted_access = settings->get("claude/auth/access_token");
    String encrypted_refresh = settings->get("claude/auth/refresh_token");
    token_expiry = settings->get("claude/auth/token_expiry");

    if (encrypted_access.is_empty()) {
        return;
    }

    String key = _get_machine_key();
    access_token = _decrypt(encrypted_access, key);
    refresh_token = _decrypt(encrypted_refresh, key);

    if (!access_token.is_empty() && !needs_refresh()) {
        state = AUTH_STATE_SIGNED_IN;
    } else if (!refresh_token.is_empty()) {
        _refresh_access_token();
    }
}

String ClaudeAuth::_get_machine_key() {
    // Combine machine-specific identifiers
    String machine_id = OS::get_singleton()->get_unique_id();
    String user_data = OS::get_singleton()->get_user_data_dir();

    // Hash for consistent key length
    return String::md5((machine_id + user_data).utf8());
}

String ClaudeAuth::_encrypt(const String &p_data, const String &p_key) {
    // Use AES-256-GCM
    Ref<Crypto> crypto;
    crypto.instantiate();

    PackedByteArray key_bytes = p_key.to_utf8_buffer();
    PackedByteArray data_bytes = p_data.to_utf8_buffer();
    PackedByteArray iv = crypto->generate_random_bytes(12);

    PackedByteArray encrypted = crypto->encrypt(
        Crypto::MODE_AES_256_GCM, key_bytes, data_bytes, iv);

    // Prepend IV for decryption
    PackedByteArray result;
    result.append_array(iv);
    result.append_array(encrypted);

    return Marshalls::raw_to_base64(result);
}
```

### Token Refresh

```cpp
Error ClaudeAuth::_refresh_access_token() {
    if (refresh_token.is_empty()) {
        return ERR_UNAUTHORIZED;
    }

    Dictionary body;
    body["grant_type"] = "refresh_token";
    body["client_id"] = client_id;
    body["refresh_token"] = refresh_token;

    PackedStringArray headers;
    headers.push_back("Content-Type: application/json");

    HTTPRequest *refresh_request = memnew(HTTPRequest);
    refresh_request->set_use_threads(true);
    refresh_request->connect("request_completed",
        callable_mp(this, &ClaudeAuth::_on_token_received));

    EditorNode::get_singleton()->add_child(refresh_request);

    return refresh_request->request(token_endpoint, headers,
        HTTPClient::METHOD_POST, JSON::stringify(body));
}

String ClaudeAuth::get_access_token() {
    if (needs_refresh()) {
        _refresh_access_token();
    }
    return access_token;
}

bool ClaudeAuth::needs_refresh() const {
    return OS::get_singleton()->get_unix_time() >= token_expiry;
}
```

## UI Integration

### Sign-In Button in Dock

```cpp
void ClaudeDock::_build_auth_section() {
    auth_container = memnew(HBoxContainer);
    main_vbox->add_child(auth_container);

    // Status indicator
    auth_status = memnew(Label);
    auth_container->add_child(auth_status);

    auth_container->add_spacer();

    // Sign in/out button
    auth_button = memnew(Button);
    auth_button->connect("pressed",
        callable_mp(this, &ClaudeDock::_on_auth_button_pressed));
    auth_container->add_child(auth_button);

    _update_auth_ui();
}

void ClaudeDock::_update_auth_ui() {
    ClaudeAuth::AuthState state = auth->get_state();

    switch (state) {
        case ClaudeAuth::AUTH_STATE_SIGNED_OUT:
            auth_status->set_text("Not signed in");
            auth_button->set_text("Sign In");
            auth_button->set_disabled(false);
            input_field->set_editable(false);
            send_btn->set_disabled(true);
            break;

        case ClaudeAuth::AUTH_STATE_AWAITING_BROWSER:
            auth_status->set_text("Waiting for browser...");
            auth_button->set_text("Cancel");
            auth_button->set_disabled(false);
            break;

        case ClaudeAuth::AUTH_STATE_EXCHANGING_TOKEN:
            auth_status->set_text("Signing in...");
            auth_button->set_disabled(true);
            break;

        case ClaudeAuth::AUTH_STATE_SIGNED_IN:
            auth_status->set_text("Signed in as " + auth->get_user_email());
            auth_button->set_text("Sign Out");
            auth_button->set_disabled(false);
            input_field->set_editable(true);
            send_btn->set_disabled(false);
            break;

        case ClaudeAuth::AUTH_STATE_ERROR:
            auth_status->set_text("Authentication error");
            auth_button->set_text("Retry");
            auth_button->set_disabled(false);
            break;
    }
}

void ClaudeDock::_on_auth_button_pressed() {
    ClaudeAuth::AuthState state = auth->get_state();

    if (state == ClaudeAuth::AUTH_STATE_SIGNED_IN) {
        auth->sign_out();
    } else if (state == ClaudeAuth::AUTH_STATE_AWAITING_BROWSER) {
        auth->cancel_oauth_flow();
    } else {
        auth->start_oauth_flow();
    }
}
```

## Using Tokens for API Requests

```cpp
Error ClaudeClient::send_message_streaming(const String &p_message,
                                           const Dictionary &p_context) {
    // Get access token from auth
    String token = auth->get_access_token();
    ERR_FAIL_COND_V_MSG(token.is_empty(), ERR_UNAUTHORIZED,
        "Not signed in. Please authenticate first.");

    // Build headers with Bearer token
    PackedStringArray headers;
    headers.push_back("Content-Type: application/json");
    headers.push_back("Authorization: Bearer " + token);
    headers.push_back("anthropic-version: 2023-06-01");

    // ... rest of request implementation
}
```

## Security Considerations

1. **PKCE (Proof Key for Code Exchange)**: Prevents authorization code interception
2. **State Parameter**: Prevents CSRF attacks
3. **Localhost Redirect**: Callback stays on user's machine
4. **Encrypted Storage**: Tokens encrypted with machine-specific key
5. **Automatic Refresh**: Tokens refreshed before expiry
6. **Secure Erasure**: Tokens cleared from memory on sign-out

## Alternative: API Key Fallback

For enterprise users or offline scenarios, API key authentication remains available:

```cpp
String ClaudeClient::_get_authorization_header() {
    // Check OAuth first
    if (auth->is_signed_in()) {
        return "Bearer " + auth->get_access_token();
    }

    // Fall back to API key
    String api_key = EDITOR_GET("claude/api/key");
    if (!api_key.is_empty()) {
        return "x-api-key: " + api_key;
    }

    return "";
}
```
