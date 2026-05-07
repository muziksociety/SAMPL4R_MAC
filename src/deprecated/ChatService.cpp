#include "ChatService.h"
#include "../app/App.h"
#include "../app/ServiceRegistry.h"
#include "../app/ServiceLocator.h"
#include "LicenseSystemService.h"

namespace samplar {

// ============================================================================
// Polling Timer Implementation
// ============================================================================

class ChatPollTimer : public juce::Timer {
public:
    explicit ChatPollTimer(ChatService* owner) : owner_(owner) {}

    void timerCallback() override {
        if (owner_) {
            // Run polling on background thread
            juce::Thread::launch([this] {
                if (owner_) {
                    owner_->pollMessages();
                }
            });
        }
    }

private:
    ChatService* owner_;
};

// ============================================================================
// Constructor / Destructor
// ============================================================================

ChatService::ChatService(EventBus& eventBus)
    : eventBus_(eventBus)
{
    juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " === CONSTRUCTOR START ===");

    pollTimer_ = std::make_unique<ChatPollTimer>(this);

    juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " === CONSTRUCTOR END ===");
}

ChatService::~ChatService()
{
    juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " === DESTRUCTOR START ===");
    shutdown();
    juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " === DESTRUCTOR END ===");
}

// ============================================================================
// IService Interface
// ============================================================================

void ChatService::initialise()
{
    juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " === INITIALISE START ===");

    // Get server URL from license service if available
    if (auto* registry = samplar::ServiceLocator::getRegistry()) {
        if (auto* licenseSvc = dynamic_cast<LicenseSystemService*>(registry->get("license"))) {
            std::string licenseServerUrl = licenseSvc->getServerUrl();
            if (!licenseServerUrl.empty()) {
                serverUrl_ = licenseServerUrl;
                juce::Logger::writeToLog(juce::String(LOG_PREFIX) + "   Server URL from license service: " + juce::String(serverUrl_));
            }
        }
    }

    // Subscribe to license status changes
    auto licenseHandler = eventBus_.subscribe("license.status", [this](const juce::var& payload) {
        juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " Received license.status event");
        // Reconnect if license becomes valid
        if (payload.isObject()) {
            auto* obj = payload.getDynamicObject();
            if (obj && obj->hasProperty("valid")) {
                bool valid = static_cast<bool>(obj->getProperty("valid"));
                if (valid && !isConnected()) {
                    juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " License became valid - attempting connection");
                    connect();
                }
            }
        }
    });
    subscriptions_.push_back({"license.status", licenseHandler});

    juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " === INITIALISE END ===");
}

void ChatService::shutdown()
{
    juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " === SHUTDOWN START ===");

    // Stop polling
    stopPolling();
    disconnect();

    // Unsubscribe from events
    for (const auto& [topic, id] : subscriptions_) {
        eventBus_.unsubscribe(topic, id);
    }
    subscriptions_.clear();

    // Clear callbacks
    {
        std::lock_guard<std::mutex> lock(callbacksMutex_);
        messageCallback_ = nullptr;
        statusCallback_ = nullptr;
        presenceCallback_ = nullptr;
    }

    juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " === SHUTDOWN END ===");
}

// ============================================================================
// Connection Management
// ============================================================================

void ChatService::connect()
{
    juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " === connect START ===");

    if (connectionStatus_ == ConnectionStatus::Connected ||
        connectionStatus_ == ConnectionStatus::Connecting) {
        juce::Logger::writeToLog(juce::String(LOG_PREFIX) + "   Already connected or connecting");
        return;
    }

    updateConnectionStatus(ConnectionStatus::Connecting, "Connecting to chat...");

    // Check authentication
    if (!isAuthenticated()) {
        juce::Logger::writeToLog(juce::String(LOG_PREFIX) + "   Not authenticated - auth required");
        updateConnectionStatus(ConnectionStatus::AuthRequired, "Please sign in to use chat");
        return;
    }

    // Fetch initial history
    fetchHistory();

    // If history fetch succeeded, we're connected
    if (connectionStatus_ == ConnectionStatus::Connecting) {
        updateConnectionStatus(ConnectionStatus::Connected, "Connected");
        startPolling();

        // Fetch user's nickname in background
        fetchNickname();
    }

    juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " === connect END ===");
}

void ChatService::disconnect()
{
    juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " === disconnect START ===");

    stopPolling();
    updateConnectionStatus(ConnectionStatus::Disconnected, "Disconnected");

    juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " === disconnect END ===");
}

bool ChatService::isConnected() const
{
    return connectionStatus_ == ConnectionStatus::Connected;
}

