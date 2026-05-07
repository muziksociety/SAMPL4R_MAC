# Python ML in a JUCE/C++ App (Demucs and friends)

## Position
- Recommended: keep Demucs and other Python‑heavy models in a separate Python process and integrate via IPC while the JUCE/C++ app handles UI, audio, and orchestration.
- Rationale: removes embedding/packaging friction, isolates crashes, and lets you ship now. You can later migrate selected models to ONNX/LibTorch without blocking.

## Integration Options
- Out‑of‑process IPC (preferred)
  - Transport: ZeroMQ (simple), gRPC (IDL + tooling), or localhost TCP (custom protocol).
  - Data: control as JSON/protobuf; bulk audio via temp files or shared memory for large stems.
  - Lifecycle: spawn on demand; health checks; version handshake; graceful shutdown.

- In‑process Python embedding (not recommended initially)
  - Pros: single process; direct numpy access.
  - Cons: GIL contention, packaging complexity (Windows), higher crash surface.

- Native inference
  - ONNX Runtime: export ONNX; run CPU/CUDA/DML. Good balance of perf + deployability.
  - LibTorch (TorchScript): maximum fidelity; heavier toolchain and CUDA coupling.
  - TensorRT: best NVIDIA perf; conversion complexity, vendor lock‑in.

## Proposed IPC Contract
- Request (JSON/protobuf)
```
{
  "op": "separate",
  "id": "uuid",
  "model": "demucs_v4",
  "input": {
    "path": "C:/…/mix.wav"
  },
  "options": {
    "stems": ["vocals", "drums", "bass", "other"],
    "overlap": 0.25,
    "shifts": 1,
    "device": "auto"
  }
}
```
- Progress event
```
{ "id": "uuid", "type": "progress", "pct": 42.0, "msg": "chunk 5/12" }
```
- Result
```
{
  "id": "uuid",
  "type": "result",
  "stems": {
    "vocals": "C:/…/mix_vocals.wav",
    "drums":  "C:/…/mix_drums.wav",
    "bass":   "C:/…/mix_bass.wav",
    "other":  "C:/…/mix_other.wav"
  },
  "metrics": { "rtf": 3.7 }
}
```
- Error
```
{ "id": "uuid", "type": "error", "code": "MODEL_LOAD", "msg": "CUDA not available" }
```

## JUCE/C++ Client Sketch
```cpp
class SeparationClientService : public samplar::IService {
public:
  using Progress = std::function<void(float, juce::String)>;
  struct Options { juce::String model; std::vector<juce::String> stems; int shifts{1}; float overlap{0.25f}; };

  void initialise() override; void shutdown() override;

  std::future<juce::Result> requestSeparation(const juce::File& input,
                                              const Options& opt,
                                              Progress onProgress);
};
```
- Implementation: background thread connects to Python server, sends request, pumps progress to EventBus on the message thread, resolves future when done.

## Python Service Sketch (Demucs)
```python
from demucs import pretrained, apply
from zmq import Context

# recv request -> run -> send progress -> send result paths
```
- Concurrency: one worker per request or a small pool; GPU serialization if needed.
- Environment: ship venv alongside Python service; pin versions.

## Packaging on Windows
- Distribute the JUCE app and a `python_service/` folder with:
  - Embedded venv (or Conda env) containing Demucs and deps.
  - `service.exe` (PyInstaller) or `python.exe service.py` runner.
  - Model weights in a cache folder.
- App spawns service with `--port` and `--model-dir`; uses a PID file/healthcheck.

## Performance and UX
- Treat separation as offline/batch: show determinate progress, allow cancel, and write stems to disk.
- For short files, you can stream memory buffers to the service, but files are simpler and robust.
- Consider a CPU‑only fallback (ORT CPU or PyTorch CPU) when GPU is unavailable.

## Migration to Native Inference (optional)
- ONNX path: validate an ONNX export producing bit‑for‑bit (or close) parity on a test corpus; handle unsupported ops with custom kernels if needed.
- LibTorch path: TorchScript export, ensure contiguous tensors and channel ordering; profile and tune with intraop threads.

## Risks and Mitigations
- Model drift: pin model and dep versions; include hash checks.
- IPC reliability: retries, timeouts, back‑pressure; write partial results atomically.
- GPU variability: feature detect at startup; surface device selection in UI.

## Conclusion
- Yes, keeping Demucs/ML in Python via IPC is the most pragmatic step now. It complements a JUCE/C++ core, ships faster, and leaves a path to native inference later without blocking UI/audio work.

