#include "ChatPage.h"
#include "../../app/App.h"
#include "../../app/ServiceRegistry.h"
#include "../../app/ServiceLocator.h"
#include "../../services/LicenseSystemService.h"

namespace samplar {

// ============================================================================
// MessageBubble Implementation
// ============================================================================

ChatPage::MessageBubble::MessageBubble(const ChatService::ChatMessage& msg, bool isOwn)
    : message_(msg), isOwnMessage_(isOwn)
{
    // Calculate initial height
    requiredHeight_ = 50;  // Will be updated in setWidth
}

void ChatPage::MessageBubble::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().reduced(4, 2);

    // All messages use full width (no left/right distinction)
    auto bubbleBounds = bounds;

    // Background color based on message type
    juce::Colour bgColor;
    juce::Colour borderColor;

    if (message_.isModerator) {
        // Moderator/DEV messages: purple/violet tint
        bgColor = juce::Colour(0xFF2A1A3A);      // Purple tint
        borderColor = juce::Colour(0xFFAA55FF);  // Bright purple border
    } else if (isOwnMessage_) {
        bgColor = juce::Colour(0xFF1A3A5A);      // Blue tint for own messages
        borderColor = juce::Colour(0xFF00FFFF).withAlpha(0.3f);
    } else {
        bgColor = juce::Colour(0xFF2A2A2A);      // Dark gray for others
        borderColor = juce::Colour(0xFF00FFFF).withAlpha(0.2f);
    }

    // System messages have different styling
    if (message_.type == ChatService::MessageType::System ||
        message_.type == ChatService::MessageType::Join ||
        message_.type == ChatService::MessageType::Leave) {
        bgColor = juce::Colour(0xFF1A1A1A);
        borderColor = juce::Colour(0xFF444444);
    }

    // Draw bubble background
    g.setColour(bgColor);
    g.fillRoundedRectangle(bubbleBounds.toFloat(), 8.0f);

    // Draw border (thicker for moderator)
    g.setColour(borderColor);
    float borderWidth = message_.isModerator ? 2.0f : 1.0f;
    g.drawRoundedRectangle(bubbleBounds.toFloat(), 8.0f, borderWidth);

    // Content area
    auto contentBounds = bubbleBounds.reduced(10, 6);

    // Username row with optional DEV badge
    if (message_.type == ChatService::MessageType::Text ||
        message_.type == ChatService::MessageType::Moderator) {
        auto nameBounds = contentBounds.removeFromTop(16);

        // DEV badge for moderators
        if (message_.isModerator) {
            // Draw [DEV] badge
            g.setFont(juce::Font(10.0f, juce::Font::bold));
            juce::String badge = "[DEV]";
            int badgeWidth = g.getCurrentFont().getStringWidth(badge) + 8;

            auto badgeBounds = nameBounds.removeFromLeft(badgeWidth);
            g.setColour(juce::Colour(0xFFAA55FF));  // Purple
            g.fillRoundedRectangle(badgeBounds.reduced(1).toFloat(), 3.0f);
            g.setColour(juce::Colours::white);
            g.drawText(badge, badgeBounds, juce::Justification::centred, false);
            nameBounds.removeFromLeft(4);
        }

        // Username
        g.setFont(nameFont_);
        if (message_.isModerator) {
            g.setColour(juce::Colour(0xFFAA55FF));  // Purple for moderator name
        } else if (isOwnMessage_) {
            g.setColour(juce::Colour(0xFF88CCFF));  // Light blue for own name
        } else {
            g.setColour(juce::Colour(0xFF00FFFF));  // Cyan for other names
        }
        g.drawText(juce::String(message_.username), nameBounds, juce::Justification::topLeft, true);
        contentBounds.removeFromTop(2);
    }

    // Message content
    g.setFont(messageFont_);

    if (message_.type == ChatService::MessageType::System) {
        g.setColour(juce::Colour(0xFF888888));  // Gray for system
        g.drawFittedText("-- " + juce::String(message_.content) + " --",
                         contentBounds, juce::Justification::centred, 3);
    } else if (message_.type == ChatService::MessageType::Join) {
        g.setColour(juce::Colour(0xFF00FF00));  // Green for join
        g.drawFittedText(juce::String(message_.username) + " joined the chat",
                         contentBounds, juce::Justification::centredLeft, 2);
    } else if (message_.type == ChatService::MessageType::Leave) {
        g.setColour(juce::Colour(0xFFFF8800));  // Orange for leave
        g.drawFittedText(juce::String(message_.username) + " left the chat",
                         contentBounds, juce::Justification::centredLeft, 2);
    } else {
        g.setColour(juce::Colours::white);
        g.drawFittedText(juce::String(message_.content),
                         contentBounds, juce::Justification::topLeft, 10);
    }