ChatService::ConnectionStatus ChatService::getConnectionStatus() const
{
    return connectionStatus_.load();
}

// ============================================================================
// Messaging
// ============================================================================

bool ChatService::sendMessage(const std::string& content)
{
    juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " === sendMessage START ===");
    juce::Logger::writeToLog(juce::String(LOG_PREFIX) + "   Content length: " + juce::String(static_cast<int>(content.length())));

    if (!isConnected()) {
        juce::Logger::writeToLog(juce::String(LOG_PREFIX) + "   ERROR: Not connected");
        return false;
    }

    // Validate content (matches server-side validation)
    std::string validationError;
    if (!validateContent(content, validationError)) {
        juce::Logger::writeToLog(juce::String(LOG_PREFIX) + "   ERROR: " + juce::String(validationError));
        return false;
    }

    // Check for duplicate message
    if (isDuplicate(content)) {
        juce::Logger::writeToLog(juce::String(LOG_PREFIX) + "   ERROR: Duplicate message blocked");
        return false;
    }

    // Check rate limit
    if (!checkRateLimit()) {
        juce::Logger::writeToLog(juce::String(LOG_PREFIX) + "   ERROR: Rate limited");
        return false;
    }

    bool success = doSendMessage(content);

    if (success) {
        recordMessageSent();
        recordMessageForDedupe(content);
        // Immediately poll for the new message
        pollMessages();
    }

    juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " === sendMessage END === result: " + juce::String(success ? "SUCCESS" : "FAILED"));
    return success;
}

// ============================================================================
// Content Validation (matches server-side moderation)
// ============================================================================

bool ChatService::validateContent(const std::string& content, std::string& errorOut) const
{
    // Check length (server: min 2, max 500)
    if (content.length() < 2) {
        errorOut = "Message too short (min 2 characters)";
        return false;
    }

    if (content.length() > 500) {
        errorOut = "Message too long (max 500 characters)";
        return false;
    }

    // Check for URLs
    if (containsUrl(content)) {
        errorOut = "Links are not allowed in chat";
        return false;
    }

    // Check for email addresses
    if (containsEmail(content)) {
        errorOut = "Email addresses are not allowed in chat";
        return false;
    }

    return true;
}

bool ChatService::containsUrl(const std::string& content) const
{
    juce::String text(content);
    juce::String lower = text.toLowerCase();

    // Check common URL prefixes
    if (lower.contains("http://") || lower.contains("https://") ||
        lower.contains("ftp://") || lower.contains("www.")) {
        return true;
    }

    // Check common TLDs (simplified check matching server regex)
    std::vector<std::string> tlds = {
        ".com", ".org", ".net", ".io", ".co", ".dev", ".app", ".xyz",
        ".info", ".biz", ".me", ".tv", ".cc", ".ru", ".cn", ".uk",
        ".de", ".fr", ".es", ".it", ".nl", ".be", ".ch", ".at",
        ".au", ".ca", ".br", ".jp", ".kr", ".in"
    };

    for (const auto& tld : tlds) {
        // Look for pattern like "word.tld" followed by space, slash, or end
        size_t pos = 0;
        while ((pos = lower.toStdString().find(tld, pos)) != std::string::npos) {
            // Check if there's a word character before the TLD
            if (pos > 0) {
                char before = lower.toStdString()[pos - 1];
                if (std::isalnum(before)) {
                    // Check what comes after the TLD
                    size_t afterPos = pos + tld.length();
                    if (afterPos >= lower.length() ||
                        lower.toStdString()[afterPos] == ' ' ||
                        lower.toStdString()[afterPos] == '/' ||
                        lower.toStdString()[afterPos] == '\n' ||
                        lower.toStdString()[afterPos] == '\r') {
                        return true;
                    }
                }
            }
            pos++;
        }
    }

    return false;
}

bool ChatService::containsEmail(const std::string& content) const
{
    // Simple email detection: word@word.word
    juce::String text(content);

    // Find @ symbol
    int atPos = text.indexOf("@");
    if (atPos < 1) return false;

    // Check for characters before @
    bool hasLocalPart = false;
    for (int i = atPos - 1; i >= 0; i--) {
        juce::juce_wchar c = text[i];
        if (c == ' ' || c == '\n' || c == '\r') break;
        if (std::isalnum(static_cast<int>(c)) || c == '.' || c == '_' || c == '-' || c == '+') {
            hasLocalPart = true;
        }
    }

    if (!hasLocalPart) return false;

    // Check for domain after @
    int dotPos = text.indexOf(atPos + 1, ".");
    if (dotPos < atPos + 2) return false;

    // Check there's something between @ and .
    bool hasDomain = false;
    for (int i = atPos + 1; i < dotPos; i++) {
        juce::juce_wchar c = text[i];
        if (std::isalnum(static_cast<int>(c)) || c == '-') {
            hasDomain = true;
        }
    }

    if (!hasDomain) return false;

    // Check there's a TLD after the dot
    bool hasTld = false;
    for (int i = dotPos + 1; i < text.length(); i++) {
        juce::juce_wchar c = text[i];
        if (c == ' ' || c == '\n' || c == '\r') break;
        if (std::isalpha(static_cast<int>(c))) {
            hasTld = true;
        }
    }

    return hasTld;
}

