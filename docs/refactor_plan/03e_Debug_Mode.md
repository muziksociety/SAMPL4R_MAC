# 03e – Debug Mode (F1x10 Console)

Requires: `Sample_monsta5/docs/refactor_plan/03_UI_Shell_and_Navigation.md`
Provides: built-in console for diagnostics, toggled by pressing F1 ten times within 4 seconds.

Design
- Toggle gesture: press F1 10x within 4 seconds (counter resets after 4s of inactivity).
- Console: bottom-docked panel with a read-only text output and Clear button.
- Logging: `juce::Logger::setCurrentLogger(ConsoleLogger)` forwards messages to the console via AsyncUpdater.
- Scope: console is global to the app window; size ~180px height when visible.

Deliverables
- `src/ui/Debug/DebugConsole.*`
- Wiring in `src/ui/MainWindow.cpp` (RootComponent) to handle F1x10 and dock the console.

Acceptance
- Pressing F1x10 toggles the console; logs appear in real time.
- No performance impact when hidden (no painting); thread-safe appends.

