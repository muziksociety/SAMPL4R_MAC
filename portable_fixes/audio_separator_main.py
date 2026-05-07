#!/usr/bin/env python
"""
Entry point for running audio_separator as a module: python -m audio_separator

This file fixes the issue where audio_separator package cannot be run with
`python -m audio_separator` because it's missing __main__.py.

Copy this file to:
  <python>/Lib/site-packages/audio_separator/__main__.py
"""
from audio_separator.utils.cli import main

if __name__ == "__main__":
    main()
