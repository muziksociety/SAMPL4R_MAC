#!/usr/bin/env python3
"""
Test script to run Demucs with EXACT UVR parameters
"""

import os
import sys
import torch
import torchaudio
from demucs import pretrained
from demucs.apply import apply_model
import time

def test_uvr_exact():
    print("Testing EXACT UVR parameters...")
    
    # Set threading like UVR would
    os.environ['OMP_NUM_THREADS'] = '4'
    os.environ['MKL_NUM_THREADS'] = '4'
    
    torch.set_num_threads(4)
    torch.set_num_interop_threads(1)
    
    input_file = r"D:\Cd's\12 Discipulos\01-Track-01.mp3"
    model_name = "htdemucs"
    
    print(f"Loading model: {model_name}")
    model = pretrained.get_model(model_name)
    model.eval()
    
    print(f"Loading audio: {input_file}")
    wav, sr = torchaudio.load(input_file)
    if sr != 44100:
        wav = torchaudio.functional.resample(wav, sr, 44100)
    
    print(f"Audio shape: {wav.shape}")
    print(f"Audio length: {wav.shape[1] / 44100:.2f} seconds")
    
    # Test different parameter combinations
    test_configs = [
        # UVR exact
        {"segment": 7.8, "overlap": 0.25, "shifts": 2, "split": True, "num_workers": 0},
        # Conservative
        {"segment": None, "overlap": 0.25, "shifts": 1, "split": False, "num_workers": 0},
        # Hybrid
        {"segment": 10.0, "overlap": 0.25, "shifts": 1, "split": True, "num_workers": 0},
    ]
    
    for i, config in enumerate(test_configs):
        print(f"\n--- Test {i+1}: {config} ---")
        try:
            start_time = time.time()
            
            with torch.no_grad():
                sources = apply_model(
                    model,
                    wav[None],
                    device='cpu',
                    progress=False,
                    **config
                )[0]
            
            elapsed = time.time() - start_time
            print(f"SUCCESS in {elapsed:.2f} seconds")
            print(f"Output shape: {sources.shape}")
            return True
            
        except Exception as e:
            print(f"FAILED: {type(e).__name__}: {str(e)}")
            continue
    
    print("All configurations failed!")
    return False

if __name__ == "__main__":
    test_uvr_exact()