    // Timestamp (bottom right of bubble)
    g.setFont(timeFont_);
    g.setColour(juce::Colour(0xFF666666));

    // Format time (for now just show HH:MM)
    auto time = std::chrono::system_clock::to_time_t(message_.timestamp);
    std::tm tm = *std::localtime(&time);
    char timeStr[8];
    std::strftime(timeStr, sizeof(timeStr), "%H:%M", &tm);

    auto timeBounds = bubbleBounds.reduced(10, 4);
    g.drawText(juce::String(timeStr),
               timeBounds.removeFromBottom(12),
               juce::Justification::bottomRight, true);
}

void ChatPage::MessageBubble::setWidth(int w)
{
    // Calculate required height based on message content
    int maxBubbleWidth = static_cast<int>(w * 0.75f) - 20;

    juce::Font font(14.0f);
    auto textBounds = juce::Rectangle<int>(0, 0, maxBubbleWidth - 20, 1000);

    // Measure text height
    juce::GlyphArrangement glyphs;
    glyphs.addFittedText(font, juce::String(message_.content),
                         0, 0, static_cast<float>(textBounds.getWidth()), 1000.0f,
                         juce::Justification::topLeft, 10);

    float textHeight = 0;
    for (int i = 0; i < glyphs.getNumGlyphs(); ++i) {
        textHeight = juce::jmax(textHeight, glyphs.getGlyph(i).getBounds().getBottom());
    }

    requiredHeight_ = static_cast<int>(textHeight) + 35;  // Add padding

    // Add extra space for username if not own message
    if (!isOwnMessage_ && message_.type == ChatService::MessageType::Text) {
        requiredHeight_ += 18;
    }

    requiredHeight_ = juce::jmax(requiredHeight_, 40);  // Minimum height

    setSize(w, requiredHeight_);
}

// ============================================================================
// MessageListComponent Implementation
// ============================================================================

ChatPage::MessageListComponent::MessageListComponent()
{
}

void ChatPage::MessageListComponent::addMessages(const std::vector<ChatService::ChatMessage>& messages)
{
    for (const auto& msg : messages) {
        // Check if message already exists (by ID)
        bool exists = false;
        for (int i = 0; i < bubbles_.size(); ++i) {
            // Can't easily check ID in bubble, so we just add all for now
            // In production, track IDs separately
        }

        if (!exists) {
            auto* bubble = new MessageBubble(msg, msg.isOwnMessage);
            bubbles_.add(bubble);
            addAndMakeVisible(bubble);
        }
    }

    updateLayout(getWidth());
}

void ChatPage::MessageListComponent::clearMessages()
{
    bubbles_.clear();
    contentHeight_ = 0;
    setSize(getWidth(), 10);
}

void ChatPage::MessageListComponent::updateLayout(int width)
{
    if (width <= 0) return;
    lastWidth_ = width;

    int y = 4;
    for (auto* bubble : bubbles_) {
        bubble->setWidth(width);
        bubble->setTopLeftPosition(0, y);
        y += bubble->getRequiredHeight() + 4;
    }

    contentHeight_ = y;
    setSize(width, juce::jmax(10, contentHeight_));
}

// ============================================================================
// ChatPage Implementation
// ============================================================================

