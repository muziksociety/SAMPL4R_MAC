#!/usr/bin/env python3
"""
Diagnose why Python demucs stalls in the app but not in tests
"""
import os
import sys
import subprocess
import tempfile
import json
import time
import shutil
from pathlib import Path

def test_file_path_handling():
    """Test if file paths with special characters cause issues"""
    print("=== TESTING FILE PATH HANDLING ===")

    # Test the exact file path from the logs
    test_file = r"D:\Cd's\12 Discipulos\01-Track-01.mp3"
    print(f"Test file: {test_file}")
    print(f"File exists: {os.path.exists(test_file)}")

    if os.path.exists(test_file):
        print(f"File size: {os.path.getsize(test_file)} bytes")
        print(f"File readable: {os.access(test_file, os.R_OK)}")

    # Test if we can copy it to a simpler path
    if os.path.exists(test_file):
        simple_path = "test_input_copy.mp3"
        try:
            shutil.copy2(test_file, simple_path)
            print(f"Successfully copied to: {simple_path}")
            return simple_path
        except Exception as e:
            print(f"Failed to copy file: {e}")
            return None
    return None

def test_python_environment():
    """Test Python environment and imports"""
    print("\n=== TESTING PYTHON ENVIRONMENT ===")

    try:
        import torch
        print(f"PyTorch version: {torch.__version__}")
        print(f"CUDA available: {torch.cuda.is_available()}")
        if torch.cuda.is_available():
            print(f"CUDA device count: {torch.cuda.device_count()}")
    except ImportError as e:
        print(f"PyTorch import error: {e}")

    try:
        import demucs
        print(f"Demucs available: True")
        print(f"Demucs version: {demucs.__version__ if hasattr(demucs, '__version__') else 'unknown'}")
    except ImportError as e:
        print(f"Demucs import error: {e}")

def test_exact_command():
    """Test the exact command from the logs"""
    print("\n=== TESTING EXACT COMMAND (FIXED VERSION) ===")

    # Recreate the exact command from logs
    script_path = r"B:\SANTI\SAMPLE_MONSTA\sample_monsta4\Sample_monsta5\build\SM5App_artefacts\Release\python_bridge\demucs_separator.py"
    input_file = r"D:\Cd's\12 Discipulos\01-Track-01.mp3"

    print(f"Script exists: {os.path.exists(script_path)}")
    print(f"Input exists: {os.path.exists(input_file)}")

    if not os.path.exists(input_file):
        print("Using test file instead...")
        input_file = "test_input.wav"
        if not os.path.exists(input_file):
            print("No test file available, skipping command test")
            return

    # Create temp directory
    with tempfile.TemporaryDirectory() as temp_dir:
        progress_file = os.path.join(temp_dir, "progress.json")
        result_file = os.path.join(temp_dir, "result.json")

        # Set environment variables like the C++ app might
        env = os.environ.copy()
        env['OMP_NUM_THREADS'] = '4'  # Simulate what C++ app sets

        cmd = [
            "python",
            script_path,
            input_file,
            temp_dir,
            "--model", "htdemucs",
            "--progress-file", progress_file,
            "--result-file", result_file
        ]

        print(f"Command: {' '.join(cmd)}")
        print(f"Environment OMP_NUM_THREADS: {env.get('OMP_NUM_THREADS')}")

        try:
            # Start process with timeout
            process = subprocess.Popen(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                bufsize=1,
                universal_newlines=True,
                env=env  # Use modified environment
            )

            print("Process started, waiting 60 seconds for fixed version...")

            # Monitor for 60 seconds (longer timeout for fixed version)
            start_time = time.time()
            last_progress = None
            while time.time() - start_time < 60:
                if process.poll() is not None:
                    print(f"Process completed with return code: {process.returncode}")
                    stdout, stderr = process.communicate()
                    print(f"STDOUT: {stdout}")
                    if stderr:
                        print(f"STDERR: {stderr}")
                    return True  # Success!

                # Check progress file
                if os.path.exists(progress_file):
                    try:
                        with open(progress_file, 'r') as f:
                            progress = json.load(f)
                            if progress != last_progress:
                                print(f"Progress: {progress}")
                                last_progress = progress
                    except:
                        pass

                time.sleep(2)
            else:
                print("Process still running after 60 seconds - STILL STALLING!")
                process.terminate()
                time.sleep(2)
                if process.poll() is None:
                    process.kill()
                print("Process terminated")
                return False

        except Exception as e:
            print(f"Error running command: {e}")
            return False

