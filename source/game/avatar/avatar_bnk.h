#pragma once

#include "shared.h"

#include "filesystem/bank.h"
#include "render/2db.h"
#include "render/2dm.h"
#include "render/3dg.h"

uint64_t AvatarCalcHashcode( const char *pName );

static constexpr uintptr_t kAvatarSkeletonDataOffset =
    2 * sizeof( RenderFile::Section );

static constexpr char kAvatarBnkEmptyName[] = "NoName";

enum eAvatarBankRequest : int32_t {
    ABR_Ready = 0,   // the bank is loaded and handed back in pLoadingBnk
    ABR_Failed = 1,  // the part has no bank, or the load failed
    ABR_Pending = 2, // no free slot yet, retry next tick
};

enum eAvatarBnkLoadStatus : int32_t {
    ABL_None = 0,      // no bank held
    ABL_Requested = 1, // the file request is in flight
    ABL_Installed = 2, // loadContent() ran; the entries are parsed
};

class AvatarBnk : public Bank {
public:
    static constexpr int32_t kMaxGeometries = 0x40;
    static constexpr int32_t kMaxMaterials = 0x40;
    static constexpr int32_t kMaxBitmaps = 0x40;
    static constexpr int32_t kMemoryPoolSize = 0x80000;

    inline bool isInstallOK() const { return bInstallOK; }
    inline bool hasInstalledResources() const { return bCommonBank; }
    inline int32_t getNumReferences() const { return NumReferences; }
    inline const char *getName() const { return Name.c_str(); }
    inline void addReference() { NumReferences++; }
    inline void removeReference() {
        if ( NumReferences > 0 ) {
            NumReferences--;
        }
    }

public:
    explicit AvatarBnk( const bool bCommon = false );
    ~AvatarBnk();

    bool load( const char *pCharacterFolder,
               const char *pBankName,
               const bool bAsync,
               const bool bCritical,
               const bool bUseBootBank );

    void release();
    void allocateMemoryPool();
    void freeMemoryPool();

    void loadContent() override;

    Render3DG *find3DG( uint64_t hashcode );
    Render2DB *find2DB( uint64_t hashcode );
    Render2DM *getMaterial( uint64_t hashcode );

    void *findGeometryStream( uint64_t hashcode );
    void *findBitmapStream( uint64_t hashcode );
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

    Render2DB Bitmaps[kMaxBitmaps];
    int32_t NumBitmaps = 0;

    MaterialEntry Materials[kMaxMaterials];
    int32_t NumMaterials = 0;

    eAvatarBnkLoadStatus LoadingStatus = ABL_None;
    bool bAsyncLoading = false;
    int32_t NumReferences = 0;
    bool bInstallOK = false;
    std::string Name = kAvatarBnkEmptyName;

    bool bCommonBank = false;
    void *pMemoryPool = nullptr;
    int32_t MemoryPoolSize = 0;
    AvatarBnk *pNextInCache = nullptr;

    friend class AvatarBnkCache;

private:
    void parseEntries();
    void releaseParsedEntries();
    void *findStreamByHashcode( int32_t subType, uint64_t hashcode );
    void *getEntryData( const char *pPartName,
                        const char *pExtension,
                        int32_t subType );
};

class AvatarBnkCache {
public:
    static constexpr int32_t kNumNodes = 1;

public:
    AvatarBnkCache();
    ~AvatarBnkCache();

    void initialize();
    void terminate();

    eAvatarBankRequest requestBank( const char *pCharacterFolder,
                                    const char *pBankName,
                                    const bool bAsync,
                                    const bool bCritical,
                                    AvatarBnk **ppOutBnk );

private:
    AvatarBnk *findLoadedBank( const char *pBankName );
    AvatarBnk *findFreeNode();
    AvatarBnk *getOldestBank();
    AvatarBnk *removeOldestBank();
    void pushFront( AvatarBnk *pBnk );
    void moveToFront( AvatarBnk *pBnk );
    bool getBank( const char *pBankName, AvatarBnk **ppOutBnk );

private:
    AvatarBnk Nodes[kNumNodes];
    AvatarBnk *pHead = nullptr;

    int32_t CacheHit = 0;
    int32_t CacheWait = 0;
    int32_t CacheRemove = 0;
    int32_t CacheRequest = 0;
};

class AvatarBnkManager {
public:
    inline AvatarBnk &getCommonBodies() { return commonBodies; }
    inline AvatarBnk &getCommonClothes() { return commonClothes; }
    inline AvatarBnkCache &getBankCache() { return bankCache; }

public:
    void initialize();
    void terminate();

private:
    AvatarBnk commonBodies{ true };
    AvatarBnk commonClothes{ true };
    AvatarBnkCache bankCache;
};