ChatPage::ChatPage()
{
    juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " === CONSTRUCTOR START ===");

    // Title styling
    titleLabel_.setFont(juce::Font(18.0f, juce::Font::bold));
    titleLabel_.setColour(juce::Label::textColourId, juce::Colour(0xFFFFD700));  // Gold
    titleLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(titleLabel_);

    // Back button
    backButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF2A2A2A));
    backButton_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    backButton_.onClick = [this] {
        juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " Back button clicked");
        disconnectFromChat();
        if (onBack) onBack();
    };
    addAndMakeVisible(backButton_);

    // Users count label
    usersCountLabel_.setFont(juce::Font(12.0f));
    usersCountLabel_.setColour(juce::Label::textColourId, juce::Colour(0xFF00FFFF));
    usersCountLabel_.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(usersCountLabel_);

    // Nickname label (shows current user's nickname in header)
    nicknameLabel_.setFont(juce::Font(12.0f, juce::Font::italic));
    nicknameLabel_.setColour(juce::Label::textColourId, juce::Colour(0xFF88FFFF));  // Light cyan
    nicknameLabel_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(nicknameLabel_);

    // Settings button
    settingsButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF2A2A2A));
    settingsButton_.setColour(juce::TextButton::textColourOffId, juce::Colour(0xFF00FFFF));
    settingsButton_.onClick = [this] { onSettingsClicked(); };
    addAndMakeVisible(settingsButton_);

    // Connection status
    connectionStatusLabel_.setFont(juce::Font(12.0f));
    connectionStatusLabel_.setColour(juce::Label::textColourId, juce::Colour(0xFFFF4444));
    connectionStatusLabel_.setJustificationType(juce::Justification::centred);
    addChildComponent(connectionStatusLabel_);

    // Reconnect button
    reconnectButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF2A2A2A));
    reconnectButton_.setColour(juce::TextButton::textColourOffId, juce::Colour(0xFF00FFFF));
    reconnectButton_.onClick = [this] { onReconnectClicked(); };
    addChildComponent(reconnectButton_);

    // Sign in button
    signInButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF00AA00));
    signInButton_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    signInButton_.onClick = [this] { onSignInClicked(); };
    addChildComponent(signInButton_);

    // Message viewport and list
    messageList_ = std::make_unique<MessageListComponent>();
    messageViewport_.setViewedComponent(messageList_.get(), false);
    messageViewport_.setScrollBarsShown(true, false);
    addAndMakeVisible(messageViewport_);

    // Input field
    inputField_.setMultiLine(false);
    inputField_.setReturnKeyStartsNewLine(false);
    inputField_.setTextToShowWhenEmpty("Type your message...", juce::Colour(0xFF666666));
    inputField_.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xFF1A1A1A));
    inputField_.setColour(juce::TextEditor::textColourId, juce::Colours::white);
    inputField_.setColour(juce::TextEditor::outlineColourId, juce::Colour(0xFF00FFFF).withAlpha(0.3f));
    inputField_.setColour(juce::TextEditor::focusedOutlineColourId, juce::Colour(0xFF00FFFF));
    inputField_.onReturnKey = [this] { onSendClicked(); };
    addAndMakeVisible(inputField_);

    // Emoji button
    emojiButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF2A2A2A));
    emojiButton_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    emojiButton_.onClick = [this] { showEmojiPicker(); };
    addAndMakeVisible(emojiButton_);

    // Send button
    sendButton_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF00AA00));
    sendButton_.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
    sendButton_.onClick = [this] { onSendClicked(); };
    addAndMakeVisible(sendButton_);

    // Error feedback label (above input)
    errorLabel_.setFont(juce::Font(12.0f));
    errorLabel_.setColour(juce::Label::textColourId, juce::Colour(0xFFFF6666));  // Red/pink
    errorLabel_.setColour(juce::Label::backgroundColourId, juce::Colour(0xFF2A1515));  // Dark red bg
    errorLabel_.setJustificationType(juce::Justification::centredLeft);
    addChildComponent(errorLabel_);  // Hidden initially

    // Get ChatService reference
    if (auto* registry = samplar::ServiceLocator::getRegistry()) {
        chatService_ = dynamic_cast<ChatService*>(registry->get("chat"));

        if (chatService_) {
            juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " ChatService found");

            // Set callbacks
            chatService_->setMessageCallback([this](const std::vector<ChatService::ChatMessage>& msgs) {
                onMessagesReceived(msgs);
            });

            chatService_->setStatusCallback([this](ChatService::ConnectionStatus status, const std::string& msg) {
                onStatusChanged(status, msg);
            });

            chatService_->setPresenceCallback([this](int count, const std::vector<std::string>& users) {
                onPresenceUpdated(count, users);
            });

            // Nickname callback - updates header label when nickname changes
            chatService_->setNicknameCallback([this](const std::string& nickname) {
                juce::MessageManager::callAsync([this, nickname] {
                    if (nickname.empty()) {
                        nicknameLabel_.setText("", juce::dontSendNotification);
                    } else {
                        nicknameLabel_.setText("as: " + juce::String(nickname), juce::dontSendNotification);
                    }
                });
            });
        } else {
            juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " WARNING: ChatService not found!");
        }
    }

    // Start update timer
    startTimer(100);  // 100ms UI update timer

    juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " === CONSTRUCTOR END ===");
}

