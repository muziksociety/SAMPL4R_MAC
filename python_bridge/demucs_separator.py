#!/usr/bin/env python3
"""
Demucs Audio Source Separation Bridge for SampleMonsta5

This script provides a Python bridge for Demucs audio source separation
when the C++ implementation is not available (due to BLAS dependencies).

Usage:
    python demucs_separator.py <input_file> <output_dir> [--model htdemucs] [--progress-file progress.txt]
"""

import sys
import os
import argparse
import json
import time
import signal
import threading
from pathlib import Path

# Set reasonable threading limits like UVR would in a GUI environment
# UVR doesn't set these globally but we need some control in C++ app context
os.environ['OMP_NUM_THREADS'] = '4'  # Allow 4 threads like UVR
os.environ['MKL_NUM_THREADS'] = '4'
os.environ['NUMEXPR_NUM_THREADS'] = '4'

try:
    import torch
    import torchaudio
    from demucs import pretrained
    from demucs.apply import apply_model
    from demucs.audio import save_audio

    # GPU Detection and Setup - EXACTLY like UVR
    def setup_device(force_cpu=False):
        """Setup device exactly like UVR does"""
        if force_cpu:
            print("CPU processing forced by user")
            return 'cpu'

        # Check for CUDA availability
        cuda_available = torch.cuda.is_available()

        # Check for MPS (Apple Silicon) availability
        mps_available = hasattr(torch.backends, 'mps') and torch.backends.mps.is_available()

        if cuda_available:
            device = 'cuda'
            gpu_name = torch.cuda.get_device_name()
            gpu_memory = torch.cuda.get_device_properties(0).total_memory / 1024**3
            print(f"GPU detected: CUDA - {gpu_name}")
            print(f"CUDA memory: {gpu_memory:.1f} GB")

            # Check if GPU has enough memory (like UVR does)
            if gpu_memory < 4.0:
                print("WARNING: GPU has less than 4GB memory, performance may be limited")
            elif gpu_memory >= 8.0:
                print("GPU has sufficient memory for optimal performance")
        elif mps_available:
            device = 'mps'
            print("GPU detected: Apple MPS")
        else:
            device = 'cpu'
            print("No GPU detected, using CPU")

        return device

    # Device will be set up in main() based on command line args
    DEVICE = None

    # Set PyTorch threading based on device like UVR
    if DEVICE == 'cpu':
        # CPU: Use more threads for better performance
        torch.set_num_threads(4)
        torch.set_num_interop_threads(1)
    else:
        # GPU: Use fewer CPU threads to avoid conflicts
        torch.set_num_threads(2)
        torch.set_num_interop_threads(1)

    DEMUCS_AVAILABLE = True
except ImportError as e:
    DEMUCS_AVAILABLE = False
    IMPORT_ERROR = str(e)

def write_progress(progress_file, percent, message):
    """Write progress to file for C++ to read"""
    if progress_file:
        try:
            with open(progress_file, 'w') as f:
                json.dump({
                    'percent': percent,
                    'message': message,
                    'timestamp': time.time()
                }, f)
        except Exception:
            pass  # Ignore progress file errors