bool ChatService::isDuplicate(const std::string& content)
{
    std::lock_guard<std::mutex> lock(dedupeMutex_);

    auto now = std::chrono::steady_clock::now();
    auto cutoff = now - std::chrono::milliseconds(DEDUPE_WINDOW_MS);

    // Normalize content for comparison
    juce::String normalized = juce::String(content).toLowerCase().trim();
    std::string normalizedStr = normalized.toStdString();

    // Clean old entries and check for duplicates
    std::vector<std::pair<std::string, std::chrono::steady_clock::time_point>> validEntries;

    for (const auto& [msg, timestamp] : recentMessages_) {
        if (timestamp > cutoff) {
            validEntries.push_back({msg, timestamp});
            if (msg == normalizedStr) {
                return true;  // Found duplicate
            }
        }
    }

    recentMessages_ = std::move(validEntries);
    return false;
}

void ChatService::recordMessageForDedupe(const std::string& content)
{
    std::lock_guard<std::mutex> lock(dedupeMutex_);

    juce::String normalized = juce::String(content).toLowerCase().trim();
    recentMessages_.push_back({normalized.toStdString(), std::chrono::steady_clock::now()});

    // Keep only last 20 messages max
    while (recentMessages_.size() > 20) {
        recentMessages_.erase(recentMessages_.begin());
    }
}

std::vector<ChatService::ChatMessage> ChatService::getRecentMessages(int limit) const
{
    std::lock_guard<std::mutex> lock(messagesMutex_);

    if (static_cast<int>(cachedMessages_.size()) <= limit) {
        return cachedMessages_;
    }

    return std::vector<ChatMessage>(
        cachedMessages_.end() - limit,
        cachedMessages_.end()
    );
}

// ============================================================================
// Polling Control
// ============================================================================

void ChatService::startPolling()
{
    juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " === startPolling START ===");

    if (pollingActive_) {
        juce::Logger::writeToLog(juce::String(LOG_PREFIX) + "   Already polling");
        return;
    }

    pollingActive_ = true;
    pollTimer_->startTimer(pollingIntervalMs_);

    juce::Logger::writeToLog(juce::String(LOG_PREFIX) + "   Polling started with interval: " + juce::String(pollingIntervalMs_) + "ms");
    juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " === startPolling END ===");
}

void ChatService::stopPolling()
{
    juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " === stopPolling START ===");

    pollingActive_ = false;
    pollTimer_->stopTimer();

    juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " === stopPolling END ===");
}

bool ChatService::isPolling() const
{
    return pollingActive_;
}

// ============================================================================
// Callbacks
// ============================================================================

void ChatService::setMessageCallback(MessageCallback callback)
{
    std::lock_guard<std::mutex> lock(callbacksMutex_);
    messageCallback_ = std::move(callback);
}

void ChatService::setStatusCallback(StatusCallback callback)
{
    std::lock_guard<std::mutex> lock(callbacksMutex_);
    statusCallback_ = std::move(callback);
}

void ChatService::setPresenceCallback(PresenceCallback callback)
{
    std::lock_guard<std::mutex> lock(callbacksMutex_);
    presenceCallback_ = std::move(callback);
}

// ============================================================================
// Configuration
// ============================================================================

void ChatService::setServerUrl(const std::string& url)
{
    serverUrl_ = url;
    juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " Server URL set to: " + juce::String(url));
}

void ChatService::setPollingInterval(int milliseconds)
{
    pollingIntervalMs_ = juce::jlimit(1000, 30000, milliseconds);
    juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " Polling interval set to: " + juce::String(pollingIntervalMs_) + "ms");

    // Restart timer if already polling
    if (pollingActive_) {
        pollTimer_->stopTimer();
        pollTimer_->startTimer(pollingIntervalMs_);
    }
}

// ============================================================================
// Stats
// ============================================================================

