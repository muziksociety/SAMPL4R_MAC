#pragma once

#include "../app/Service.h"
#include "../app/EventBus.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace samplar {

// Forward declaration for friend class
class ChatPollTimer;

/**
 * ChatService - Community chat client for SAMPLAR
 *
 * Provides real-time messaging via HTTP polling to the license server.
 * Uses the same JWT authentication as the license system.
 *
 * MVP Features (Phase 1):
 * - Single global chat room
 * - HTTP polling every 3 seconds
 * - Message send/receive
 * - User presence (online count)
 * - Rate limiting (10 messages / 10 seconds)
 *
 * Phase 2 (Future):
 * - WebSocket for real-time
 * - Multiple chat rooms
 * - User mentions (@username)
 */
class ChatService : public IService {
public:
    // Message types
    enum class MessageType {
        Text,       // Normal user message
        System,     // System notification
        Join,       // User joined
        Leave,      // User left
        Moderator   // Moderator/dev message
    };

    // Chat message structure
    struct ChatMessage {
        int64_t id{0};                              // Server message ID
        std::string username;                        // Display name
        std::string content;                         // Message text
        MessageType type{MessageType::Text};
        std::chrono::system_clock::time_point timestamp;
        bool isOwnMessage{false};                    // True if sent by current user
        bool isModerator{false};                     // True if from dev/moderator
    };

    // Connection status
    enum class ConnectionStatus {
        Disconnected,
        Connecting,
        Connected,
        AuthRequired,   // User needs to sign in
        Error
    };

    // Callbacks for UI updates
    using MessageCallback = std::function<void(const std::vector<ChatMessage>&)>;
    using StatusCallback = std::function<void(ConnectionStatus, const std::string& message)>;
    using PresenceCallback = std::function<void(int onlineCount, const std::vector<std::string>& usernames)>;

    explicit ChatService(EventBus& eventBus);
    ~ChatService() override;

    void initialise() override;
    void shutdown() override;

    // Connection management
    void connect();
    void disconnect();
    bool isConnected() const;
    ConnectionStatus getConnectionStatus() const;

    // Messaging
    bool sendMessage(const std::string& content);
    std::vector<ChatMessage> getRecentMessages(int limit = 50) const;

    // Polling control
    void startPolling();
    void stopPolling();
    bool isPolling() const;

    // Callbacks for UI
    void setMessageCallback(MessageCallback callback);
    void setStatusCallback(StatusCallback callback);
    void setPresenceCallback(PresenceCallback callback);

    // Configuration
    void setServerUrl(const std::string& url);
    void setPollingInterval(int milliseconds);

    // Stats
    int getOnlineUserCount() const;
    std::string getCurrentUsername() const;

    // Nickname management
    std::string getNickname() const;  // Returns cached nickname
    void fetchNickname();             // Fetch from server
    bool setNickname(const std::string& nickname, std::string& errorOut);

    // Nickname callback
    using NicknameCallback = std::function<void(const std::string& nickname)>;
    void setNicknameCallback(NicknameCallback callback);

    // Content validation (public for UI pre-validation)
    bool validateContent(const std::string& content, std::string& errorOut) const;
    bool isDuplicate(const std::string& content);

private:
    // Allow ChatPollTimer to access private members
    friend class ChatPollTimer;

    // Internal helpers
    void pollMessages();
    void pollPresence();
    void fetchHistory();
    bool doSendMessage(const std::string& content);

    // Authentication
    std::string getAuthToken() const;
    std::string getCurrentUserEmail() const;
    bool isAuthenticated() const;

    // HTTP requests
    struct HttpResponse {
        bool success{false};
        int statusCode{0};
        juce::var jsonBody;
        std::string errorMessage;
    };

    HttpResponse httpGet(const std::string& endpoint);
    HttpResponse httpPost(const std::string& endpoint, const juce::var& body);

    // Rate limiting
    bool checkRateLimit();
    void recordMessageSent();

    // Message parsing
    ChatMessage parseMessage(const juce::var& msgVar) const;
    std::vector<ChatMessage> parseMessages(const juce::var& arrayVar) const;

    // State updates
    void updateConnectionStatus(ConnectionStatus status, const std::string& message = "");
    void notifyNewMessages(const std::vector<ChatMessage>& messages);
    void notifyPresenceUpdate(int count, const std::vector<std::string>& users);

    // Event bus
    EventBus& eventBus_;
    std::vector<std::pair<std::string, EventBus::HandlerId>> subscriptions_;

    // Connection state
    std::atomic<ConnectionStatus> connectionStatus_{ConnectionStatus::Disconnected};
    std::string serverUrl_{"https://license-server-n5it.onrender.com"};

    // Polling
    std::unique_ptr<juce::Timer> pollTimer_;
    std::atomic<bool> pollingActive_{false};
    int pollingIntervalMs_{3000};  // 3 seconds

    // Message cache
    mutable std::mutex messagesMutex_;
    std::vector<ChatMessage> cachedMessages_;
    int64_t lastMessageId_{0};  // For incremental fetching

    // Presence
    std::atomic<int> onlineUserCount_{0};
    std::vector<std::string> onlineUsernames_;

    // Rate limiting: max 10 messages per 10 seconds
    static constexpr int RATE_LIMIT_MAX_MESSAGES = 10;
    static constexpr int RATE_LIMIT_WINDOW_MS = 10000;
    std::vector<std::chrono::steady_clock::time_point> messageTimes_;
    mutable std::mutex rateLimitMutex_;

    // Deduplication: block same message within 30 seconds
    static constexpr int DEDUPE_WINDOW_MS = 30000;
    std::vector<std::pair<std::string, std::chrono::steady_clock::time_point>> recentMessages_;
    mutable std::mutex dedupeMutex_;

    // Content validation helpers (private)
    bool containsUrl(const std::string& content) const;
    bool containsEmail(const std::string& content) const;
    void recordMessageForDedupe(const std::string& content);

    // Callbacks
    MessageCallback messageCallback_;
    StatusCallback statusCallback_;
    PresenceCallback presenceCallback_;
    NicknameCallback nicknameCallback_;
    mutable std::mutex callbacksMutex_;

    // Cached nickname
    std::string cachedNickname_;

    // Network
    mutable std::mutex networkMutex_;
    int timeoutSeconds_{8};

    // Logging prefix
    static constexpr const char* LOG_PREFIX = "[ChatService]";
};

} // namespace samplar
