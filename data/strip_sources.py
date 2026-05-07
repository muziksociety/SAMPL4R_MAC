#!/usr/bin/env python
"""
Strip sourceFile metadata from patterns JSON files.
This removes the origin paths of extracted MIDI patterns to prevent
exposing the source file paths when the JSON is analyzed.
"""
import json
import os

def strip_source_files(filepath):
    """Remove sourceFile from all patterns in a patterns JSON file."""
    print(f"Processing: {filepath}")

    with open(filepath, 'r', encoding='utf-8') as f:
        data = json.load(f)

    count = 0
    if 'genres' in data:
        for genre_name, genre_data in data['genres'].items():
            for instrument in ['kick', 'snare', 'hihat', 'perc']:
                if instrument in genre_data and 'patterns' in genre_data[instrument]:
                    for pattern in genre_data[instrument]['patterns']:
                        if 'sourceFile' in pattern:
                            del pattern['sourceFile']
                            count += 1

    if count > 0:
        with open(filepath, 'w', encoding='utf-8') as f:
            json.dump(data, f, separators=(',', ':'))
        print(f"  Removed {count} sourceFile entries")
    else:
        print(f"  No sourceFile entries found")

    return count

def main():
    # Get the directory where this script is located
    script_dir = os.path.dirname(os.path.abspath(__file__))

    # Find all JSON files in this directory
    json_files = [f for f in os.listdir(script_dir) if f.endswith('.json')]

    total = 0
    for json_file in json_files:
        filepath = os.path.join(script_dir, json_file)
        total += strip_source_files(filepath)

    print(f"\nTotal sourceFile entries removed: {total}")

if __name__ == "__main__":
    main()
