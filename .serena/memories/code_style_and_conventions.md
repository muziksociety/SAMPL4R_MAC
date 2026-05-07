# SAMPLAR - Code Style & Conventions

## Naming Conventions

### Classes and Structs
- **PascalCase** for class names: `MiniGameHost`, `ProcessingCoordinatorService`
- **Interface prefix**: `IMiniGame` for abstract interfaces
- **Service suffix**: Classes providing core functionality end with `Service`

### Variables and Functions
- **camelCase** for functions: `setProgress()`, `startSession()`, `getApplicationName()`
- **Trailing underscore** for private members: `gameActive_`, `score_`, `rng_`
- **Clear descriptive names**: `waitingForStart_`, `lastUpdateTime_`, `stepIntervalSec_`

### Constants and Enums
- **SCREAMING_SNAKE_CASE** for constants: `GRID_SIZE`, `MERGE_ANIMATION_DURATION`
- **PascalCase** for enum values: `enum Dir { Up, Down, Left, Right }`
- **Scoped enums preferred**: `enum class PieceType { I, O, T, S, Z, J, L }`

## File Organization

### Header Files (.h)
```cpp
#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <memory>
#include "LocalHeaders.h"

namespace samplar {
    class ClassName {
    public:
        // Public interface first
        
    private:
        // Private members with trailing underscore
    };
}
```

### Source Files (.cpp)
```cpp
#include "Header.h"
#include "OtherHeaders.h"

using namespace juce;

namespace samplar {
    // Implementation
}
```

## Game-Specific Patterns

### IMiniGame Interface Implementation
```cpp
class GameName : public IMiniGame, private juce::Timer {
public:
    // IMiniGame interface (required)
    void start() override;
    void stop() override; 
    void setProgress(float progress) override;
    
    // JUCE Component interface
    void paint(juce::Graphics& g) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress& key) override;
    
private:
    void timerCallback() override;
    
    // Game state with trailing underscores
    bool gameActive_ = false;
    bool paused_ = false;
    bool waitingForStart_ = true;
    float progress_ = 0.0f;
    
    // Helper methods
    void resetGame();
    void updateGame(float deltaTime);
};
```

## Memory Management
- **RAII principles**: Use smart pointers (`std::unique_ptr`, `std::shared_ptr`)
- **JUCE ownership**: `std::unique_ptr<juce::Component>` for owned components
- **Stack allocation preferred** for small objects and game state

## Error Handling
- **Logging over exceptions**: Use `juce::Logger::writeToLog()` for diagnostics
- **Graceful degradation**: Games should handle missing assets gracefully
- **Defensive programming**: Check bounds, validate input, handle edge cases

## Performance Guidelines
- **60 FPS target**: All games designed for 60fps with `juce::Timer`
- **Minimal allocations**: Avoid allocations in game loops
- **Asset preloading**: Load images/resources in constructor, not during gameplay
- **Efficient rendering**: Use JUCE's optimized drawing methods

## Documentation Style
- **Brief inline comments** for complex logic only
- **Function documentation** for public interfaces
- **TODO comments** for future enhancements: `// TODO: Implement sound effects`