# MEMORY.md - MonstaForge's Long-Term Memory

## Project Status

SAMPLAR is a C++/JUCE audio application for drum sample processing and variation generation.

## Architecture

- **BIBLE Framework**: EventBus, ServiceRegistry, IService pattern
- **Variation Engines**: 8 different engines for sample processing
- **Build System**: CMake with Visual Studio 2022
- **License System**: Server-based licensing

## Key Files

- `src/app/` - Core framework (EventBus, ServiceRegistry)
- `src/services/` - Business logic 
- `src/services/variation/engines/` - All variation engines
- `src/ui/` - JUCE components
- `CLAUDE.md` - Full development guide

## Recent Context

*Agent just initialized - awaiting first tasks from Santi/Lira*

## Lessons Learned

See `mistakes.md` for documented failures and prevention strategies.

## Important Patterns

1. **Evidence Gate**: No planning without verified code evidence
2. **Logging Gate**: All implementations must include comprehensive logging
3. **Log Evidence Gate**: Bug fixes require actual console log evidence
