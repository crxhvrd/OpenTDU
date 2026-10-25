#pragma once

#include "file_map.h"
#include "core/markup.h"

static constexpr uint32_t kMaxContentDescSize = 0x8000;
static constexpr uint32_t kMaxFoundCars = 0x180;
static constexpr uint32_t kMaxInstalledContent = 0x7f;
static constexpr uint32_t kMaxContentDescriptionLength = 0x100;
static constexpr const char *kContentDescFilename = "contentdesc.xml";
static constexpr int32_t kMaxRadioSlots = 0x40;
static constexpr int32_t kMaxStationsPerRadioSlot = 4;

class RadioStation;

struct FoundCar {
    uint32_t DatabaseID = 0;
    uint8_t bUnlocked : 1;
    char CardealerSpotName[32] = {};

    FoundCar() : bUnlocked( false ) {}
};

enum eContentAction : uint32_t {
    CA_AddVehicle = 1,
    CA_UnlockVehicle = 2,
    CA_AddRadioStation = 4,
};

enum eContentDescResult : int32_t {
    CDR_Invalid = 0,
    CDR_Valid = 1,
    CDR_NoVehicle = 2,
};

struct ModRadioSlot {
    std::string UnitName;
    RadioStation *pStations[kMaxStationsPerRadioSlot] = {};
};

struct InstalledContent {
    std::wstring DisplayName;
    uint32_t ActionMask = 0;
};

struct ModDBStringTranslation {
    char Language[3] = {};
    std::string Value;
};

struct ModDBString {
    uint64_t Hashcode = 0;
    std::string Value; // used when no translation matches the language
    std::vector<ModDBStringTranslation> Translations;

    const std::string &resolve( const char *pLanguage ) const;
};

struct ModDBInjection {
    std::wstring PackName;
    std::string Table;
    std::vector<std::string> Rows;
    std::vector<ModDBString> Strings;
    uint8_t bMandatory : 1;

    ModDBInjection() : bMandatory( false ) {}
};

class ModInstaller {
public:
    inline const char *getDescription() const { return pDescription; }
    inline const std::vector<InstalledContent> &getInstalledContent() const {
        return installedContent;
    }
    inline const std::vector<ModDBInjection> &getDatabaseInjections() const {
        return dbInjections;
    }

public:
    ModInstaller();
    ~ModInstaller();

    void reset();
    void markInjectionsMandatory( const std::wstring &packName );

    static bool HasContentDesc( const EnumerateContextItem &item );

    eContentDescResult loadContentDesc( const EnumerateContextItem &item,
                                        const bool bApplyContent );

    eContentDescResult checkContentFromMem();
    eContentDescResult loadFromMem( const uint32_t licenceFlags,
                                    const EnumerateContextItem &item,
                                    const bool bApplyContent );

    const FoundCar *findCar( const uint32_t databaseId,
                             const char *pEnvName ) const;

private:
    FoundCar *findCarSlot( const uint32_t databaseId, const char *pEnvName );
    FoundCar *allocateCarSlot();

    void onAddVehicle( const char *pLanguage,
                       const uint32_t databaseId,
                       const char *pEnvName );
    void onUnlockVehicle( const char *pLanguage,
                          const uint32_t databaseId,
                          const char *pEnvName );
    void onPostNews( const char *pEnvName,
                     const char *pText,
                     const int32_t moneyReward );
    int32_t onAddRadioStation( const char *pEnvName,
                               const EnumerateContextItem &item,
                               int32_t radioSlot );

    void onAddDBRow( const EnumerateContextItem &item,
                     const char *pTable,
                     const char *pRow );
    void onAddDBString( const EnumerateContextItem &item,
                        const char *pTable,
                        const char *pHashcode,
                        const char *pValue );
    void readDBStringTranslations( ModDBString &string );
    ModDBInjection &getInjectionSlot( const EnumerateContextItem &item,
                                      const char *pTable );

    bool addInstalledContent( const std::wstring &displayName,
                              const uint32_t actionMask );

private:
    std::vector<char> contentDescBuffer;
    uint32_t contentDescSize;

    FoundCar foundCars[kMaxFoundCars];
    std::vector<InstalledContent> installedContent;
    std::vector<ModDBInjection> dbInjections;

    ModRadioSlot radioSlots[kMaxRadioSlots];
    int32_t numRadioSlots;

    char pDescription[kMaxContentDescriptionLength];

    CMarkup reader;
};

extern ModInstaller *gpModInstaller;
