# SAMPLAR - Task Completion Checklist

## When Adding New Games

### 1. Game Implementation
- [ ] Inherit from `IMiniGame` and `juce::Timer` 
- [ ] Implement required interface: `start()`, `stop()`, `setProgress(float)`
- [ ] Add JUCE overrides: `paint()`, `resized()`, `keyPressed()`
- [ ] Follow naming conventions (trailing underscores for members)
- [ ] Include waiting state with instructions: `waitingForStart_`

### 2. Asset Integration
- [ ] Add assets to `assets/` directory if needed
- [ ] Update CMakeLists.txt to embed resources via `BinaryData`
- [ ] Load assets in constructor with error handling
- [ ] Verify assets work without network dependencies

### 3. Registration
- [ ] Register game factory in `MiniGameHost`
- [ ] Test game rotation and selection
- [ ] Verify progress synchronization with audio processing

### 4. Testing Requirements
- [ ] Game starts/stops properly via interface
- [ ] Responds to progress updates (difficulty/speed scaling) 
- [ ] Handles all input types (keyboard, mouse)
- [ ] Works in both windowed and fullscreen modes
- [ ] No memory leaks during extended play sessions

## When Modifying Core Services

### 1. Service Changes
- [ ] Update service in `src/services/`
- [ ] Maintain event bus communication patterns
- [ ] Preserve service lifecycle: `initialise()`, `shutdown()`
- [ ] Update `ServiceRegistry` if needed

### 2. Event Bus Integration
- [ ] Use event publishing for service communication
- [ ] Avoid direct service dependencies
- [ ] Document new event types

### 3. Testing Service Changes
- [ ] Verify service startup/shutdown
- [ ] Test event bus message flow
- [ ] Check integration with audio processing pipeline

## Build & Release Checklist

### 1. Pre-Build Verification
- [ ] Clean previous build: `cmake --build build/sm5 --target clean`
- [ ] Verify all dependencies: JUCE, SoundTouch, assets
- [ ] Check CMake configuration warnings

### 2. Build Process
- [ ] Configure: `cmake -S . -B build/sm5 -DWITH_SOUNDTOUCH=ON`
- [ ] Build Release: `cmake --build build/sm5 --config Release`
- [ ] Verify executable: `"build/sm5/SM5App_artefacts/Release/SAMPLAR.exe"`

### 3. Quality Assurance
- [ ] Test all 7 games individually
- [ ] Verify audio processing + gaming simultaneously  
- [ ] Check progress synchronization
- [ ] Test game switching functionality
- [ ] Verify asset loading (no missing textures/sounds)

### 4. Performance Validation
- [ ] Monitor memory usage during extended sessions
- [ ] Verify 60fps game performance
- [ ] Test CPU usage during audio processing + gaming
- [ ] Check for memory leaks

## Documentation Updates

### 1. Code Documentation
- [ ] Update inline comments for complex logic
- [ ] Document new public interfaces
- [ ] Add TODO comments for future work

### 2. Project Documentation
- [ ] Update README if new features added
- [ ] Update build instructions if dependencies changed
- [ ] Document new configuration options

## Platform-Specific Testing

### 1. Windows (Primary Platform)
- [ ] Test on Windows 10/11
- [ ] Verify Visual Studio build compatibility
- [ ] Test with different screen resolutions
- [ ] Verify Windows-specific crash handlers work

### 2. Cross-Platform Considerations
- [ ] Ensure JUCE cross-platform compatibility maintained
- [ ] Test asset loading paths work on different filesystems
- [ ] Verify CMake builds work on target platforms