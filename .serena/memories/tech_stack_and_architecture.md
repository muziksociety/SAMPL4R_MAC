# SAMPLAR - Technical Stack & Architecture

## Core Technology Stack
- **Language**: C++17 with modern features
- **Framework**: JUCE 7.x (cross-platform audio/GUI framework)
- **Build System**: CMake with advanced dependency management (368-line CMakeLists.txt)
- **AI Integration**: Demucs C++ CLI + Python bridge for audio separation
- **Performance**: Intel IPP support, OpenBLAS acceleration, AVX2 SIMD optimizations

## Architecture Pattern
**Service-Oriented Architecture** with event-driven communication:

```
App (JUCE Application)
├── ServiceRegistry (manages service lifecycle)
├── EventBus (pub/sub communication)
├── MainWindow (UI host)
└── Services:
    ├── AudioDeviceService
    ├── ProcessingCoordinatorService  
    ├── FullSongService
    ├── DrumSliceService
    ├── GeneratorService
    └── VariationService
```

## Game Architecture
**Plugin-based Mini-Game System**:

```
MiniGameHost (manages game rotation)
├── IMiniGame (interface)
│   ├── start() - begin game session
│   ├── stop() - end game session  
│   └── setProgress(0.0-1.0) - sync with audio processing
└── Game Implementations:
    ├── SnakeGame (garden theme, power-ups, smooth animations)
    ├── BlockPuzzleGame (Tetris-style with starfield background)
    ├── BreakoutGame (Tron theme, multi-ball, power-ups)
    ├── FlappyGame (asset-based sprites, physics)
    ├── Game2048 (tile animations, merge effects)
    ├── SpaceShooterGame (retro space invaders)
    └── DummyGame (fallback/testing)
```

## Key Dependencies
- **JUCE Modules**: gui_basics, gui_extra, audio_basics, audio_devices, audio_formats, dsp
- **Optional**: SoundTouch (constant-pitch preview), ZeroMQ (disabled), Demucs C++ lib (disabled, using CLI)
- **Build Options**: `WITH_SOUNDTOUCH=ON`, `WITH_DEMUCS_CLI=ON`

## Service Communication
Services communicate via **EventBus** pattern rather than direct coupling:
- `ProcessingCoordinatorService` publishes progress events
- `MiniGameHost` subscribes to progress updates
- Individual games receive progress via `setProgress(float)` interface