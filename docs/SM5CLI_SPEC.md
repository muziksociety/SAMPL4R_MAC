# SM5CLI - SAMPLAR Command Line Interface Specification

**Version:** 1.0  
**Date:** 2026-02-14  
**Authors:** Echo (Windows), Phantom (Mac)  

## Overview

SM5CLI is a cross-platform command line interface for remote testing and automation of SAMPLAR applications. It provides a unified interface for testing, building, and pipeline operations across Windows and Mac platforms.

## Architecture

### Design Principles
- **Cross-platform consistency:** Same command structure on both platforms
- **Existing infrastructure:** Leverages existing build systems and test runners
- **No functionality disruption:** Operates headless, doesn't interfere with GUI
- **Standardized results:** JSON output format for agent coordination
- **File-based communication:** Simple file I/O instead of complex messaging

### Platform Implementation

| Component | Windows | Mac | Shared |
|-----------|---------|-----|---------|
| CLI Wrapper | `scripts/sm5cli.ps1` | `scripts/sm5cli` | Same interface |
| Test Runner | `SM5Tests.exe` | `SM5Tests` | Same JUCE code |
| Build System | PowerShell + CMake | Bash + CMake | CMake configuration |
| Result Format | JSON | JSON | Standardized contract |

## Command Interface

### Unified Command Structure
```bash
# Cross-platform commands (same on Windows/Mac)
sm5cli status                                    # Show CLI status
sm5cli test [options]                           # Run tests  
sm5cli build [options]                          # Build project
sm5cli pipeline [options]                       # Process audio pipeline
sm5cli help                                     # Show help
```

### Platform-Specific Execution

**Windows:**
```powershell
.\scripts\sm5cli.ps1 <command> [options]
```

**Mac:**
```bash
./scripts/sm5cli <command> [options]
```

## Commands Reference

### 1. status
Shows current SM5CLI status and environment information.

**Usage:**
```bash
sm5cli status
```

**Output (JSON):**
```json
{
  "platform": "windows|mac",
  "timestamp": "2026-02-14T07:57:37Z",
  "SAMPLAR_root": "/path/to/sample_monsta5",
  "build_dir": "/path/to/build",
  "tests_available": true,
  "cmake_configured": true
}
```

### 2. test
Runs SM5Tests with specified category and options.

**Usage:**
```bash
sm5cli test [options]
```

**Options:**
- `--category <string>` - Test category (Core, Services, Engines, Analysis, Utils, ML, Licensing)
- `--verbose` - Verbose output
- `--output <json|text>` - Output format (default: json)

**Examples:**
```bash
sm5cli test                                     # Run all tests
sm5cli test --category=Engines                 # Test variation engines only
sm5cli test --category=Core --verbose          # Verbose core tests
```

**Output (JSON):**
```json
{
  "platform": "windows|mac",
  "command": "test",
  "category": "Engines",
  "status": "success|failed",
  "results": {
    "passed": 12,
    "failed": 0,
    "duration_ms": 847,
    "exit_code": 0
  },
  "timestamp": "2026-02-14T07:57:37Z",
  "logs": ["path/to/output.log", "path/to/error.log"]
}
```

### 3. build
Builds the SAMPLAR project with specified configuration.

**Usage:**
```bash
sm5cli build [options]
```

**Options:**
- `--type <debug|release>` - Build type (default: debug)
- `--verify` - Force CMake reconfiguration
- `--log-level <info|verbose>` - Log verbosity

**Platform-specific Options:**
- **Mac only:** `--universal` - Build universal binary (x86_64 + arm64)

**Examples:**
```bash
sm5cli build                                    # Debug build
sm5cli build --type=release                    # Release build  
sm5cli build --type=release --verify           # Clean release build
sm5cli build --type=release --universal        # Mac universal binary
```

**Output (JSON):**
```json
{
  "platform": "windows|mac",
  "command": "build", 
  "type": "Release",
  "status": "success|failed",
  "results": {
    "duration_ms": 45000,
    "build_dir": "/path/to/build",
    "executables": [
      "/path/to/SAMPLAR.exe",
      "/path/to/SM5Tests.exe"
    ]
  },
  "timestamp": "2026-02-14T07:57:37Z"
}
```

### 4. pipeline
Runs SAMPLAR audio processing pipeline for testing.

**Usage:**
```bash
sm5cli pipeline --input <file> [options]
```

**Options:**
- `--input <path>` - Input audio file (required)
- `--output-dir <path>` - Output directory (default: tests/results)
- `--separation` - Enable stem separation
- `--variations <number>` - Number of variations to generate (default: 5)
- `--engine <string>` - Variation engine to use (default: all)

**Examples:**
```bash
sm5cli pipeline --input test.wav                                    # Basic processing
sm5cli pipeline --input song.wav --separation --variations=3        # Separation + 3 variations
sm5cli pipeline --input kick.wav --engine=FastSimpleVariation       # Specific engine
```

**Output (JSON):**
```json
{
  "platform": "windows|mac",
  "command": "pipeline",
  "input": "/path/to/input.wav",
  "status": "success|failed",
  "results": {
    "separation": true,
    "variations_generated": 3,
    "engine_used": "FastSimpleVariation",
    "duration_ms": 12000,
    "output_files": [
      "/path/to/output_1.wav",
      "/path/to/output_2.wav"
    ]
  },
  "timestamp": "2026-02-14T07:57:37Z"
}
```

