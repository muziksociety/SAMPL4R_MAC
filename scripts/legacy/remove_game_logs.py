#!/usr/bin/env python3
"""Remove all juce::Logger::writeToLog statements from game files"""

import re
import os

game_files = [
    "src/ui/Games/SnakeGame.cpp",
    "src/ui/Games/BlockPuzzleGame.cpp",
    "src/ui/Games/FlappyGame.cpp",
    "src/ui/Games/MiniGameHost.cpp",
    "src/ui/Games/Game2048.cpp",
    "src/ui/Games/BreakoutGame.cpp",
    "src/ui/Games/SpaceShooterGame.cpp",
    "src/ui/Games/DummyGame.cpp",
]

def remove_log_lines(filepath):
    """Remove all juce::Logger::writeToLog lines from a file"""
    if not os.path.exists(filepath):
        print(f"Skipping {filepath} (not found)")
        return 0
    
    with open(filepath, 'r', encoding='utf-8') as f:
        lines = f.readlines()
    
    new_lines = []
    removed_count = 0
    
    for line in lines:
        # Skip lines that contain juce::Logger::writeToLog
        if 'juce::Logger::writeToLog' in line:
            removed_count += 1
            continue
        new_lines.append(line)
    
    if removed_count > 0:
        with open(filepath, 'w', encoding='utf-8') as f:
            f.writelines(new_lines)
        print(f"✓ {filepath}: Removed {removed_count} log lines")
    else:
        print(f"  {filepath}: No log lines found")
    
    return removed_count

def main():
    total_removed = 0
    for filepath in game_files:
        count = remove_log_lines(filepath)
        total_removed += count
    
    print(f"\n✅ Total: Removed {total_removed} log lines from game files")

if __name__ == "__main__":
    main()

