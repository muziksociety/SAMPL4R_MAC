#!/usr/bin/env python3
"""Replace all emojis with ASCII equivalents in SM5 codebase"""

import os
import glob

# Emoji replacement map
REPLACEMENTS = {
    '🔥': '[TRACE]',
    '🚨': '[CRASH]',
    '💥': '[ERROR]',
    '📍': '[ADDR]',
    '🎵': '[AUDIO]',
    '🔌': '[DEVICE]',
    '✅': '[OK]',
    '🔍': '[SCAN]',
    '🔬': '[ANALYZE]',
    '🎯': '[TARGET]',
    '🛡️': '[SHIELD]',
    '🔊': '[VOLUME]',
    '🎨': '[CREATIVE]',
    '⚠️': '[WARNING]',
    '⚡': '[FAST]',
    '❌': '[FAILED]',
    '🐍': '[PYTHON]',
    '🎬': '[ANIM]',
    '🥁': '[DRUM]',
    '🎧': '[PLAY]',
    '📊': '[CHART]',
    '🔄': '[REFRESH]',
    'ℹ️': '[INFO]',
    '📡': '[SIGNAL]',
    '🔧': '[TOOL]',
    '📁': '[FOLDER]',
    '💾': '[SAVE]',
    '🎛️': '[CONTROL]',
    '🔑': '[KEY]',
    '🧹': '[CLEAN]',
    '🏠': '[HOME]',
}

def replace_emojis_in_file(filepath):
    """Replace emojis in a single file"""
    try:
        with open(filepath, 'r', encoding='utf-8') as f:
            content = f.read()
        
        original = content
        for emoji, replacement in REPLACEMENTS.items():
            content = content.replace(emoji, replacement)
        
        if content != original:
            with open(filepath, 'w', encoding='utf-8', newline='') as f:
                f.write(content)
            print(f"✓ Replaced emojis in: {filepath}")
            return True
        return False
    except Exception as e:
        print(f"✗ Error processing {filepath}: {e}")
        return False

def main():
    """Main function"""
    src_dir = 'src'
    patterns = ['**/*.cpp', '**/*.h']
    
    total_files = 0
    modified_files = 0
    
    for pattern in patterns:
        for filepath in glob.glob(os.path.join(src_dir, pattern), recursive=True):
            total_files += 1
            if replace_emojis_in_file(filepath):
                modified_files += 1
    
    print(f"\nProcessed {total_files} files, modified {modified_files} files")

if __name__ == '__main__':
    main()