def test_minimal_demucs():
    """Test minimal demucs separation"""
    print("\n=== TESTING MINIMAL DEMUCS ===")

    try:
        import torch
        import demucs.api

        # Test with a simple file
        test_file = "test_input.wav"
        if not os.path.exists(test_file):
            print("No test file available")
            return

        print(f"Testing with: {test_file}")

        # Try minimal separation
        separator = demucs.api.Separator(model="htdemucs")
        print("Separator created")

        # This might stall - let's see
        print("Starting separation...")
        start_time = time.time()

        try:
            origin, separated = separator.separate_audio_file(test_file)
            print(f"Separation completed in {time.time() - start_time:.2f} seconds")
            print(f"Separated tracks: {list(separated.keys())}")
        except Exception as e:
            print(f"Separation failed: {e}")

    except ImportError as e:
        print(f"Cannot test minimal demucs: {e}")

if __name__ == "__main__":
    print("DEMUCS STALL DIAGNOSIS - TESTING FIX")
    print("=" * 50)

    # Test 1: File path handling
    simple_file = test_file_path_handling()

    # Test 2: Python environment
    test_python_environment()

    # Test 3: Exact command reproduction with fix
    success = test_exact_command()

    # Test 4: Minimal demucs test
    test_minimal_demucs()

    print("\n=== DIAGNOSIS COMPLETE ===")
    if success:
        print("✅ FIX SUCCESSFUL: Python script no longer stalls!")
    else:
        print("❌ FIX FAILED: Python script still stalls")
    print("Check output above for details")
from pathlib import Path

def test_exact_command():
    """Test the exact command from the app log"""
    print("[DIAGNOSIS] Testing exact command from app log...")
    
    # Simulate the exact file path issue
    test_input = r"D:\Cd's\12 Discipulos\01-Track-01.mp3"
    print(f"[INFO] Testing with problematic path: {test_input}")
    
    # Check if this file exists (it probably doesn't)
    if not os.path.exists(test_input):
        print("[ERROR] Input file doesn't exist - this is the problem!")
        print("[INFO] App is trying to process a file that doesn't exist")
        return False
    
    print("[INFO] File exists, testing processing...")
    
    # Test with temp directory like the app does
    with tempfile.TemporaryDirectory(prefix="fullSong_") as temp_dir:
        progress_file = os.path.join(temp_dir, "progress.json")
        result_file = os.path.join(temp_dir, "result.json")
        
        cmd = [
            "python",
            "build/SM5App_artefacts/Release/python_bridge/demucs_separator.py",
            test_input,
            temp_dir,
            "--model", "htdemucs",
            "--progress-file", progress_file,
            "--result-file", result_file
        ]
        
        print(f"[EXEC] {' '.join(cmd)}")
        
        try:
            result = subprocess.run(cmd, capture_output=True, text=True, timeout=60)
            print(f"[INFO] Exit code: {result.returncode}")
            if result.stderr:
                print(f"[STDERR] {result.stderr}")
            return result.returncode == 0
        except subprocess.TimeoutExpired:
            print("[ERROR] Command timed out")
            return False
        except Exception as e:
            print(f"[ERROR] {e}")
            return False