ChatPage::~ChatPage()
{
    juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " === DESTRUCTOR ===");
    stopTimer();
    disconnectFromChat();
}

void ChatPage::paint(juce::Graphics& g)
{
    // Dark background
    g.fillAll(juce::Colour::fromString("ff0D1117"));

    // Header background
    auto headerArea = getLocalBounds().removeFromTop(40);
    g.setColour(juce::Colour(0xFF1A1A1A));
    g.fillRect(headerArea);

    // Header border
    g.setColour(juce::Colour(0xFF00FFFF).withAlpha(0.3f));
    g.drawLine(0.0f, 40.0f, static_cast<float>(getWidth()), 40.0f, 1.0f);

    // Input area background
    auto inputArea = getLocalBounds().removeFromBottom(50);
    g.setColour(juce::Colour(0xFF1A1A1A));
    g.fillRect(inputArea);

    // Input area border
    g.setColour(juce::Colour(0xFF00FFFF).withAlpha(0.3f));
    float inputY = static_cast<float>(getHeight() - 50);
    g.drawLine(0.0f, inputY, static_cast<float>(getWidth()), inputY, 1.0f);
}

void ChatPage::resized()
{
    auto r = getLocalBounds();

    // Header: Back | Title | Nickname | Users | Settings
    auto header = r.removeFromTop(40).reduced(8, 4);
    backButton_.setBounds(header.removeFromLeft(60));
    header.removeFromLeft(8);

    settingsButton_.setBounds(header.removeFromRight(40));
    header.removeFromRight(4);
    usersCountLabel_.setBounds(header.removeFromRight(80));
    header.removeFromRight(8);

    // Title gets fixed width, nickname takes remaining space
    titleLabel_.setBounds(header.removeFromLeft(140));
    header.removeFromLeft(4);
    nicknameLabel_.setBounds(header);  // Shows "as: YourNickname"

    // Error label (above input, only when visible)
    if (errorVisible_) {
        auto errorArea = r.removeFromBottom(24).reduced(8, 2);
        errorLabel_.setBounds(errorArea);
        errorLabel_.setVisible(true);
    } else {
        errorLabel_.setVisible(false);
    }

    // Input area: Input | Emoji | Send
    auto inputArea = r.removeFromBottom(50).reduced(8, 8);
    sendButton_.setBounds(inputArea.removeFromRight(60));
    inputArea.removeFromRight(4);
    emojiButton_.setBounds(inputArea.removeFromRight(36));
    inputArea.removeFromRight(4);
    inputField_.setBounds(inputArea);

    // Connection status (centered in content area when not connected)
    auto contentArea = r.reduced(4);

    if (!connected_) {
        auto statusArea = contentArea.withSizeKeepingCentre(300, 100);
        connectionStatusLabel_.setBounds(statusArea.removeFromTop(30));
        statusArea.removeFromTop(10);

        auto buttonArea = statusArea.removeFromTop(40);
        if (lastStatus_ == ChatService::ConnectionStatus::AuthRequired) {
            signInButton_.setBounds(buttonArea.withSizeKeepingCentre(120, 36));
            signInButton_.setVisible(true);
            reconnectButton_.setVisible(false);
        } else if (lastStatus_ == ChatService::ConnectionStatus::Disconnected ||
                   lastStatus_ == ChatService::ConnectionStatus::Error) {
            reconnectButton_.setBounds(buttonArea.withSizeKeepingCentre(120, 36));
            reconnectButton_.setVisible(true);
            signInButton_.setVisible(false);
        } else {
            reconnectButton_.setVisible(false);
            signInButton_.setVisible(false);
        }
        connectionStatusLabel_.setVisible(true);

        // Hide message area
        messageViewport_.setBounds(0, 0, 0, 0);
    } else {
        connectionStatusLabel_.setVisible(false);
        reconnectButton_.setVisible(false);
        signInButton_.setVisible(false);

        // Message viewport fills content area
        messageViewport_.setBounds(contentArea);
        messageList_->updateLayout(contentArea.getWidth() - 20);  // Account for scrollbar
    }
}

