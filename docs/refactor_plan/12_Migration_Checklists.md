# 12 – Migration Checklists (Actionable)

Requires: all prior modules
Provides: step-by-step tasks to complete the port.

Core (Week 1–2)
- [ ] Create CMake skeleton with JUCE; App/MainWindow skeleton.
- [ ] Implement ServiceRegistry and EventBus; sample log events.
- [ ] Style/LookAndFeel and splash/loading.

UI Pages (Week 2–3)
- [ ] InitialPage: welcome, recent, system checks, loading animation.
- [ ] ProcessingPage: task cards (separation, offline FX), queue, progress.
- [ ] SampleBrowserPage: folder tree, list/grid, context menus.

Audio (Week 3–5)
- [ ] Engine: device init, callback, graph.
- [ ] Nodes: FileSource, Gain, EQ, Compressor, Reverb, Delay.
- [ ] Params: smoothing and UI bindings.

Wave Editor (Week 5–7)
- [ ] WaveformView with cached drawing and zoom/scroll.
- [ ] TransportBar with loop/scrub; keyboard shortcuts.
- [ ] EffectsPanel wired to nodes.
- [ ] ImmutableAudio cache and background decode.
- [ ] Export manager with progress and cancel.

Plugins (Week 5–8)
- [ ] Browser page and file ops.
- [ ] Frequency analyzer (FFT) and filters.
- [ ] Licensing pages/services.

Python IPC (parallel)
- [ ] SeparationClientService (ZeroMQ/gRPC) + progress UI.
- [ ] Python service packaging and version handshake.

QA
- [ ] Golden audio tests vs SM4 for core effects.
- [ ] Crash/latency audits; startup under 500ms UI visible.
