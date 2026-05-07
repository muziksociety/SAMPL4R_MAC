# SM4 → SAMPLAR License Migration Notes

This note documents how to migrate customer state from the legacy SM4 Python licensing system into the new SAMPLAR C++ implementation.

## 1. Export SM4 State
- Collect the existing SM4 license_cache.json files per user.
- Export the customer session tokens and machine fingerprints from SM4 (user_credentials.json).
- Capture a support report listing customers currently in an offline grace period.

## 2. Prepare the SAMPLAR Environment
- Deploy the SAMPLAR build with SAMPLAR_RUN_TESTS=1 once to ensure the embedded unit tests pass.
- Stage a temporary cache location by setting SAMPLAR_CACHE_DIR_OVERRIDE to a writable directory before launching SAMPLAR. Example (PowerShell):
  `powershell
   = "C:\Users\Muzik\AppData\Local\Samplar\migration"
  `
- Launch the app; the new cache and failure-tracking files will be generated in that directory.

## 3. Run the Migration Tooling
At present migration is performed manually via scripted steps:
1. Copy each SM4 license_cache.json into the directory given by SAMPLAR_CACHE_DIR_OVERRIDE.
2. Convert encrypted payloads if needed (SAMPLAR expects Base64-encoded AES blocks). Existing SM4 entries that are still in plaintext will be accepted and re-encrypted on first validation.
3. Copy user session entries into sessions/session_<hardware>.json using the new SAMPLAR schema (ccessToken, userEmail, expiresAtMs, hardwareId).
4. Restart SAMPLAR so it rehydrates the cache and sessions, then validate the license service status from the License Status widget.

## 4. Post-migration Checks
- Use the new License Status widget in the main UI to confirm that license type, expiration, and hardware binding match expectations.
- For users migrated while offline, confirm the grace-period progress bar is shown and that a manual validation clears it.
- Review the log file SAMPLAR_console.log for any license.status events flagged as failures.

## 5. Rollback Plan
- If an issue is detected, clear the SAMPLAR cache directory (SAMPLAR_CACHE_DIR_OVERRIDE) and restore the SM4 files, then relaunch the previous SM4 application while the issue is analyzed.
- Keep the exported SM4 artifacts until all customers have validated successfully under SAMPLAR for at least one cycle.

## 6. Related Utilities
- SAMPLAR_RUN_TESTS=1 — runs all compiled JUCE unit tests (including licensing) and quits.
- SAMPLAR_CACHE_DIR_OVERRIDE — overrides the default cache location, useful for migration rehearsal and automated testing.

