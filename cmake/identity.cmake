# -----------------------------------------------------------------------
# SAMPLAR plugin identity - the ONLY place these codes may be written.
#
# A DAW finds a saved SAMPLAR instance by these codes. Changing one silently
# breaks every customer session that uses the plugin. They must match
# MASTERMIND _registry/plugins/plugin-ids.md (Samplar row) and the Windows repo.
#
# CMakeLists.txt include()s this file and reads the values from here only.
# tools/release_linter/check_plugin_ids.py reads the BUILT bundle and fails the
# build check (ctest PluginIdCheck / release linter) when it differs from this file.
# Protected path: changes need MASTERMIND sign-off (card t_d0d4cc69).
# -----------------------------------------------------------------------
set(SAMPLAR_COMPANY_NAME             "Muziksociety")
set(SAMPLAR_PLUGIN_MANUFACTURER_CODE "Mzks")
set(SAMPLAR_PLUGIN_CODE              "Smpl")

# VST3 processor class ID. JUCE derives it from the two codes above
# (ABCDEF01 9182FAEB + hex(manufacturer) + hex(plugin)); written here so the
# build check compares the built bundle against an explicit expected value.
set(SAMPLAR_VST3_CLASS_ID            "ABCDEF019182FAEB4D7A6B73536D706C")

# Bundle ids. macOS bundles (VST3 plugin and standalone app) use SAMPLAR_BUNDLE_ID;
# the Windows VST3 has always carried the ".vst3" id (not used by any host there).
set(SAMPLAR_BUNDLE_ID                "com.muziksociety.samplar")
set(SAMPLAR_BUNDLE_ID_NON_APPLE      "com.muziksociety.samplar.vst3")

# No AU: there is no Apple developer account (MM-024). When AU is added it must use
# AU type aumu / manufacturer Mzks / subtype Smpl (registry row) - set
# SAMPLAR_AU_MAIN_TYPE here and the build check verifies the .component Info.plist.