int ChatService::getOnlineUserCount() const
{
    return onlineUserCount_.load();
}

std::string ChatService::getCurrentUsername() const
{
    std::string email = getCurrentUserEmail();
    // Extract username from email (before @)
    size_t atPos = email.find('@');
    if (atPos != std::string::npos) {
        return email.substr(0, atPos);
    }
    return email;
}

// ============================================================================
// Internal: Polling
// ============================================================================

void ChatService::pollMessages()
{
    if (!pollingActive_ || !isConnected()) {
        return;
    }

    juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " Polling messages (since ID: " + juce::String(lastMessageId_) + ")");

    // Build endpoint with after_id parameter (matches server API)
    std::string endpoint = "/api/chat/history";
    if (lastMessageId_ > 0) {
        endpoint += "?after_id=" + std::to_string(lastMessageId_);
    } else {
        endpoint += "?limit=50";  // Initial fetch
    }

    auto response = httpGet(endpoint);

    if (!response.success) {
        if (response.statusCode == 401) {
            updateConnectionStatus(ConnectionStatus::AuthRequired, "Session expired");
            stopPolling();
        } else if (response.statusCode == 0) {
            // Network error - don't disconnect, just log
            juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " Network error during poll");
        }
        return;
    }

    // Parse messages
    if (response.jsonBody.isObject()) {
        auto* obj = response.jsonBody.getDynamicObject();
        if (obj && obj->hasProperty("messages")) {
            auto messages = parseMessages(obj->getProperty("messages"));

            if (!messages.empty()) {
                // Update cache
                {
                    std::lock_guard<std::mutex> lock(messagesMutex_);
                    for (const auto& msg : messages) {
                        cachedMessages_.push_back(msg);
                        if (msg.id > lastMessageId_) {
                            lastMessageId_ = msg.id;
                        }
                    }

                    // Keep only last 200 messages in cache
                    while (cachedMessages_.size() > 200) {
                        cachedMessages_.erase(cachedMessages_.begin());
                    }
                }

                // Notify UI
                notifyNewMessages(messages);
            }
        }

        // Update presence if included
        if (obj && obj->hasProperty("online_count")) {
            int count = static_cast<int>(obj->getProperty("online_count"));
            onlineUserCount_ = count;

            std::vector<std::string> users;
            if (obj->hasProperty("online_users") && obj->getProperty("online_users").isArray()) {
                auto* arr = obj->getProperty("online_users").getArray();
                for (const auto& u : *arr) {
                    users.push_back(u.toString().toStdString());
                }
                onlineUsernames_ = users;
            }

            notifyPresenceUpdate(count, users);
        }
    }
}

void ChatService::pollPresence()
{
    if (!pollingActive_ || !isConnected()) {
        return;
    }

    auto response = httpGet("/api/chat/users/online");

    if (response.success && response.jsonBody.isObject()) {
        auto* obj = response.jsonBody.getDynamicObject();
        if (obj && obj->hasProperty("count")) {
            int count = static_cast<int>(obj->getProperty("count"));
            onlineUserCount_ = count;

            std::vector<std::string> users;
            if (obj->hasProperty("users") && obj->getProperty("users").isArray()) {
                auto* arr = obj->getProperty("users").getArray();
                for (const auto& u : *arr) {
                    users.push_back(u.toString().toStdString());
                }
            }
            onlineUsernames_ = users;

            notifyPresenceUpdate(count, users);
        }
    }
}

void ChatService::fetchHistory()
{
    juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " === fetchHistory START ===");

    auto response = httpGet("/api/chat/history?limit=50");

    if (!response.success) {
        juce::Logger::writeToLog(juce::String(LOG_PREFIX) + "   FAILED: " + juce::String(response.errorMessage));

        if (response.statusCode == 401) {
            updateConnectionStatus(ConnectionStatus::AuthRequired, "Please sign in");
        } else {
            updateConnectionStatus(ConnectionStatus::Error, response.errorMessage);
        }
        return;
    }

    // Parse and cache messages
    if (response.jsonBody.isObject()) {
        auto* obj = response.jsonBody.getDynamicObject();
        if (obj && obj->hasProperty("messages")) {
            auto messages = parseMessages(obj->getProperty("messages"));

            {
                std::lock_guard<std::mutex> lock(messagesMutex_);
                cachedMessages_ = messages;

                // Track last message ID
                for (const auto& msg : messages) {
                    if (msg.id > lastMessageId_) {
                        lastMessageId_ = msg.id;
                    }
                }
            }

            juce::Logger::writeToLog(juce::String(LOG_PREFIX) + "   Loaded " + juce::String(static_cast<int>(messages.size())) + " messages");
            notifyNewMessages(messages);
        }
    }

    juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " === fetchHistory END ===");
}

