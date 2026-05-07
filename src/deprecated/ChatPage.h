#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../../services/ChatService.h"
#include <functional>
#include <memory>
#include <vector>

namespace samplar {

/**
 * ChatPage - Native JUCE chat interface for SAMPLAR
 *
 * Displays a real-time community chat with:
 * - Message list with scrolling
 * - Text input field
 * - User presence indicator
 * - Connection status
 *
 * Design follows SAMPLAR dark theme with cyan accents.
 */
class ChatPage : public juce::Component, private juce::Timer {
public:
    ChatPage();
    ~ChatPage() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    // Navigation callback (set by shell)
    std::function<void()> onBack;

    // Callbacks for navigation from parent
    std::function<void()> onGoChat;

    // Initialize chat connection when page becomes visible
    void connectToChat();

    // Disconnect when page is hidden
    void disconnectFromChat();

private:
    // Timer callback for UI updates
    void timerCallback() override;

    // Message bubble component
    class MessageBubble : public juce::Component {
    public:
        MessageBubble(const ChatService::ChatMessage& msg, bool isOwn);
        void paint(juce::Graphics& g) override;

        int getRequiredHeight() const { return requiredHeight_; }
        void setWidth(int w);

    private:
        ChatService::ChatMessage message_;
        bool isOwnMessage_;
        int requiredHeight_{40};
        juce::Font nameFont_{13.0f, juce::Font::bold};
        juce::Font messageFont_{14.0f};
        juce::Font timeFont_{11.0f};
    };

    // Message list container (inside viewport)
    class MessageListComponent : public juce::Component {
    public:
        MessageListComponent();

        void addMessages(const std::vector<ChatService::ChatMessage>& messages);
        void clearMessages();
        void updateLayout(int width);

        int getContentHeight() const { return contentHeight_; }

    private:
        juce::OwnedArray<MessageBubble> bubbles_;
        int contentHeight_{0};
        int lastWidth_{0};
    };

    // UI Components - Header
    juce::TextButton backButton_{"HOME"};
    juce::Label titleLabel_{{}, "Community Chat"};
    juce::Label nicknameLabel_{{}, ""};  // Shows current nickname
    juce::Label usersCountLabel_{{}, "Users: 0"};
    juce::TextButton settingsButton_{"Nick"};

    // Connection status
    juce::Label connectionStatusLabel_{{}, "Disconnected"};
    juce::TextButton reconnectButton_{"Reconnect"};
    juce::TextButton signInButton_{"Sign In"};

    // Message display
    juce::Viewport messageViewport_;
    std::unique_ptr<MessageListComponent> messageList_;

    // Input area
    juce::TextEditor inputField_;
    juce::TextButton emojiButton_{juce::String::fromUTF8("\xF0\x9F\x98\x80")};  // 😀
    juce::TextButton sendButton_{"Send"};

    // Emoji picker
    class EmojiPicker;
    std::unique_ptr<EmojiPicker> emojiPicker_;
    void showEmojiPicker();
    void insertEmoji(const juce::String& emoji);

    // Error feedback label (shows above input)
    juce::Label errorLabel_{{}, ""};
    bool errorVisible_{false};

    // Service reference
    ChatService* chatService_{nullptr};

    // State
    bool connected_{false};
    ChatService::ConnectionStatus lastStatus_{ChatService::ConnectionStatus::Disconnected};
    std::vector<ChatService::ChatMessage> displayedMessages_;

    // UI helpers
    void onSendClicked();
    void onReconnectClicked();
    void onSignInClicked();
    void onSettingsClicked();
    void showNicknameDialog();
    void updateConnectionUI();
    void scrollToBottom();
    void showError(const juce::String& message);
    void hideError();

    // Callbacks from ChatService
    void onMessagesReceived(const std::vector<ChatService::ChatMessage>& messages);
    void onStatusChanged(ChatService::ConnectionStatus status, const std::string& message);
    void onPresenceUpdated(int count, const std::vector<std::string>& users);

    // Logging
    static constexpr const char* LOG_PREFIX = "[ChatPage]";

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChatPage)
};

} // namespace samplar
