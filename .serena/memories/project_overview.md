# SAMPLAR - Project Overview

## Project Purpose
SAMPLAR is a **native C++ audio application** built with the JUCE framework that combines advanced audio processing with interactive entertainment. The core concept is to entertain users with **7 mini-games** during the 60-80 second audio separation processing powered by **Demucs AI**.

## Key Features
- **AI-Powered Audio Separation**: Demucs C++ CLI integration for professional audio source separation
- **Interactive Gaming**: 7 full-featured mini-games (Snake, Tetris-style Block Puzzle, Flappy Bird, Breakout, Space Shooter, 2048, Dummy Game)
- **Native Performance**: C++17 with AVX2 SIMD optimizations and Intel IPP support
- **Commercial-Grade Architecture**: Service-based design with event bus communication

## Target Use Case
Commercial audio processing tool that transforms the traditionally boring waiting period during audio separation into an engaging gaming experience. Perfect for music producers, audio engineers, and content creators who need to separate audio tracks.

## Technical Innovation
- **Game-Driven Processing**: Mini-games adapt difficulty/speed based on processing progress (0.0-1.0)
- **Asset Management**: 66+ embedded game sprites as CMake binary resources
- **Cross-Platform**: JUCE framework ensures Windows, macOS, and Linux compatibility
- **Memory Efficient**: Games designed to run alongside intensive audio processing without interference

## Business Model
Commercial desktop application with professional audio processing capabilities enhanced by gamification to improve user experience during lengthy processing operations.