#!/usr/bin/env python3
"""
Test GPU processing with the updated script
"""

import os
import sys
import subprocess
import tempfile
import time

def test_gpu_processing():
    print("Testing GPU processing...")
    
    # Use the same file but test GPU vs CPU
    input_file = r"D:\Cd's\12 Discipulos\01-Track-01.mp3"
    
    with tempfile.TemporaryDirectory() as temp_dir:
        script_path = r"build\SM5App_artefacts\Release\python_bridge\demucs_separator.py"
        progress_file = os.path.join(temp_dir, "progress.json")
        result_file = os.path.join(temp_dir, "result.json")
        
        # Test GPU processing
        print("\n=== Testing GPU Processing ===")
        cmd_gpu = [
            'python', script_path,
            input_file, temp_dir,
            '--model', 'htdemucs',
            '--progress-file', progress_file,
            '--result-file', result_file
        ]
        
        print(f"Running: {' '.join(cmd_gpu)}")
        start_time = time.time()
        
        try:
            process = subprocess.run(cmd_gpu, capture_output=True, text=True, timeout=600)
            elapsed = time.time() - start_time
            
            print(f"GPU processing completed in {elapsed:.2f} seconds")
            print(f"Return code: {process.returncode}")
            print(f"STDOUT: {process.stdout}")
            if process.stderr:
                print(f"STDERR: {process.stderr}")
                
            if process.returncode == 0:
                print("✅ GPU processing: SUCCESS")
                return True
            else:
                print("❌ GPU processing: FAILED")
                return False
                
        except subprocess.TimeoutExpired:
            print("❌ GPU processing: TIMEOUT")
            return False

if __name__ == "__main__":
    test_gpu_processing()
