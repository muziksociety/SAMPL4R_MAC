# 02b – Event Contracts (Message Names and Payloads)

Requires: `Sample_monsta5/docs/refactor_plan/02_EventBus_ServiceRegistry.md`
Used by: all modules when porting SM4 event flows.

Principles
- Keep event names stable to ease parity testing.
- Use plain structs in C++ with JSON (for persistence/IPC) mirroring SM4 payloads.
- Declare producer/consumer for each event and required thread (UI/audio/bg).

Core Contracts (initial set)
- `ui.register_page { name:string, component:Component*, displayName:string, order:int }` (UI thread)
- `ui.navigate name:string` (UI thread)
- `ui.register_widget_factory { name:string, factory:function }` (UI thread)

Wave Editor
- `wave_editor.open { file:File, index:int }` (UI)
- `wave_editor.apply_preset { name:string }` (UI)
- `wave_editor.export { path:File, format:string }` (bg)
- `wave_editor.audio_loaded { samples:int, sr:int, ch:int }` (UI)

Context Menu
- `context_menu.action.sample_edit_requested { widget:any, sample_index:int, sample_path:string }` (UI)

Separation (IPC)
- `separation.request { id:uuid, file:File, model:string, options:json }` (bg)
- `separation.progress { id:uuid, pct:float, msg:string }` (UI)
- `separation.result { id:uuid, stems:map, metrics:json }` (UI)
- `separation.error { id:uuid, code:string, msg:string }` (UI)

Licensing
- `licensing.activate { key:string }` (bg)
- `licensing.status { state:string, expiry:datetime }` (UI)

Notes
- Expand this document as you port each plugin. Link back to producing/consuming services and source Python files for reference.