void ChatPage::timerCallback()
{
    // Periodically check if we need to update UI
    if (chatService_) {
        auto currentStatus = chatService_->getConnectionStatus();
        if (currentStatus != lastStatus_) {
            lastStatus_ = currentStatus;
            updateConnectionUI();
        }
    }
}

void ChatPage::connectToChat()
{
    juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " === connectToChat START ===");

    if (chatService_) {
        chatService_->connect();
    } else {
        juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " ERROR: No chat service");
        connectionStatusLabel_.setText("Chat service unavailable", juce::dontSendNotification);
    }

    juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " === connectToChat END ===");
}

void ChatPage::disconnectFromChat()
{
    juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " === disconnectFromChat ===");

    if (chatService_) {
        chatService_->disconnect();
    }
    connected_ = false;
}

void ChatPage::showError(const juce::String& message)
{
    juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " Showing error: " + message);

    errorLabel_.setText("  " + message, juce::dontSendNotification);  // Padding for visual
    errorVisible_ = true;
    resized();
    repaint();

    // Auto-hide after 4 seconds
    juce::Component::SafePointer<ChatPage> safeThis(this);
    juce::Timer::callAfterDelay(4000, [safeThis] {
        if (safeThis == nullptr) return;
        juce::MessageManager::callAsync([safeThis] {
            if (safeThis != nullptr)
                safeThis->hideError();
        });
    });
}

void ChatPage::hideError()
{
    if (errorVisible_) {
        errorVisible_ = false;
        resized();
        repaint();
    }
}

void ChatPage::onSettingsClicked()
{
    juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " Settings clicked");
    showNicknameDialog();
}

void ChatPage::showNicknameDialog()
{
    juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " === showNicknameDialog ===");

    // Create dialog content
    auto* content = new juce::Component();
    content->setSize(300, 120);

    auto* label = new juce::Label({}, "Enter your chat nickname:");
    label->setFont(juce::Font(14.0f));
    label->setColour(juce::Label::textColourId, juce::Colours::white);
    label->setBounds(10, 10, 280, 20);
    content->addAndMakeVisible(label);

    auto* editor = new juce::TextEditor();
    editor->setMultiLine(false);
    editor->setTextToShowWhenEmpty("Your nickname...", juce::Colour(0xFF666666));
    editor->setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xFF1A1A1A));
    editor->setColour(juce::TextEditor::textColourId, juce::Colours::white);
    editor->setColour(juce::TextEditor::outlineColourId, juce::Colour(0xFF00FFFF).withAlpha(0.3f));
    editor->setBounds(10, 35, 280, 30);

    // Pre-fill with current nickname
    if (chatService_) {
        std::string currentNick = chatService_->getNickname();
        if (!currentNick.empty()) {
            editor->setText(juce::String(currentNick));
        }
    }

    content->addAndMakeVisible(editor);

    auto* infoLabel = new juce::Label({}, "2-30 characters, letters/numbers/spaces only");
    infoLabel->setFont(juce::Font(11.0f));
    infoLabel->setColour(juce::Label::textColourId, juce::Colour(0xFF888888));
    infoLabel->setBounds(10, 70, 280, 16);
    content->addAndMakeVisible(infoLabel);

    auto* errorLabel = new juce::Label({}, "");
    errorLabel->setFont(juce::Font(11.0f));
    errorLabel->setColour(juce::Label::textColourId, juce::Colour(0xFFFF6666));
    errorLabel->setBounds(10, 90, 280, 16);
    content->addAndMakeVisible(errorLabel);

    // Show dialog
    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "Set Chat Nickname";
    options.dialogBackgroundColour = juce::Colour(0xFF1A1A1A);
    options.content.setOwned(content);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = false;
    options.resizable = false;

    // Capture pointers for lambda
    auto* editorPtr = editor;
    auto* errorLabelPtr = errorLabel;
    auto* chatServicePtr = chatService_;

    // Create custom buttons
    auto* dialog = options.launchAsync();
    if (dialog) {
        // Add OK/Cancel buttons manually in the content
        auto* okButton = new juce::TextButton("Save");
        okButton->setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF00AA00));
        okButton->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        okButton->setBounds(130, 90, 70, 25);

        auto* cancelButton = new juce::TextButton("Cancel");
        cancelButton->setColour(juce::TextButton::buttonColourId, juce::Colour(0xFF444444));
        cancelButton->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        cancelButton->setBounds(210, 90, 70, 25);

        content->addAndMakeVisible(okButton);
        content->addAndMakeVisible(cancelButton);

        // Resize content for buttons
        content->setSize(300, 130);
        errorLabel->setBounds(10, 70, 110, 16);

        okButton->onClick = [dialog, editorPtr, errorLabelPtr, chatServicePtr, this] {
            juce::String nickname = editorPtr->getText().trim();

            if (nickname.isEmpty()) {
                errorLabelPtr->setText("Enter a nickname", juce::dontSendNotification);
                return;
            }

            if (chatServicePtr) {
                std::string errorMsg;
                if (chatServicePtr->setNickname(nickname.toStdString(), errorMsg)) {
                    // Update the header label immediately
                    nicknameLabel_.setText("as: " + nickname, juce::dontSendNotification);
                    juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " Nickname updated in UI: " + nickname);
                    dialog->exitModalState(1);
                    delete dialog;
                } else {
                    errorLabelPtr->setText(juce::String(errorMsg), juce::dontSendNotification);
                }
            }
        };

        cancelButton->onClick = [dialog] {
            dialog->exitModalState(0);
            delete dialog;
        };
    }
}

