# SAMPLAR - Essential Development Commands

## Build Commands (Windows)

### Configure Build
```bash
# Standard Release build
cmake -S . -B build/sm5 -DWITH_SOUNDTOUCH=ON

# Debug build with all features
cmake -S . -B build/sm5 -DCMAKE_BUILD_TYPE=Debug -DWITH_SOUNDTOUCH=ON -DWITH_DEMUCS_CLI=ON

# Offline build (no network dependencies)
cmake -S . -B build/sm5 -DWITH_SOUNDTOUCH=OFF
```

### Build Project
```bash
# Release build
cmake --build build/sm5 --config Release

# Debug build  
cmake --build build/sm5 --config Debug

# Build specific target
cmake --build build/sm5 --target Samplar --config Release
```

## Run Commands

### Execute Application
```bash
# From build directory
"build/sm5/SM5App_artefacts/Release/SAMPLAR.exe"

# Debug version
"build/sm5/SM5App_artefacts/Debug/SAMPLAR.exe"
```

## Windows Development Utilities

### File Operations
```cmd
dir /s *.cpp          # Find all C++ files recursively
findstr /s "pattern"  # Search text in files (grep equivalent)
where cmake           # Find cmake executable (which equivalent)
```

### Process Management
```cmd
tasklist | findstr "Sample"     # Find running SAMPLAR processes
taskkill /f /im "Sample*.exe"   # Force kill SAMPLAR processes
```

### Git Operations (Standard)
```bash
git status
git add .
git commit -m "message"
git push origin master
```

## Testing & Debugging

### Log Analysis
```bash
# Check application logs
type SAMPLAR_debug.log

# Real-time log monitoring
powershell Get-Content SAMPLAR_debug.log -Wait
```

### Memory & Performance
```bash
# Check build artifacts
dir "build/sm5" /s

# Clean build
cmake --build build/sm5 --target clean
```

## Project Management

### Asset Management
```bash
# List game assets
dir assets /s
dir "src/ui/Games" 

# Verify embedded resources
findstr /s "BinaryData" "src/**/*.cpp"
```

### Dependency Verification
```bash
# Check JUCE installation
dir JUCE
dir ../JUCE

# Verify CMake configuration
cmake -S . -B build/test --dry-run
```