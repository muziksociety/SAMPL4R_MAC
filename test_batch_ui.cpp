// Quick syntax test for batch UI implementation
#include "src/ui/Pages/WavePreviewPage.h"
#include "src/ui/Pages/InitialPage.h"
#include <vector>

int main() {
    // Test that our new functions compile
    samplar::WavePreviewPage preview;
    std::vector<juce::File> files;
    files.push_back(juce::File("test.wav"));
    
    // This should compile if our interface is correct
    preview.loadMultipleFiles(files);
    
    // Test InitialPage callback exists
    samplar::InitialPage initial;
    initial.onOpenMultipleFiles = [](const std::vector<juce::File>& files) {
        // Callback exists and compiles
    };
    
    return 0;
}