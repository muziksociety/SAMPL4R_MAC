#!/usr/bin/env python3
"""
Debug the Python script to see the exact error
"""

import os
import sys
import subprocess
import tempfile

def test_debug_python():
    print("🔍 Debugging Python Demucs Script")
    print("=" * 50)
    
    # Test with the exact same parameters the app uses
    input_file = r"D:\Cd's\12 Discipulos\01-Track-01.mp3"
    script_path = r"build\SM5App_artefacts\Release\python_bridge\demucs_separator.py"
    
    # Create a temp directory similar to what the app creates
    with tempfile.TemporaryDirectory(prefix="fullSong_") as temp_dir:
        progress_file = os.path.join(temp_dir, "progress.json")
        result_file = os.path.join(temp_dir, "result.json")
        
        cmd = [
            'python', script_path,
            input_file, temp_dir,
            '--model', 'htdemucs',
            '--progress-file', progress_file,
            '--result-file', result_file
        ]
        
        print(f"Command: {' '.join(cmd)}")
        print(f"Temp dir: {temp_dir}")
        print("Running...")
        
        try:
            # Run with real-time output
            process = subprocess.Popen(
                cmd, 
                stdout=subprocess.PIPE, 
                stderr=subprocess.PIPE, 
                text=True,
                bufsize=1,
                universal_newlines=True
            )
            
            # Read output in real-time
            while True:
                output = process.stdout.readline()
                if output == '' and process.poll() is not None:
                    break
                if output:
                    print(f"STDOUT: {output.strip()}")
            
            # Get any remaining output
            stdout, stderr = process.communicate()
            
            print(f"\nProcess finished with exit code: {process.returncode}")
            
            if stdout:
                print(f"Final STDOUT:\n{stdout}")
            if stderr:
                print(f"STDERR:\n{stderr}")
                
            # Check result file
            if os.path.exists(result_file):
                print(f"\nResult file contents:")
                with open(result_file, 'r') as f:
                    print(f.read())
            else:
                print(f"\nResult file not found: {result_file}")
                
            # Check what files were created
            print(f"\nFiles in temp directory:")
            for file in os.listdir(temp_dir):
                file_path = os.path.join(temp_dir, file)
                size = os.path.getsize(file_path) if os.path.isfile(file_path) else "DIR"
                print(f"  {file} ({size} bytes)")
                
        except Exception as e:
            print(f"Error running script: {e}")

if __name__ == "__main__":
    test_debug_python()
