# Demucs.cpp UVR Optimization - macOS Quick Reference

## 🎯 Quick Summary
Change 4 lines in 2 files to get **3× faster audio processing** with UVR-equivalent quality.

---

## 📝 Changes Required

### File 1: `third_party/demucs.cpp/cli-apps/threaded_inference.hpp`

#### Change 1A - Line 27 (demucscppthreaded namespace)
```cpp
// FIND THIS (around line 27):
const float OVERLAP = 0.75;

// REPLACE WITH:
const float OVERLAP = 0.25;
```

#### Change 1B - Line 202 (demucscppthreaded_v3 namespace)
```cpp
// FIND THIS (around line 202):
const float OVERLAP = 0.75;

// REPLACE WITH:
const float OVERLAP = 0.25;
```

---

### File 2: `third_party/demucs.cpp/src/model.hpp`

#### Change 2A - Line 655 (demucscpp namespace)
```cpp
// FIND THIS (around line 655):
const float OVERLAP = 0.75;              // overlap between segments

// REPLACE WITH:
const float OVERLAP = 0.25;              // overlap between segments
```

#### Change 2B - Line 1403 (demucscpp_v3 namespace)
```cpp
// FIND THIS (around line 1403):
const float OVERLAP = 0.75;              // overlap between segments

// REPLACE WITH:
const float OVERLAP = 0.25;              // overlap between segments
```

---

## 🔧 macOS Build Commands

```bash
# Navigate to demucs.cpp directory
cd third_party/demucs.cpp

# Clean previous build
rm -rf build
mkdir build
cd build

# Configure with CMake
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build (use all CPU cores)
make -j$(sysctl -n hw.ncpu)

# Verify executable was created
ls -lh demucs_mt.cpp.main
```

---

## ✅ Quick Verification

### Check the changes:
```bash
# Verify line 27 in threaded_inference.hpp
grep -n "const float OVERLAP" third_party/demucs.cpp/cli-apps/threaded_inference.hpp

# Expected output:
# 27:const float OVERLAP = 0.25;
# 202:const float OVERLAP = 0.25;

# Verify line 655 and 1403 in model.hpp
grep -n "const float OVERLAP" third_party/demucs.cpp/src/model.hpp

# Expected output:
# 655:const float OVERLAP = 0.25;              // overlap between segments
# 1403:const float OVERLAP = 0.25;              // overlap between segments
```

### Test the executable:
```bash
# Process a test file
./build/demucs_mt.cpp.main -i test.wav -o output_dir

# Verify 3× speedup:
# - 3-minute song should take ~4 minutes (not ~12 minutes)
# - 5-minute song should take ~6-7 minutes (not ~20 minutes)
```

---

## 📊 Expected Results

| Audio Length | Before (0.75) | After (0.25) | Speedup |
|--------------|---------------|--------------|---------|
| 3 minutes | ~12 min | ~4 min | **3× faster** |
| 5 minutes | ~20 min | ~6-7 min | **3× faster** |
| 45 minutes | ~3 hours | ~1 hour | **3× faster** |

---

## 🚨 Common Mistakes

### ❌ Wrong: Changing only 1 or 2 locations
You must change **all 4 OVERLAP values** (2 in each file)

### ❌ Wrong: Using 0.025 instead of 0.25
Make sure it's `0.25` (not `0.025` or `0.250`)

### ❌ Wrong: Not rebuilding after changes
Always do a clean rebuild: `rm -rf build && mkdir build && cd build && cmake .. && make -j$(sysctl -n hw.ncpu)`

---

## 🔍 Search & Replace Commands

### Using sed (macOS/Linux):
```bash
# Backup original files first
cp third_party/demucs.cpp/cli-apps/threaded_inference.hpp third_party/demucs.cpp/cli-apps/threaded_inference.hpp.bak
cp third_party/demucs.cpp/src/model.hpp third_party/demucs.cpp/src/model.hpp.bak

# Replace in threaded_inference.hpp
sed -i '' 's/const float OVERLAP = 0\.75;/const float OVERLAP = 0.25;/g' third_party/demucs.cpp/cli-apps/threaded_inference.hpp

# Replace in model.hpp
sed -i '' 's/const float OVERLAP = 0\.75;/const float OVERLAP = 0.25;/g' third_party/demucs.cpp/src/model.hpp

# Verify changes
grep "const float OVERLAP" third_party/demucs.cpp/cli-apps/threaded_inference.hpp
grep "const float OVERLAP" third_party/demucs.cpp/src/model.hpp
```

### Using VS Code:
1. Open `third_party/demucs.cpp` folder in VS Code
2. Press `Cmd+Shift+F` (Find in Files)
3. Search for: `const float OVERLAP = 0.75;`
4. Replace with: `const float OVERLAP = 0.25;`
5. Click "Replace All" (should find 4 matches)
6. Verify all 4 replacements were made

---

## 📦 Integration with Main Project

### If demucs.cpp is a submodule:
```bash
# After making changes, commit to demucs.cpp
cd third_party/demucs.cpp
git add cli-apps/threaded_inference.hpp src/model.hpp
git commit -m "UVR optimization: Change overlap from 0.75 to 0.25 for 3x speedup"

# Update submodule reference in main project
cd ../..
git add third_party/demucs.cpp
git commit -m "Update demucs.cpp with UVR optimizations (3x faster)"
```

### If demucs.cpp is vendored (copied):
```bash
# Just commit the changes directly
git add third_party/demucs.cpp/cli-apps/threaded_inference.hpp
git add third_party/demucs.cpp/src/model.hpp
git commit -m "UVR optimization: Change overlap from 0.75 to 0.25 for 3x speedup"
```

---

## 🎯 One-Liner Summary

**Change `OVERLAP = 0.75` to `OVERLAP = 0.25` in 4 locations (2 files) for 3× faster processing.**

---

## 📄 Full Documentation

For complete details, see: `DEMUCS_CPP_UVR_OPTIMIZATION_COMPLETE_GUIDE.md`

---

**Quick Reference Version**: 1.0  
**Last Updated**: October 15, 2025  
**Status**: ✅ Ready for macOS Implementation

