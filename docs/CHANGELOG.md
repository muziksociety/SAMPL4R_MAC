# SAMPLAR - Changelog

All notable changes to SAMPLAR will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [2.0.0] - 2025-01-12

### 🎉 Production Release

This is the first production-ready release of SAMPLAR (formerly SAMPLAR).

### Changed
- **Rebranded** from "SAMPLAR" to "SAMPLAR"
- **Version bump** to 2.0.0 to signify production readiness
- Executable renamed from `SAMPLAR.exe` to `SAMPLAR.exe`
- Installer updated to version 2.0.0

### Added
- ✅ Complete JUCE/C++ rewrite (from Python/Kivy)
- ✅ Professional Windows installer with VC++ redistributable
- ✅ Application icon (SAMPLAR character)
- ✅ Desktop and Start Menu shortcuts
- ✅ Automatic dependency installation

### Features
- 🎵 **Sample Browser** - Browse and manage audio samples
- 🎨 **Variation Engine** - Generate creative sample variations
- 🌊 **Wave Editor** - Edit audio with effects (EQ, reverb, delay, etc.)
- 🎹 **Step Sequencer** - Create patterns and beats
- 🎤 **Vocal Separation** - Demucs-powered stem extraction
- 🎮 **Mini-Games** - Flappy Bird, Snake, Space Shooter
- 📝 **Licensing System** - Beta/prerelease license management

### Technical
- Built with JUCE 8.0.8
- C++17 codebase
- CMake build system
- Inno Setup installer
- OpenBLAS for audio processing
- Demucs integration via Python bridge

---

## [0.1.0] - 2024-12-XX

### Development Versions

- Initial JUCE/C++ port from SAMPLAR 4 (Python/Kivy)
- Core architecture implementation (BIBLE framework)
- Service registry and event bus
- UI components and pages
- Audio processing engine
- Plugin system

---

## Version History

- **2.0.0** - Production release (current)
- **0.1.0** - Development/beta versions

---

## Semantic Versioning

SAMPLAR follows [Semantic Versioning](https://semver.org/):

- **MAJOR** version (2.x.x) - Incompatible API changes
- **MINOR** version (x.0.x) - New features (backwards compatible)
- **PATCH** version (x.x.0) - Bug fixes (backwards compatible)

---

## How to Update

### For Users:
1. Download the latest installer from the website
2. Run `samplar_Setup_v2.0.0.exe`
3. Follow the installation wizard
4. Your settings and licenses will be preserved

### For Developers:
1. Pull the latest code: `git pull origin main`
2. Rebuild: `.\make_portable.ps1`
3. Create installer: `.\make_installer.ps1`

---

## Support

- **Website**: https://yourwebsite.com
- **Email**: support@yourcompany.com
- **Issues**: https://github.com/yourrepo/issues

---

**Note**: This changelog will be updated with each release. Check back for the latest changes!

