#!/usr/bin/env python3
"""
Test Python script in EXACT same environment as the C++ app
"""

import os
import sys
import subprocess
import tempfile
import time

def test_exact_app_environment():
    print("🔍 Testing Python Script in EXACT App Environment")
    print("=" * 60)
    
    # Set working directory to match the app
    app_dir = r"B:\SANTI\SAMPLE_MONSTA\sample_monsta4\Sample_monsta5\build\SM5App_artefacts\Release"
    if not os.path.exists(app_dir):
        print(f"❌ App directory not found: {app_dir}")
        return False
    
    print(f"📁 Changing to app directory: {app_dir}")
    os.chdir(app_dir)
    
    # Use exact same paths as the app
    script_path = r"B:\SANTI\SAMPLE_MONSTA\sample_monsta4\Sample_monsta5\build\SM5App_artefacts\Release\python_bridge\demucs_separator.py"
    input_file = r"D:\Cd's\12 Discipulos\01-Track-01.mp3"
    
    # Create temp directory with same naming pattern as app
    temp_dir = tempfile.mkdtemp(prefix="fullSong_")
    progress_file = os.path.join(temp_dir, "progress.json")
    result_file = os.path.join(temp_dir, "result.json")
    
    print(f"📁 Temp directory: {temp_dir}")
    print(f"📄 Script path: {script_path}")
    print(f"🎵 Input file: {input_file}")
    print(f"📊 Progress file: {progress_file}")
    print(f"📋 Result file: {result_file}")
    
    # Build exact same command as the app
    cmd = [
        'python',
        script_path,
        input_file,
        temp_dir,
        '--model', 'htdemucs',
        '--progress-file', progress_file,
        '--result-file', result_file
    ]
    
    print(f"\n🚀 Running exact app command:")
    for i, arg in enumerate(cmd):
        print(f"  arg[{i}]: '{arg}'")
    
    print(f"\n⚙️ Environment:")
    print(f"  Working directory: {os.getcwd()}")
    print(f"  Python executable: {sys.executable}")
    print(f"  Python version: {sys.version}")
    print(f"  PATH: {os.environ.get('PATH', 'NOT_SET')[:200]}...")
    
    # Check file existence like the app does
    print(f"\n🔍 File checks:")
    print(f"  Script exists: {os.path.exists(script_path)}")
    print(f"  Input exists: {os.path.exists(input_file)}")
    print(f"  Temp dir exists: {os.path.exists(temp_dir)}")
    
    print(f"\n⏳ Starting process...")
    start_time = time.time()
    
    try:
        # Use same process creation as JUCE ChildProcess
        process = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=0,  # Unbuffered like JUCE
            cwd=app_dir   # Same working directory as app
        )
        
        print(f"✅ Process started with PID: {process.pid}")
        
        # Monitor output in real-time like the app
        stdout_lines = []
        stderr_lines = []
        
        while process.poll() is None:
            # Read stdout
            if process.stdout:
                line = process.stdout.readline()
                if line:
                    line = line.strip()
                    stdout_lines.append(line)
                    print(f"[STDOUT] {line}")
            
            # Read stderr  
            if process.stderr:
                line = process.stderr.readline()
                if line:
                    line = line.strip()
                    stderr_lines.append(line)
                    print(f"[STDERR] {line}")
            
            time.sleep(0.1)
        
        # Get final output
        final_stdout, final_stderr = process.communicate()
        if final_stdout:
            for line in final_stdout.strip().split('\n'):
                if line:
                    stdout_lines.append(line)
                    print(f"[STDOUT] {line}")
        if final_stderr:
            for line in final_stderr.strip().split('\n'):
                if line:
                    stderr_lines.append(line)
                    print(f"[STDERR] {line}")
        
        elapsed = time.time() - start_time
        exit_code = process.returncode
        
        print(f"\n📊 RESULTS:")
        print(f"  Exit code: {exit_code}")
        print(f"  Duration: {elapsed:.2f} seconds")
        print(f"  STDOUT lines: {len(stdout_lines)}")
        print(f"  STDERR lines: {len(stderr_lines)}")
        
        # Check result file
        if os.path.exists(result_file):
            print(f"  Result file: EXISTS")
            with open(result_file, 'r') as f:
                result_content = f.read()
                print(f"  Result content: {result_content[:200]}...")
        else:
            print(f"  Result file: NOT FOUND")
        
        # Check progress file
        if os.path.exists(progress_file):
            print(f"  Progress file: EXISTS")
            with open(progress_file, 'r') as f:
                progress_content = f.read()
                print(f"  Progress content: {progress_content}")
        else:
            print(f"  Progress file: NOT FOUND")
        
        # List temp directory contents
        print(f"\n📁 Temp directory contents:")
        try:
            for item in os.listdir(temp_dir):
                item_path = os.path.join(temp_dir, item)
                size = os.path.getsize(item_path) if os.path.isfile(item_path) else "DIR"
                print(f"  {item} ({size} bytes)")
        except Exception as e:
            print(f"  Error listing directory: {e}")
        
        if exit_code == 0:
            print(f"\n✅ SUCCESS: Python script completed successfully!")
            return True
        else:
            print(f"\n❌ FAILURE: Python script failed with exit code {exit_code}")
            if stderr_lines:
                print(f"Error details:")
                for line in stderr_lines[-10:]:  # Last 10 error lines
                    print(f"  {line}")
            return False
            
    except Exception as e:
        print(f"❌ Exception running process: {e}")
        return False
    finally:
        # Cleanup
        try:
            import shutil
            shutil.rmtree(temp_dir)
            print(f"🧹 Cleaned up temp directory")
        except:
            pass

if __name__ == "__main__":
    success = test_exact_app_environment()
    print(f"\n{'='*60}")
    if success:
        print("🎉 TEST PASSED: Python works in app environment!")
    else:
        print("💥 TEST FAILED: Python fails in app environment!")
    print("="*60)
