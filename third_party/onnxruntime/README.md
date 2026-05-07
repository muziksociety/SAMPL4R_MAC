# ONNX Runtime GPU Backend for Sample Monsta 5

This directory contains the ONNX Runtime files needed for GPU-accelerated audio separation.

## Performance

- **GPU (ONNX + CUDA)**: ~2-3 minutes for a 5-minute song
- **CPU (demucs.cpp)**: ~25-30 minutes for a 5-minute song
- **Speedup**: 10-15x faster with GPU

## Required Files

Place the following files in this directory structure:

```
third_party/onnxruntime/
├── bin/
│   ├── demucs_onnx.exe           (~5MB)   - ONNX CLI binary
│   ├── onnxruntime.dll           (~50MB)  - ONNX Runtime core
│   ├── onnxruntime_providers_shared.dll   - Shared provider support
│   └── onnxruntime_providers_cuda.dll (~15MB) - CUDA execution provider
└── models/
    └── htdemucs.ort              (~300MB) - HTDemucs model in ORT format
```

## How to Obtain These Files

### Option 1: Build from Source (Recommended)

1. Clone demucs.onnx: `git clone https://github.com/sevagh/demucs.onnx`
2. Build with ONNX Runtime CUDA support (see demucs.onnx README)
3. Convert HTDemucs model to .ort format using ONNX tools

### Option 2: Pre-built Release (When Available)

Check the Sample Monsta releases page for pre-built GPU packages.

## Requirements for End Users

End users only need:
- **NVIDIA GPU** (any CUDA-capable GPU)
- **NVIDIA drivers** installed (provides nvcuda.dll automatically)

No separate CUDA toolkit installation is required - the ONNX Runtime DLLs include everything needed.

## Troubleshooting

If GPU acceleration is not working, check the SM5 debug console for messages like:
```
[GPU] ONNX CLI available: NO
[GPU] Missing: demucs_onnx.exe
```

The application will automatically fall back to CPU (demucs.cpp) if GPU is not available.

## DirectML Note

DirectML (AMD/Intel GPUs) is currently NOT supported due to a known issue with the HTDemucs model architecture. Only NVIDIA CUDA is supported.
