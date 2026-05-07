#pragma once

#include "DataPaths.h"

namespace samplar {

/**
 * Centralized log path resolution.
 *
 * Delegates to DataPaths::getLogsDirectory() which handles:
 *   - Installed builds (Program Files) -> %APPDATA%/Samplar/logs
 *   - Portable / dev builds            -> <app_dir>/logs
 */
inline juce::File getLogsDirectory()
{
    return DataPaths::getLogsDirectory();
}

} // namespace samplar
