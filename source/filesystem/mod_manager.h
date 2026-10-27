#pragma once

#include "mod_installer.h"

struct ModPack {
    std::string Id;   // folder name, and the mods.ini section name
    std::string Path; // pack folder, relative to the working directory
    std::wstring DisplayName;
    ModDBString Description; // contentdesc.xml DESC; Value = Id fallback

    uint32_t NumEntries = 0;   // records in the pack's .map
    int32_t FilemapIndex = -1; // slot it mounted on, -1 when it did not

    uint8_t bEnabled : 1;     // the user's choice, persisted in mods.ini
    uint8_t bConflicting : 1; // claims a file another enabled pack claims
    uint8_t bMounted : 1;
    uint8_t bIncomplete : 1; // missing a half, so it can never be mounted
    uint8_t bMandatory : 1;  // built into the port; always on, never saved
    uint8_t bDescriptorOnly : 1; // contentdesc.xml without a .map/.big

    std::string IncompleteReason;
    std::string Status;

    ModPack()
        : bEnabled( false ),
          bConflicting( false ),
          bMounted( false ),
          bIncomplete( false ),
          bMandatory( false ),
          bDescriptorOnly( false ) {}
};

class ModManager {
public:
    inline std::vector<ModPack> &getPacks() { return packs; }
    inline const std::vector<ModPack> &getPacks() const { return packs; }
    inline bool hasPendingChanges() const { return bPendingChanges; }
    inline void setPendingChanges( const bool bPending ) {
        bPendingChanges = bPending;
    }

public:
    ModManager();
    ~ModManager();

    void refresh();

    bool isEnabled( const char *pId ) const;
    void setEnabled( const char *pId, const bool bEnabled );

    bool save();

    ModPack *findPack( const char *pId );

private:
    void addBuiltInPacks();
    void loadEnabledState();
    void detectConflicts();

private:
    std::vector<ModPack> packs;
    uint8_t bPendingChanges : 1;
};

extern ModManager *gpModManager;
