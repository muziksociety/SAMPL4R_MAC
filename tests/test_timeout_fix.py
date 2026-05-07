#!/usr/bin/env python3
"""
Quick test of the timeout fix
"""
import subprocess
import tempfile
import os
import time

def test_timeout_fix():
    print("Testing timeout fix...")
    
    script_path = r"B:\SANTI\SAMPLE_MONSTA\sample_monsta4\Sample_monsta5\build\SM5App_artefacts\Release\python_bridge\demucs_separator.py"
    input_file = r"D:\Cd's\12 Discipulos\01-Track-01.mp3"
    
    if not os.path.exists(input_file):
        print("No test file available")
        return False
    
    with tempfile.TemporaryDirectory() as temp_dir:
        progress_file = os.path.join(temp_dir, "progress.json")
        result_file = os.path.join(temp_dir, "result.json")
        
        cmd = [
            "python",
            script_path,
            input_file,
            temp_dir,
            "--model", "htdemucs",
            "--progress-file", progress_file,
            "--result-file", result_file
        ]
        
        print(f"Running: {' '.join(cmd)}")
        
        start_time = time.time()
        process = subprocess.run(cmd, capture_output=True, text=True, timeout=600)  # 10 minutes
        elapsed = time.time() - start_time
        
        print(f"Process completed in {elapsed:.2f} seconds")
        print(f"Return code: {process.returncode}")
        print(f"STDOUT: {process.stdout}")
        if process.stderr:
            print(f"STDERR: {process.stderr}")
        
        return process.returncode == 0

if __name__ == "__main__":
    success = test_timeout_fix()
    print(f"Test result: {'SUCCESS' if success else 'FAILED'}")