def separate_audio(input_file, output_dir, model_name='htdemucs', progress_file=None, force_cpu=False):
    """
    Separate audio using Demucs with enhanced error handling for C++ app integration
    """
    """
    Separate audio using Demucs
    
    Args:
        input_file: Path to input audio file
        output_dir: Directory to save separated stems
        model_name: Demucs model to use (default: htdemucs)
        progress_file: Optional file to write progress updates
        
    Returns:
        dict: Result with success status and file paths
    """
    
    if not DEMUCS_AVAILABLE:
        return {
            'success': False,
            'error': f'Demucs not available: {IMPORT_ERROR}',
            'drum_stem': None
        }
    
    try:
        # Enhanced error handling for C++ app integration
        print(f"DEBUG: Starting separation with input: {input_file}")
        print(f"DEBUG: Output directory: {output_dir}")
        print(f"DEBUG: Model: {model_name}")
        sys.stdout.flush()

        # Setup device based on user preference
        device = setup_device(force_cpu)

        write_progress(progress_file, 5, "Loading Demucs model...")
        
        # Load the pretrained model and move to device EXACTLY like UVR does
        print(f"Loading Demucs model: {model_name}")
        model = pretrained.get_model(model_name)

        # Move model to device like UVR
        print(f"Moving model to device: {device}")
        model.to(device)
        model.eval()

        # Clear GPU cache if using GPU (like UVR does)
        if device != 'cpu':
            if device == 'cuda':
                torch.cuda.empty_cache()
            elif device == 'mps':
                torch.mps.empty_cache()
        
        write_progress(progress_file, 15, "Loading audio file...")
        
        # Load audio and move to device like UVR
        wav, sr = torchaudio.load(input_file)

        # Ensure stereo
        if wav.shape[0] == 1:
            wav = wav.repeat(2, 1)
        elif wav.shape[0] > 2:
            wav = wav[:2]  # Take first two channels

        # Resample if necessary
        if sr != model.samplerate:
            resampler = torchaudio.transforms.Resample(sr, model.samplerate)
            wav = resampler(wav)
            sr = model.samplerate

        # Move audio to device like UVR does
        wav = wav.to(device)
            
        write_progress(progress_file, 25, "Running source separation...")
        
        # Use EXACT UVR parameters - PROVEN TO WORK!
        # These parameters successfully processed the 4.5 minute file in 9.3 minutes
        segment = 7.8    # UVR segment size - EXACT
        overlap = 0.25   # UVR overlap - EXACT
        shifts = 2       # UVR shifts - EXACT
        split = True     # UVR split - EXACT

        write_progress(progress_file, 30, "Starting audio separation...")

        # Force garbage collection before heavy processing
        import gc
        gc.collect()

        # No debug prints - run exactly like UVR

        with torch.no_grad():
            # Direct apply_model call EXACTLY like UVR - no timeout, no threading wrapper
            write_progress(progress_file, 35, "Running neural network inference...")
            start_time = time.time()

            # EXACT UVR apply_model call with ALL parameters from Demucs 4.0.1
            sources = apply_model(
                model,
                wav[None],           # UVR: mix_infer[None]
                shifts=shifts,       # UVR: self.shifts (2)
                split=split,         # UVR: self.is_split_mode (True)
                overlap=overlap,     # UVR: self.overlap (0.25)
                segment=segment,     # UVR segmentation for long files (7.8)
                progress=False,      # Disable progress callbacks
                device=device,       # Use detected device (GPU or CPU)
                num_workers=0        # UVR uses 0 workers - EXACT
            )[0]                     # UVR: [0] to get first element

            elapsed = time.time() - start_time
            print(f"DEBUG: apply_model completed in {elapsed:.2f} seconds")
            sys.stdout.flush()

            # Clear GPU cache after processing like UVR does
            if device != 'cpu':
                if device == 'cuda':
                    torch.cuda.empty_cache()
                elif device == 'mps':
                    torch.mps.empty_cache()

        # Move sources back to CPU for saving like UVR does
        sources = sources.cpu()

        # sources shape: [batch, sources, channels, time]
        sources = sources.squeeze(0)  # Remove batch dimension
        
        write_progress(progress_file, 70, "Saving separated stems...")
        
        # Create output directory with enhanced error handling
        try:
            os.makedirs(output_dir, exist_ok=True)
            print(f"DEBUG: Created output directory: {output_dir}")
        except Exception as e:
            print(f"ERROR: Failed to create output directory: {e}")
            raise
        
        # Save all stems
        stem_names = model.sources
        stem_files = {}
        
        for i, stem_name in enumerate(stem_names):
            stem_file = os.path.join(output_dir, f"{stem_name}.wav")
            try:
                print(f"DEBUG: Saving {stem_name} stem to: {stem_file}")
                save_audio(sources[i], stem_file, sr)
                stem_files[stem_name] = stem_file
                print(f"DEBUG: Successfully saved {stem_name} stem")
            except Exception as e:
                print(f"ERROR: Failed to save {stem_name} stem: {e}")
                raise
            
        write_progress(progress_file, 90, "Separation complete")
        
        # Return the drum stem specifically
        drum_stem = stem_files.get('drums', None)
        if not drum_stem:
            return {
                'success': False,
                'error': 'Drum stem not found in separation results',
                'drum_stem': None,
                'all_stems': stem_files
            }
            
        write_progress(progress_file, 100, "Done")

        print("DEBUG: All stems saved successfully")
        print(f"DEBUG: Drum stem path: {drum_stem}")
        sys.stdout.flush()

        return {
            'success': True,
            'error': None,
            'drum_stem': drum_stem,
            'all_stems': stem_files
        }
        
    except Exception as e:
        error_msg = f"Demucs separation failed: {str(e)}"
        print(f"ERROR: {error_msg}")
        print(f"ERROR: Exception type: {type(e).__name__}")
        import traceback
        print(f"ERROR: Traceback: {traceback.format_exc()}")
        sys.stdout.flush()

        write_progress(progress_file, 0, error_msg)
        return {
            'success': False,
            'error': error_msg,
            'drum_stem': None
        }

def main():
    parser = argparse.ArgumentParser(description='Demucs Audio Source Separation')
    parser.add_argument('input_file', help='Input audio file path')
    parser.add_argument('output_dir', help='Output directory for separated stems')
    parser.add_argument('--model', default='htdemucs', help='Demucs model name (default: htdemucs)')
    parser.add_argument('--progress-file', help='File to write progress updates')
    parser.add_argument('--result-file', help='File to write final result JSON')
    parser.add_argument('--cpu', action='store_true', help='Force CPU processing (like UVR CPU option)')
    
    args = parser.parse_args()
    
    # Check if input file exists
    if not os.path.exists(args.input_file):
        result = {
            'success': False,
            'error': f'Input file not found: {args.input_file}',
            'drum_stem': None
        }
    else:
        # Run separation
        result = separate_audio(
            args.input_file,
            args.output_dir,
            args.model,
            args.progress_file,
            args.cpu
        )
    
    # Write result to file if specified
    if args.result_file:
        try:
            print(f"DEBUG: Writing result to file: {args.result_file}")
            with open(args.result_file, 'w') as f:
                json.dump(result, f, indent=2)
            print(f"DEBUG: Result file written successfully")
        except Exception as e:
            print(f"ERROR: Failed to write result file: {e}", file=sys.stderr)
            print(f"DEBUG: Exception type: {type(e).__name__}")
            import traceback
            print(f"DEBUG: Traceback: {traceback.format_exc()}")

    # Print result to stdout
    print(json.dumps(result, indent=2))

    # Debug the exit decision
    print(f"DEBUG: result['success'] = {result['success']}")
    print(f"DEBUG: type(result['success']) = {type(result['success'])}")
    exit_code = 0 if result['success'] else 1
    print(f"DEBUG: Calculated exit code = {exit_code}")

    # Exit with appropriate code
    sys.exit(exit_code)

if __name__ == '__main__':
    main()