void ChatPage::onSendClicked()
{
    juce::String text = inputField_.getText().trim();

    if (text.isEmpty()) {
        return;
    }

    // Hide any previous error
    hideError();

    // Local validation before sending
    std::string textStr = text.toStdString();

    // Check message length
    if (textStr.length() < 2) {
        showError("Message too short (min 2 characters)");
        return;
    }
    if (textStr.length() > 500) {
        showError("Message too long (max 500 characters)");
        return;
    }

    // Use ChatService validation for URLs/emails/duplicates
    if (chatService_) {
        std::string validationError;
        if (!chatService_->validateContent(textStr, validationError)) {
            showError(juce::String(validationError));
            return;
        }

        // Check for duplicate
        if (chatService_->isDuplicate(textStr)) {
            showError("Please don't send the same message repeatedly");
            return;
        }
    }

    juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " Sending message: " + text.substring(0, 50) + "...");

    if (chatService_ && chatService_->sendMessage(text.toStdString())) {
        inputField_.clear();
    } else {
        // Show generic error if send failed
        showError("Failed to send message. Please try again.");
        juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " Failed to send message");
    }
}

void ChatPage::onReconnectClicked()
{
    juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " Reconnect clicked");
    connectToChat();
}

void ChatPage::onSignInClicked()
{
    juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " Sign in clicked - starting browser authentication");

    // Directly trigger browser authentication
    if (auto* registry = samplar::ServiceLocator::getRegistry()) {
        if (auto* licenseSvc = dynamic_cast<LicenseSystemService*>(registry->get("license"))) {
            // Update UI to show connecting state
            connectionStatusLabel_.setText("Opening browser...", juce::dontSendNotification);
            connectionStatusLabel_.setColour(juce::Label::textColourId, juce::Colour(0xFFFFAA00));
            signInButton_.setEnabled(false);
            repaint();

            // Start browser authentication
            licenseSvc->startBrowserAuthentication([this](bool success, const std::string& message) {
                juce::MessageManager::callAsync([this, success, message] {
                    signInButton_.setEnabled(true);

                    if (success) {
                        juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " Browser auth successful, connecting to chat...");
                        connectionStatusLabel_.setText("Signed in! Connecting...", juce::dontSendNotification);
                        connectionStatusLabel_.setColour(juce::Label::textColourId, juce::Colour(0xFF00FF00));
                        // Now try to connect to chat
                        connectToChat();
                    } else {
                        juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " Browser auth failed: " + juce::String(message));
                        connectionStatusLabel_.setText("Sign in failed: " + juce::String(message), juce::dontSendNotification);
                        connectionStatusLabel_.setColour(juce::Label::textColourId, juce::Colour(0xFFFF4444));
                    }
                    resized();
                    repaint();
                });
            });
        } else {
            juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " ERROR: License service not found");
            connectionStatusLabel_.setText("License service unavailable", juce::dontSendNotification);
        }
    }
}

