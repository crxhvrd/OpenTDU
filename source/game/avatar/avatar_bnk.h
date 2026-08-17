#pragma once

#include "shared.h"

#include "filesystem/bank.h"
#include "render/2dm.h"
#include "render/3dg.h"

uint64_t AvatarCalcHashcode( const char *pName );

class AvatarBnk : public Bank {
public:
    static constexpr int32_t kMaxGeometries = 0x40;
    static constexpr int32_t kMaxMaterials = 0x40;

    inline bool isInstallOK() const { return bInstallOK; }
    inline int32_t getNumReferences() const { return NumReferences; }
    inline void addReference() { NumReferences++; }
    inline void removeReference() {
        if ( NumReferences > 0 ) {
            NumReferences--;
        }
    }

public:
    AvatarBnk();
    ~AvatarBnk();

    bool load( const char *pCharacterFolder, const char *pBankName );

    void loadContent() override;

    Render3DG *find3DG( uint64_t hashcode );
    Render2DM *getMaterial( uint64_t hashcode );
    void *get2DM( const char *pPartName );
    void *get3DD( const char *pPartName );
    void *getSkeleton( const char *pPartName );

private:
    struct MaterialEntry {
        uint64_t Hashcode = 0;
        Render2DM Material;
    };

private:
    Render3DG Geometries[kMaxGeometries];
    int32_t NumGeometries = 0;

    MaterialEntry Materials[kMaxMaterials];
    int32_t NumMaterials = 0;

    int32_t NumReferences = 0;
    bool bInstallOK = false;
    std::string Name;

private:
    void parseEntries();
    void *getEntryData( const char *pPartName,
                        const char *pExtension,
                        int32_t subType );
};

class AvatarBnkManager {
public:
    inline AvatarBnk &getCommonBodies() { return commonBodies; }
    inline AvatarBnk &getCommonClothes() { return commonClothes; }

public:
    void initialize();
    void terminate();

private:
    AvatarBnk commonBodies;
    AvatarBnk commonClothes;
};