## Result Storage

### File Locations
- **Results directory:** `tests/results/`
- **Latest result:** `tests/results/latest.json`
- **Build results:** `tests/results/build_latest.json`
- **Pipeline results:** `tests/results/pipeline_latest.json`
- **Log files:** `tests/results/*_output.log`, `tests/results/*_error.log`

### JSON Result Contract

All commands output standardized JSON with this structure:

```json
{
  "platform": "windows|mac",
  "command": "test|build|pipeline|status",
  "status": "success|failed|running",
  "results": {
    // Command-specific results
  },
  "timestamp": "2026-02-14T07:57:37Z",
  "logs": ["array", "of", "log", "paths"]
}
```

## Implementation Details

### Windows Implementation (`scripts/sm5cli.ps1`)

**Features:**
- PowerShell 5.1+ compatible
- Parameter validation and help system
- JUCE ChildProcess integration for SM5Tests execution
- CMake build automation
- Comprehensive error handling and logging
- JSON result serialization

**Key Components:**
```powershell
# Core functions
function Get-SM5Status { }        # Status reporting
function Invoke-SM5Test { }       # Test execution  
function Invoke-SM5Build { }      # Build automation
function Invoke-SM5Pipeline { }   # Pipeline testing
```

**Dependencies:**
- PowerShell 5.1+
- CMake (for building)
- Visual Studio 2022 or Build Tools
- SM5Tests.exe (built from project)

### Mac Implementation (`scripts/sm5cli`)

**Features:**
- Bash shell script
- Existing SM5Tests CLI integration
- Build automation via existing scripts
- Universal binary support
- Signed/unsigned build workflows

**Key Components:**
```bash
# Core functions  
status_command() { }              # Status reporting
test_command() { }               # Test execution
build_command() { }              # Build automation  
pipeline_command() { }           # Pipeline testing
```

**Dependencies:**
- macOS 10.15+ (Catalina)
- Xcode Command Line Tools
- CMake
- SM5Tests executable

## Agent Integration

### OpenClaw Agent Usage

**Command Execution:**
```bash
# Agent executes CLI command
sm5cli test --category=Engines --output=json

# Reads result file
cat tests/results/latest.json

# Parses JSON for status/results
```

**Cross-Platform Coordination:**
- Agents call platform-specific wrapper
- Same command interface on both platforms  
- Consistent JSON output format
- File-based result sharing

### Result Processing

**Agent Workflow:**
1. Execute `sm5cli` command
2. Check exit code (0 = success, 1 = failure)
3. Read JSON result from `tests/results/latest.json`
4. Parse results for status, metrics, logs
5. Report back to coordination system

## Error Handling

### Exit Codes
- `0` - Success
- `1` - Command failure
- `2` - Invalid arguments
- `3` - Environment error (missing dependencies)

### Error Response Format
```json
{
  "platform": "windows|mac",
  "command": "test",
  "status": "failed",
  "error": {
    "code": 1,
    "message": "SM5Tests execution failed",
    "details": "Process exited with code 1"
  },
  "timestamp": "2026-02-14T07:57:37Z",
  "logs": ["path/to/error.log"]
}
```

## Testing

### Manual Testing

**Windows:**
```powershell
# Test status
.\scripts\sm5cli.ps1 status

# Test help
.\scripts\sm5cli.ps1 help  

# Test build (if configured)
.\scripts\sm5cli.ps1 build --type=debug

# Test with sample file
.\scripts\sm5cli.ps1 pipeline --input sample.wav --variations=1
```

**Mac:**
```bash
# Test status  
./scripts/sm5cli status

# Run core tests
./scripts/sm5cli test --category=Core

# Build universal binary
./scripts/sm5cli build --type=release --universal
```

### Automated Testing

Both platforms support automated testing via exit codes:

```bash
# Test and check result
sm5cli test --category=Engines
if [ $? -eq 0 ]; then
  echo "Tests passed"
else
  echo "Tests failed"
fi
```

## Security Considerations

### Command Validation
- Allowlisted commands only (status, test, build, pipeline, help)
- Input file path validation
- Output directory sandboxing to `tests/results/`
- No arbitrary code execution

### File Access
- Read access: Source files, build files, test files
- Write access: Limited to `tests/results/` directory and build outputs
- No system file modifications outside project scope

## Future Enhancements

### Planned Features
1. **Remote agent coordination:** Cross-platform result sharing
2. **Advanced pipeline testing:** Multiple engines, batch processing  
3. **Performance profiling:** Memory/CPU monitoring during tests
4. **Integration testing:** Full workflow validation
5. **CI/CD integration:** GitHub Actions, Jenkins support

### Extension Points
- Additional test categories
- Custom engine testing
- Result visualization
- Automated performance regression detection

## Changelog

### Version 1.0 (2026-02-14)
- Initial implementation
- Windows PowerShell wrapper
- Mac bash wrapper  
- Unified command interface
- JSON result format
- Cross-platform testing support

---

**Implementation Status:**
- ✅ **Windows:** `scripts/sm5cli.ps1` - Complete and tested
- ✅ **Mac:** `scripts/sm5cli` - Complete and tested  
- ✅ **Specification:** Complete
- ✅ **Cross-platform validation:** Confirmed compatible

**Ready for production use and OpenClaw agent integration.**