void ChatPage::updateConnectionUI()
{
    switch (lastStatus_) {
        case ChatService::ConnectionStatus::Connected:
            connected_ = true;
            connectionStatusLabel_.setColour(juce::Label::textColourId, juce::Colour(0xFF00FF00));
            connectionStatusLabel_.setText("Connected", juce::dontSendNotification);
            break;

        case ChatService::ConnectionStatus::Connecting:
            connected_ = false;
            connectionStatusLabel_.setColour(juce::Label::textColourId, juce::Colour(0xFFFFAA00));
            connectionStatusLabel_.setText("Connecting...", juce::dontSendNotification);
            break;

        case ChatService::ConnectionStatus::Disconnected:
            connected_ = false;
            connectionStatusLabel_.setColour(juce::Label::textColourId, juce::Colour(0xFFFF4444));
            connectionStatusLabel_.setText("Disconnected", juce::dontSendNotification);
            break;

        case ChatService::ConnectionStatus::AuthRequired:
            connected_ = false;
            connectionStatusLabel_.setColour(juce::Label::textColourId, juce::Colour(0xFFFFAA00));
            connectionStatusLabel_.setText("Please sign in to chat", juce::dontSendNotification);
            break;

        case ChatService::ConnectionStatus::Error:
            connected_ = false;
            connectionStatusLabel_.setColour(juce::Label::textColourId, juce::Colour(0xFFFF4444));
            connectionStatusLabel_.setText("Connection error", juce::dontSendNotification);
            break;
    }

    resized();
    repaint();
}

void ChatPage::scrollToBottom()
{
    if (messageList_) {
        messageViewport_.setViewPosition(0, messageList_->getContentHeight());
    }
}

void ChatPage::onMessagesReceived(const std::vector<ChatService::ChatMessage>& messages)
{
    juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " Received " + juce::String(static_cast<int>(messages.size())) + " messages");

    juce::MessageManager::callAsync([this, messages] {
        if (messageList_) {
            messageList_->addMessages(messages);
            scrollToBottom();
        }
    });
}

void ChatPage::onStatusChanged(ChatService::ConnectionStatus status, const std::string& message)
{
    juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " Status changed: " + juce::String(static_cast<int>(status)) + " - " + juce::String(message));

    juce::MessageManager::callAsync([this, status, message] {
        lastStatus_ = status;
        updateConnectionUI();
    });
}

void ChatPage::onPresenceUpdated(int count, const std::vector<std::string>& users)
{
    juce::MessageManager::callAsync([this, count] {
        usersCountLabel_.setText("Users: " + juce::String(count), juce::dontSendNotification);
    });
}

// ============================================================================
// Emoji Picker
// ============================================================================

class ChatPage::EmojiPicker : public juce::Component {
public:
    std::function<void(const juce::String&)> onEmojiSelected;

    EmojiPicker() {
        setSize(280, 200);

        // Common emojis organized by category
        emojis_ = {
            // Smileys
            "\xF0\x9F\x98\x80", "\xF0\x9F\x98\x81", "\xF0\x9F\x98\x82", "\xF0\x9F\x98\x83",  // 😀😁😂😃
            "\xF0\x9F\x98\x84", "\xF0\x9F\x98\x85", "\xF0\x9F\x98\x86", "\xF0\x9F\x98\x89",  // 😄😅😆😉
            "\xF0\x9F\x98\x8A", "\xF0\x9F\x98\x8D", "\xF0\x9F\x98\x8E", "\xF0\x9F\x98\x8F",  // 😊😍😎😏
            "\xF0\x9F\x98\xAD", "\xF0\x9F\x98\xB1", "\xF0\x9F\x98\xB3", "\xF0\x9F\xA4\x94",  // 😭😱😳🤔
            // Gestures
            "\xF0\x9F\x91\x8D", "\xF0\x9F\x91\x8E", "\xF0\x9F\x91\x8F", "\xF0\x9F\x99\x8C",  // 👍👎👏🙌
            "\xF0\x9F\x94\xA5", "\xE2\x9C\xA8", "\xF0\x9F\x92\xAF", "\xF0\x9F\x92\xAA",      // 🔥✨💯💪
            // Music
            "\xF0\x9F\x8E\xB5", "\xF0\x9F\x8E\xB6", "\xF0\x9F\x8E\xA4", "\xF0\x9F\x8E\xA7",  // 🎵🎶🎤🎧
            "\xF0\x9F\xA5\x81", "\xF0\x9F\xA5\x82", "\xF0\x9F\x94\x8A", "\xF0\x9F\x94\x89",  // 🥁🥂🔊🔉
            // Objects
            "\xE2\x9D\xA4\xEF\xB8\x8F", "\xF0\x9F\x92\x9C", "\xF0\x9F\x96\xA4", "\xF0\x9F\x92\x99", // ❤️💜🖤💙
            "\xE2\x9C\x85", "\xE2\x9D\x8C", "\xF0\x9F\x91\x80", "\xF0\x9F\x92\xAC"           // ✅❌👀💬
        };
    }