bool ChatService::doSendMessage(const std::string& content)
{
    juce::DynamicObject::Ptr body = new juce::DynamicObject();
    body->setProperty("content", juce::String(content));
    body->setProperty("room_id", 1);  // Global room

    auto response = httpPost("/api/chat/send", juce::var(body.get()));

    if (!response.success) {
        juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " Send failed: " + juce::String(response.errorMessage));
        return false;
    }

    return true;
}

// ============================================================================
// Internal: Authentication
// ============================================================================

std::string ChatService::getAuthToken() const
{
    // Get token from license service
    if (auto* registry = samplar::ServiceLocator::getRegistry()) {
        if (auto* licenseSvc = dynamic_cast<LicenseSystemService*>(registry->get("license"))) {
            return licenseSvc->getAccessToken();
        }
    }
    return "";
}

std::string ChatService::getCurrentUserEmail() const
{
    if (auto* registry = samplar::ServiceLocator::getRegistry()) {
        if (auto* licenseSvc = dynamic_cast<LicenseSystemService*>(registry->get("license"))) {
            return licenseSvc->getCurrentLicenseInfo().customerEmail;
        }
    }
    return "";
}

bool ChatService::isAuthenticated() const
{
    if (auto* registry = samplar::ServiceLocator::getRegistry()) {
        if (auto* licenseSvc = dynamic_cast<LicenseSystemService*>(registry->get("license"))) {
            // First check: Do we have a valid access token? (for chat, this is primary)
            // The token allows chat even if license cache is "stale" (>7 days)
            std::string token = licenseSvc->getAccessToken();
            if (!token.empty()) {
                juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " isAuthenticated: has access token");
                return true;  // Let server validate the token
            }

            // Fallback: Check license status
            auto status = licenseSvc->getCurrentStatus();
            bool isValid = (status == LicenseSystemService::LicenseStatus::Valid ||
                           status == LicenseSystemService::LicenseStatus::GracePeriodActive);
            juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " isAuthenticated: no token, license status=" +
                juce::String(static_cast<int>(status)) + ", valid=" + juce::String(isValid ? "Y" : "N"));
            return isValid;
        }
    }
    juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " isAuthenticated: no license service");
    return false;
}

// ============================================================================
// Internal: HTTP
// ============================================================================

ChatService::HttpResponse ChatService::httpGet(const std::string& endpoint)
{
    std::lock_guard<std::mutex> lock(networkMutex_);

    HttpResponse response;
    juce::String fullUrl = juce::String(serverUrl_) + juce::String(endpoint);

    juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " HTTP GET: " + fullUrl);

    try {
        juce::URL url(fullUrl);

        // Build headers with Authorization token
        juce::String extraHeaders = "Accept: application/json\r\n";

        std::string token = getAuthToken();
        if (!token.empty()) {
            extraHeaders += "Authorization: Bearer " + juce::String(token) + "\r\n";
            juce::Logger::writeToLog(juce::String(LOG_PREFIX) + "   Auth token present (length: " + juce::String(static_cast<int>(token.length())) + ")");
        } else {
            juce::Logger::writeToLog(juce::String(LOG_PREFIX) + "   WARNING: No auth token available");
        }

        // Perform request with headers
        juce::StringPairArray responseHeaders;
        int statusCode = 0;

        auto stream = url.createInputStream(
            juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                .withExtraHeaders(extraHeaders)
                .withConnectionTimeoutMs(timeoutSeconds_ * 1000)
                .withStatusCode(&statusCode)
                .withResponseHeaders(&responseHeaders)
        );

        response.statusCode = statusCode;
        juce::Logger::writeToLog(juce::String(LOG_PREFIX) + "   Response status: " + juce::String(statusCode));

        if (!stream) {
            response.success = false;
            response.statusCode = 0;
            response.errorMessage = "Network error or connection failed";
            juce::Logger::writeToLog(juce::String(LOG_PREFIX) + "   ERROR: Failed to connect");
            return response;
        }

        juce::String responseBody = stream->readEntireStreamAsString();

        if (statusCode >= 200 && statusCode < 300) {
            // Try to parse as JSON
            juce::var parsed = juce::JSON::parse(responseBody);
            if (parsed.isVoid()) {
                response.success = false;
                response.errorMessage = "Invalid JSON response";
                return response;
            }

            response.success = true;
            response.jsonBody = parsed;

            // Check for error in response body
            if (parsed.isObject()) {
                auto* obj = parsed.getDynamicObject();
                if (obj && obj->hasProperty("error")) {
                    response.success = false;
                    response.errorMessage = obj->getProperty("error").toString().toStdString();
                }
            }
        } else {
            response.success = false;
            auto parsed = juce::JSON::parse(responseBody);
            if (parsed.isObject()) {
                auto* obj = parsed.getDynamicObject();
                if (obj && obj->hasProperty("detail")) {
                    response.errorMessage = obj->getProperty("detail").toString().toStdString();
                } else if (obj && obj->hasProperty("error")) {
                    response.errorMessage = obj->getProperty("error").toString().toStdString();
                }
            }
            if (response.errorMessage.empty()) {
                response.errorMessage = "HTTP " + std::to_string(statusCode);
            }
            juce::Logger::writeToLog(juce::String(LOG_PREFIX) + "   ERROR: " + juce::String(response.errorMessage));
        }

    } catch (const std::exception& e) {
        response.success = false;
        response.statusCode = 0;
        response.errorMessage = e.what();
        juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " HTTP error: " + juce::String(e.what()));
    }

    return response;
}

