# 08 – Licensing and Security

Requires: `Sample_monsta5/docs/refactor_plan/02_EventBus_ServiceRegistry.md`, `Sample_monsta5/docs/refactor_plan/03_UI_Shell_and_Navigation.md`
Provides: feature parity with `license_activation`, `license_system`, `security_ui`.

Goals
- Implement licensing service (key entry, verification, cache, offline grace).
- UI panels for activation and status.
- Secure storage (Windows Credential Vault / encrypted PropertiesFile).

Deliverables
- `src/services/LicensingService.h/.cpp`
- `src/ui/Pages/LicensingPage.h/.cpp`
- Persistence under `%ProgramData%/Samplar` or user profile with encryption.

Acceptance
- Activation flow completes; invalid keys handled gracefully; cache clear works.

