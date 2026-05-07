# SAMPLAR License Cleaner Utility

The SM5LicenseCleaner console application removes all locally cached license data for SAMPLAR. Use it when you need to reset a machine before handing it off or to clear corrupt cache entries during support.

## Build

`ash
cmake --build build --config Release --target SM5LicenseCleaner
`

The binary is emitted to uild/SM5LicenseCleaner_artefacts/Release/SM5LicenseCleaner.exe (adjust the configuration name as needed).

## Usage

`
SM5LicenseCleaner.exe [--force|-f] [--dry-run]
`

- --dry-run prints everything that would be removed without deleting files.
- --force (or -f) skips the interactive confirmation prompt.

When SAMPLAR_CACHE_DIR_OVERRIDE is set, the cleaner targets that directory. Otherwise, it operates on the default path (%AppData%/Samplar on Windows).

The tool removes:

- license_cache.json
- license_failures.json (if present)
- the entire sessions directory, including session_*.json
- sessions/encryption.key

A non-zero exit code indicates at least one path could not be deleted.