ChatService::HttpResponse ChatService::httpPost(const std::string& endpoint, const juce::var& body)
{
    std::lock_guard<std::mutex> lock(networkMutex_);

    HttpResponse response;
    juce::String fullUrl = juce::String(serverUrl_) + juce::String(endpoint);

    juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " HTTP POST: " + fullUrl);

    try {
        juce::URL url(fullUrl);

        // Convert body to JSON string
        juce::String jsonBody = juce::JSON::toString(body);

        // Create POST request with JSON body
        url = url.withPOSTData(jsonBody);

        // Build headers with Authorization token
        juce::String extraHeaders = "Content-Type: application/json\r\nAccept: application/json\r\n";

        std::string token = getAuthToken();
        if (!token.empty()) {
            extraHeaders += "Authorization: Bearer " + juce::String(token) + "\r\n";
            juce::Logger::writeToLog(juce::String(LOG_PREFIX) + "   Auth token present");
        } else {
            juce::Logger::writeToLog(juce::String(LOG_PREFIX) + "   WARNING: No auth token for POST");
        }

        // Perform request
        juce::StringPairArray responseHeaders;
        int statusCode = 0;

        auto stream = url.createInputStream(
            juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inPostData)
                .withExtraHeaders(extraHeaders)
                .withConnectionTimeoutMs(timeoutSeconds_ * 1000)
                .withStatusCode(&statusCode)
                .withResponseHeaders(&responseHeaders)
        );

        if (!stream) {
            response.success = false;
            response.statusCode = 0;
            response.errorMessage = "Failed to connect";
            return response;
        }

        juce::String responseBody = stream->readEntireStreamAsString();

        response.statusCode = statusCode;

        if (statusCode >= 200 && statusCode < 300) {
            response.success = true;
            response.jsonBody = juce::JSON::parse(responseBody);
        } else {
            response.success = false;
            auto parsed = juce::JSON::parse(responseBody);
            if (parsed.isObject()) {
                auto* obj = parsed.getDynamicObject();
                if (obj && obj->hasProperty("detail")) {
                    response.errorMessage = obj->getProperty("detail").toString().toStdString();
                } else if (obj && obj->hasProperty("error")) {
                    response.errorMessage = obj->getProperty("error").toString().toStdString();
                }
            }
            if (response.errorMessage.empty()) {
                response.errorMessage = "HTTP " + std::to_string(statusCode);
            }
        }

    } catch (const std::exception& e) {
        response.success = false;
        response.statusCode = 0;
        response.errorMessage = e.what();
        juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " HTTP error: " + juce::String(e.what()));
    }

    return response;
}

// ============================================================================
// Internal: Rate Limiting
// ============================================================================

bool ChatService::checkRateLimit()
{
    std::lock_guard<std::mutex> lock(rateLimitMutex_);

    auto now = std::chrono::steady_clock::now();

    // Remove old entries outside the window
    auto cutoff = now - std::chrono::milliseconds(RATE_LIMIT_WINDOW_MS);
    messageTimes_.erase(
        std::remove_if(messageTimes_.begin(), messageTimes_.end(),
            [cutoff](const auto& t) { return t < cutoff; }),
        messageTimes_.end()
    );

    // Check if under limit
    return static_cast<int>(messageTimes_.size()) < RATE_LIMIT_MAX_MESSAGES;
}

void ChatService::recordMessageSent()
{
    std::lock_guard<std::mutex> lock(rateLimitMutex_);
    messageTimes_.push_back(std::chrono::steady_clock::now());
}