def test_mp3_vs_wav():
    """Test if the issue is MP3 vs WAV"""
    print("[DIAGNOSIS] Testing MP3 vs WAV processing...")
    
    # Test with our known good WAV file
    wav_file = "test_input.wav"
    if not os.path.exists(wav_file):
        print("[ERROR] WAV test file doesn't exist")
        return False
    
    print("[INFO] Testing WAV file (should work)...")
    success_wav = test_file_processing(wav_file, "wav")
    
    # Look for any MP3 files to test
    mp3_files = list(Path(".").glob("*.mp3"))
    if mp3_files:
        mp3_file = str(mp3_files[0])
        print(f"[INFO] Testing MP3 file: {mp3_file}")
        success_mp3 = test_file_processing(mp3_file, "mp3")
        
        print(f"[RESULT] WAV success: {success_wav}, MP3 success: {success_mp3}")
        if success_wav and not success_mp3:
            print("[DIAGNOSIS] MP3 processing is the issue!")
            return False
    else:
        print("[INFO] No MP3 files found to test")
    
    return success_wav

def test_file_processing(input_file, file_type):
    """Test processing a specific file"""
    with tempfile.TemporaryDirectory(prefix=f"test_{file_type}_") as temp_dir:
        cmd = [
            "python", 
            "python_bridge/demucs_separator.py",
            input_file,
            temp_dir,
            "--model", "htdemucs"
        ]
        
        try:
            result = subprocess.run(cmd, capture_output=True, text=True, timeout=30)
            print(f"[INFO] {file_type.upper()} test exit code: {result.returncode}")
            if result.returncode != 0 and result.stderr:
                print(f"[STDERR] {result.stderr[:200]}...")
            return result.returncode == 0
        except subprocess.TimeoutExpired:
            print(f"[ERROR] {file_type.upper()} test timed out")
            return False
        except Exception as e:
            print(f"[ERROR] {file_type.upper()} test failed: {e}")
            return False

def test_path_quoting():
    """Test if the issue is path quoting with special characters"""
    print("[DIAGNOSIS] Testing path quoting with special characters...")
    
    # Create a test file with special characters in path
    test_dir = Path("test_special_chars")
    test_dir.mkdir(exist_ok=True)
    
    # Copy our test file to a path with special characters
    import shutil
    src_file = "test_input.wav"
    if os.path.exists(src_file):
        special_file = test_dir / "test file with spaces & special chars.wav"
        shutil.copy2(src_file, special_file)
        
        print(f"[INFO] Testing file with special path: {special_file}")
        success = test_file_processing(str(special_file), "special_path")
        
        # Cleanup
        special_file.unlink(missing_ok=True)
        test_dir.rmdir()
        
        if not success:
            print("[DIAGNOSIS] Special characters in path cause issues!")
            return False
    
    return True

def check_python_environment():
    """Check if Python environment is correct"""
    print("[DIAGNOSIS] Checking Python environment...")
    
    try:
        import torch
        import torchaudio
        from demucs import pretrained
        print("[SUCCESS] All required modules available")
        
        # Check torch configuration
        print(f"[INFO] Torch threads: {torch.get_num_threads()}")
        print(f"[INFO] Torch device: {torch.get_default_dtype()}")
        
        return True
    except ImportError as e:
        print(f"[ERROR] Missing Python dependency: {e}")
        return False

def main():
    print("[START] Diagnosing Python demucs stall issue...")
    print("=" * 60)
    
    tests = [
        ("Python environment check", check_python_environment),
        ("Path quoting test", test_path_quoting),
        ("MP3 vs WAV test", test_mp3_vs_wav),
        ("Exact command test", test_exact_command),
    ]
    
    for test_name, test_func in tests:
        print(f"\n[RUN] {test_name}")
        print("-" * 40)
        try:
            success = test_func()
            print(f"[RESULT] {test_name}: {'PASS' if success else 'FAIL'}")
        except Exception as e:
            print(f"[ERROR] {test_name} failed: {e}")

if __name__ == "__main__":
    main()