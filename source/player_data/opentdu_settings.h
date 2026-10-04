#pragma once

// Per-profile OpenTDU settings. 
// Location: savegame/<profile>/OpenTDU.ini.
namespace OpenTDUSettings {
    void Load();
    void Save();
} // namespace OpenTDUSettings