    void paint(juce::Graphics& g) override {
        // Background
        g.fillAll(juce::Colour(0xFF1A1A1A));

        // Border
        g.setColour(juce::Colour(0xFF00FFFF));
        g.drawRect(getLocalBounds(), 2);

        // Draw emojis in grid
        int cols = 8;
        int cellSize = 32;
        int padding = 6;
        int startX = (getWidth() - cols * cellSize) / 2;
        int startY = padding;

        g.setFont(juce::Font(20.0f));

        for (size_t i = 0; i < emojis_.size(); ++i) {
            int col = static_cast<int>(i) % cols;
            int row = static_cast<int>(i) / cols;

            int x = startX + col * cellSize;
            int y = startY + row * cellSize;

            // Hover highlight
            juce::Rectangle<int> cellBounds(x, y, cellSize, cellSize);
            if (cellBounds.contains(lastMousePos_)) {
                g.setColour(juce::Colour(0xFF333333));
                g.fillRect(cellBounds);
            }

            // Emoji
            g.setColour(juce::Colours::white);
            g.drawText(juce::String::fromUTF8(emojis_[i].c_str()),
                       cellBounds, juce::Justification::centred, false);
        }
    }

    void mouseMove(const juce::MouseEvent& e) override {
        lastMousePos_ = e.getPosition();
        repaint();
    }

    void mouseDown(const juce::MouseEvent& e) override {
        int cols = 8;
        int cellSize = 32;
        int padding = 6;
        int startX = (getWidth() - cols * cellSize) / 2;
        int startY = padding;

        for (size_t i = 0; i < emojis_.size(); ++i) {
            int col = static_cast<int>(i) % cols;
            int row = static_cast<int>(i) / cols;

            int x = startX + col * cellSize;
            int y = startY + row * cellSize;

            juce::Rectangle<int> cellBounds(x, y, cellSize, cellSize);
            if (cellBounds.contains(e.getPosition())) {
                if (onEmojiSelected) {
                    onEmojiSelected(juce::String::fromUTF8(emojis_[i].c_str()));
                }
                break;
            }
        }
    }

private:
    std::vector<std::string> emojis_;
    juce::Point<int> lastMousePos_;
};

void ChatPage::showEmojiPicker()
{
    // Toggle behavior - if already visible, hide it
    if (emojiPicker_ && emojiPicker_->isVisible()) {
        emojiPicker_->setVisible(false);
        return;
    }

    juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " Opening emoji picker");

    if (!emojiPicker_) {
        emojiPicker_ = std::make_unique<EmojiPicker>();
        emojiPicker_->onEmojiSelected = [this](const juce::String& emoji) {
            insertEmoji(emoji);
            // Close picker after selection
            emojiPicker_->setVisible(false);
        };
    }

    // Position above emoji button
    auto buttonBounds = emojiButton_.getBoundsInParent();
    int pickerX = buttonBounds.getX() - 100;  // Center roughly
    int pickerY = buttonBounds.getY() - 210;  // Above button

    // Keep within page bounds
    pickerX = juce::jmax(8, juce::jmin(pickerX, getWidth() - 288));
    pickerY = juce::jmax(50, pickerY);

    emojiPicker_->setBounds(pickerX, pickerY, 280, 200);

    addAndMakeVisible(*emojiPicker_);
    emojiPicker_->toFront(true);
}

void ChatPage::insertEmoji(const juce::String& emoji)
{
    juce::Logger::writeToLog(juce::String(LOG_PREFIX) + " Inserting emoji: " + emoji);

    // Insert at cursor position
    inputField_.insertTextAtCaret(emoji);
    inputField_.grabKeyboardFocus();
}

} // namespace samplar