// ============================================================================
// Internal: Message Parsing
// ============================================================================

ChatService::ChatMessage ChatService::parseMessage(const juce::var& msgVar) const
{
    ChatMessage msg;

    if (!msgVar.isObject()) {
        return msg;
    }

    auto* obj = msgVar.getDynamicObject();
    if (!obj) {
        return msg;
    }

    // ID
    if (obj->hasProperty("id")) {
        msg.id = static_cast<int64_t>(obj->getProperty("id"));
    }

    // Username - use user_display_name from server (COALESCE of name and email)
    if (obj->hasProperty("user_display_name")) {
        msg.username = obj->getProperty("user_display_name").toString().toStdString();
    } else if (obj->hasProperty("username")) {
        msg.username = obj->getProperty("username").toString().toStdString();
    } else if (obj->hasProperty("user_email")) {
        // Fallback: Extract username from email
        std::string email = obj->getProperty("user_email").toString().toStdString();
        size_t atPos = email.find('@');
        msg.username = (atPos != std::string::npos) ? email.substr(0, atPos) : email;
    }

    // Content
    if (obj->hasProperty("content")) {
        msg.content = obj->getProperty("content").toString().toStdString();
    } else if (obj->hasProperty("message_text")) {
        msg.content = obj->getProperty("message_text").toString().toStdString();
    }

    // Type and moderator detection
    std::string messageType = "text";
    if (obj->hasProperty("message_type")) {
        messageType = obj->getProperty("message_type").toString().toLowerCase().toStdString();
        if (messageType == "system") {
            msg.type = MessageType::System;
        } else if (messageType == "join") {
            msg.type = MessageType::Join;
        } else if (messageType == "leave") {
            msg.type = MessageType::Leave;
        } else if (messageType == "moderator") {
            msg.type = MessageType::Moderator;
            msg.isModerator = true;
        } else {
            msg.type = MessageType::Text;
        }
    }

    // Check for moderator by email (dev@samplemonsta.com)
    std::string userEmail;
    if (obj->hasProperty("user_email")) {
        userEmail = obj->getProperty("user_email").toString().toLowerCase().toStdString();
        if (userEmail == "dev@samplemonsta.com") {
            msg.isModerator = true;
        }
    }

    // Timestamp
    if (obj->hasProperty("created_at")) {
        juce::String dateStr = obj->getProperty("created_at").toString();
        // Parse ISO timestamp
        // For simplicity, we'll use current time if parsing fails
        msg.timestamp = std::chrono::system_clock::now();
    }

    // Check if own message
    std::string currentEmail = getCurrentUserEmail();
    if (!userEmail.empty()) {
        msg.isOwnMessage = (userEmail == juce::String(currentEmail).toLowerCase().toStdString());
    }

    return msg;
}

std::vector<ChatService::ChatMessage> ChatService::parseMessages(const juce::var& arrayVar) const
{
    std::vector<ChatMessage> messages;

    if (!arrayVar.isArray()) {
        return messages;
    }

    auto* arr = arrayVar.getArray();
    if (!arr) {
        return messages;
    }

    for (const auto& msgVar : *arr) {
        auto msg = parseMessage(msgVar);
        if (msg.id > 0) {
            messages.push_back(msg);
        }
    }

    return messages;
}

// ============================================================================
// Internal: State Updates
// ============================================================================

void ChatService::updateConnectionStatus(ConnectionStatus status, const std::string& message)
{
    ConnectionStatus oldStatus = connectionStatus_.exchange(status);

    if (oldStatus != status) {
        juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " Status changed to: " +
            juce::String(static_cast<int>(status)) + " - " + juce::String(message));

        // Notify callback
        std::lock_guard<std::mutex> lock(callbacksMutex_);
        if (statusCallback_) {
            juce::MessageManager::callAsync([this, status, message] {
                std::lock_guard<std::mutex> lock(callbacksMutex_);
                if (statusCallback_) {
                    statusCallback_(status, message);
                }
            });
        }

        // Publish event
        juce::DynamicObject::Ptr payload = new juce::DynamicObject();
        payload->setProperty("status", static_cast<int>(status));
        payload->setProperty("message", juce::String(message));
        payload->setProperty("connected", status == ConnectionStatus::Connected);
        eventBus_.publish("chat.status", juce::var(payload.get()));
    }
}

