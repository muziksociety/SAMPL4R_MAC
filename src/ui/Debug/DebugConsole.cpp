#include "DebugConsole.h"

using namespace juce;

namespace samplar {

DebugConsole::DebugConsole()
{
    setOpaque(true);
    
    // Create CodeEditorComponent with document and no tokenizer for better performance
    output_ = std::make_unique<juce::CodeEditorComponent>(document_, nullptr);
    output_->setReadOnly(true);
    output_->setScrollbarThickness(12);
    addAndMakeVisible(output_.get());

    clearButton_.onClick = [this]{ clear(); };
    addAndMakeVisible(clearButton_);
}

DebugConsole::~DebugConsole()
{
    // CRITICAL: Cancel any pending async updates before object memory is freed.
    // Without this, a queued handleAsyncUpdate() fires on a destroyed object → crash.
    cancelPendingUpdate();
}

void DebugConsole::paint(Graphics& g)
{
    g.fillAll(Colours::darkgrey.darker(0.6f));
}

void DebugConsole::resized()
{
    auto r = getLocalBounds().reduced(4);
    auto buttonH = 24;
    auto top = r.removeFromTop(buttonH);
    clearButton_.setBounds(top.removeFromRight(60));
    r.removeFromTop(2);
    output_->setBounds(r);
}

void DebugConsole::appendLine(String text)
{
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        pending_.add(text);
        
        // PERFORMANCE FIX: Only trigger updates every 5 messages or 50ms
        auto currentTime = Time::getMillisecondCounterHiRes();
        if (pending_.size() >= 5 || currentTime - lastUpdateTime_ > 50.0)
        {
            triggerAsyncUpdate();
            lastUpdateTime_ = currentTime;
        }
    }
}

void DebugConsole::clear()
{
    document_.replaceAllContent("");
}

void DebugConsole::handleAsyncUpdate()
{
    StringArray toFlush;
    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        toFlush.swapWith(pending_);
    }
    if (toFlush.size() > 0)
    {
        // PERFORMANCE FIX: Limit buffer to last 1000 lines to prevent memory issues
        auto currentText = document_.getAllContent();
        auto lines = StringArray::fromLines(currentText);
        
        // If we have too many lines, keep only the most recent ones
        if (lines.size() > 1000)
        {
            lines.removeRange(0, lines.size() - 800); // Keep last 800 lines
            document_.replaceAllContent(lines.joinIntoString("\n"));
        }
        
        // Append new content
        String newContent = toFlush.joinIntoString("\n") + "\n";
        document_.insertText(document_.getNumCharacters(), newContent);
        
        // Auto-scroll to bottom if enabled (disabled by default for performance)
        if (autoscroll_)
        {
            output_->scrollToLine(document_.getNumLines() - 1);
        }
    }
}

void DebugConsole::mouseUp(const MouseEvent& e)
{
    if (e.mods.isPopupMenu())
    {
        PopupMenu m;
        m.addItem(1, "Select All");
        m.addItem(2, "Copy");
        m.addSeparator();
        m.addItem(3, wrap_ ? "Disable Wrap" : "Enable Wrap");
        m.addItem(4, autoscroll_ ? "Disable Auto-Scroll" : "Enable Auto-Scroll");
        m.addSeparator();
        m.addItem(5, "Save Log File...");
        m.addItem(6, "Clear");
        m.showMenuAsync(PopupMenu::Options(), [this](int res){
            if (res == 1) { output_->selectAll(); }
            else if (res == 2) {
                // Copy selected text to clipboard
                auto selectedText = output_->getTextInRange(output_->getHighlightedRegion());
                if (selectedText.isEmpty()) selectedText = document_.getAllContent();
                SystemClipboard::copyTextToClipboard(selectedText);
            }
            else if (res == 3) { setWordWrap(!wrap_); }
            else if (res == 4) { setAutoScroll(!autoscroll_); }
            else if (res == 5) {
                auto chooser = std::make_shared<FileChooser>("Save log", File::getSpecialLocation(File::userDesktopDirectory).getChildFile("SAMPLAR_log.txt"));
                auto flags = FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles;
                chooser->launchAsync(flags, [this, chooser](const FileChooser& ch){
                    auto f = ch.getResult();
                    if (f.getFileName().isNotEmpty()) f.replaceWithText(document_.getAllContent());
                });
            }
            else if (res == 6) { clear(); }
        });
    }
}

void DebugConsole::setWordWrap(bool shouldWrap)
{
    wrap_ = shouldWrap;
    // CodeEditorComponent handles word wrap differently - this is mainly for UI state tracking
}

void DebugConsole::setAutoScroll(bool enabled)
{
    autoscroll_ = enabled;
}

bool DebugConsole::getWordWrap() const { return wrap_; }
bool DebugConsole::getAutoScroll() const { return autoscroll_; }

} // namespace samplar