void ChatService::notifyNewMessages(const std::vector<ChatMessage>& messages)
{
    std::lock_guard<std::mutex> lock(callbacksMutex_);
    if (messageCallback_) {
        juce::MessageManager::callAsync([this, messages] {
            std::lock_guard<std::mutex> lock(callbacksMutex_);
            if (messageCallback_) {
                messageCallback_(messages);
            }
        });
    }

    // Also publish event
    eventBus_.publish("chat.messages", juce::var(static_cast<int>(messages.size())));
}

void ChatService::notifyPresenceUpdate(int count, const std::vector<std::string>& users)
{
    std::lock_guard<std::mutex> lock(callbacksMutex_);
    if (presenceCallback_) {
        juce::MessageManager::callAsync([this, count, users] {
            std::lock_guard<std::mutex> lock(callbacksMutex_);
            if (presenceCallback_) {
                presenceCallback_(count, users);
            }
        });
    }

    // Also publish event
    eventBus_.publish("chat.presence", juce::var(count));
}

// ============================================================================
// Nickname Management
// ============================================================================

std::string ChatService::getNickname() const
{
    return cachedNickname_;
}

void ChatService::setNicknameCallback(NicknameCallback callback)
{
    std::lock_guard<std::mutex> lock(callbacksMutex_);
    nicknameCallback_ = std::move(callback);
}

void ChatService::fetchNickname()
{
    juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " === fetchNickname START ===");

    auto response = httpGet("/api/chat/nickname");

    if (response.success && response.jsonBody.isObject()) {
        auto* obj = response.jsonBody.getDynamicObject();
        if (obj && obj->hasProperty("nickname")) {
            auto nicknameVar = obj->getProperty("nickname");
            if (!nicknameVar.isVoid() && !nicknameVar.isUndefined()) {
                cachedNickname_ = nicknameVar.toString().toStdString();
                juce::Logger::writeToLog(juce::String(LOG_PREFIX) + "   Nickname: " + juce::String(cachedNickname_));

                // Notify callback
                std::lock_guard<std::mutex> lock(callbacksMutex_);
                if (nicknameCallback_) {
                    juce::MessageManager::callAsync([this] {
                        std::lock_guard<std::mutex> lock(callbacksMutex_);
                        if (nicknameCallback_) {
                            nicknameCallback_(cachedNickname_);
                        }
                    });
                }
            } else {
                juce::Logger::writeToLog(juce::String(LOG_PREFIX) + "   No nickname set");
                cachedNickname_.clear();
            }
        }
    } else {
        juce::Logger::writeToLog(juce::String(LOG_PREFIX) + "   ERROR: Failed to fetch nickname");
    }

    juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " === fetchNickname END ===");
}

bool ChatService::setNickname(const std::string& nickname, std::string& errorOut)
{
    juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " === setNickname START ===");
    juce::Logger::writeToLog(juce::String(LOG_PREFIX) + "   Nickname: " + juce::String(nickname));

    // Validate locally first
    if (nickname.length() < 2) {
        errorOut = "Nickname too short (min 2 characters)";
        return false;
    }
    if (nickname.length() > 30) {
        errorOut = "Nickname too long (max 30 characters)";
        return false;
    }

    // Check format (alphanumeric, spaces, underscores)
    for (char c : nickname) {
        if (!std::isalnum(c) && c != ' ' && c != '_') {
            errorOut = "Nickname can only contain letters, numbers, spaces, and underscores";
            return false;
        }
    }

    // Build request body
    juce::DynamicObject::Ptr body = new juce::DynamicObject();
    body->setProperty("nickname", juce::String(nickname));

    auto response = httpPost("/api/chat/nickname", juce::var(body.get()));

    if (response.success && response.jsonBody.isObject()) {
        auto* obj = response.jsonBody.getDynamicObject();
        if (obj && obj->hasProperty("success") && obj->getProperty("success")) {
            cachedNickname_ = nickname;
            juce::Logger::writeToLog(juce::String(LOG_PREFIX) + "   Nickname set successfully");

            // Notify callback
            std::lock_guard<std::mutex> lock(callbacksMutex_);
            if (nicknameCallback_) {
                juce::MessageManager::callAsync([this] {
                    std::lock_guard<std::mutex> lock(callbacksMutex_);
                    if (nicknameCallback_) {
                        nicknameCallback_(cachedNickname_);
                    }
                });
            }

            juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " === setNickname END === SUCCESS");
            return true;
        } else if (obj && obj->hasProperty("error")) {
            errorOut = obj->getProperty("error").toString().toStdString();
        }
    } else {
        errorOut = response.errorMessage.empty() ? "Failed to set nickname" : response.errorMessage;
    }

    juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " === setNickname END === FAILED: " + juce::String(errorOut));
    return false;
}

} // namespace samplar
