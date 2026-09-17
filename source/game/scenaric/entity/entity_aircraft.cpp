#include "shared.h"
#include "entity_aircraft.h"

#include "entity_aircraft_materials.h"
#include "game/game_mechanism.h"
#include "game/scenaric/gs_scenaric.h"
#include "game/scenaric/scenaric_bank.h"
#include "filesystem/binary.h"
#include "render/2dm.h"
#include "render/instance.h"
#include "system/system_shared.h"

static constexpr int32_t kNumLiveryMaterialGroups = 3;
static constexpr int32_t kNumLiveryVariants = 26;

using LiveryMaterialHashcodes_t =
    std::array<std::array<uint64_t, kNumLiveryVariants>,
               kNumLiveryMaterialGroups>;

static int32_t FindLiveryMaterialGroup( const LiveryMaterialHashcodes_t &table,
                                        const uint64_t hashcode ) {
    for ( int32_t group = 0; group < kNumLiveryMaterialGroups; group++ ) {
        for ( const uint64_t variantHashcode : table[group] ) {
            if ( variantHashcode == hashcode ) {
                return group;
            }
        }
    }
    return -1;
}

static uint32_t GetNumBankMaterials( const RenderFile::Section *pBank ) {
    if ( pBank == nullptr ) {
        return 0;
    }
    return ( pBank->DataSize - sizeof( RenderFile::Section ) ) /
           sizeof( Render3DDHashEntry );
}

bool SerializedAircraftMaterial::deserialize( BinaryReader *pReader ) {
    // FUN_00425830
    pStream = pReader->pCursor;
    pReader->pCursor += sizeof( uint64_t );
    return true;
}

EntityAircraft::EntityAircraft()
    : EntityActor(),
      pMaterials(),
      NumMaterials( 0 ),
      Type( AET_Count ),
      pBankMatMemory( nullptr ),
      BankMatSize( 0 ) {
    // FUN_007eb0e0
}

EntityAircraft::~EntityAircraft() {
    // FUN_00424120
}

void EntityAircraft::initialize() {
    // FUN_007eb7f0
    EntityActor::initialize();
}

void EntityAircraft::loadResources( ScenaricBank *param_2 ) {
    // FUN_007eb2f0
    EntityActor::loadResources( param_2 );

    for ( int32_t i = 0; i < NumMaterials; i++ ) {
        void *pStream = pMaterials[i].pStream;
        const uint64_t hashcode = *reinterpret_cast<uint64_t *>( pStream );

        Entity *pResolved = nullptr;
        if ( param_2 == nullptr ) {
            pResolved = gpScenaric->findEntityByHashcode( hashcode );
        } else if ( hashcode != 0 ) {
            pResolved = param_2->getEntityByHashcode( hashcode );
            if ( pResolved == nullptr ) {
                pResolved = gpScenaric->findEntityByHashcode( hashcode );
            }
        }

        *reinterpret_cast<Entity **>( pStream ) = pResolved;
    }

    Instance *pInstance = Model.pInstance;
    if ( NumMaterials == 0 || pInstance == nullptr ||
         pInstance->getMaterialBank() == nullptr ) {
        return;
    }

    BankMatSize = pInstance->getMaterialBankSize();
    pBankMatMemory = TestDrive::AllocAligned( NumMaterials * BankMatSize );
    if ( pBankMatMemory == nullptr ) {
        return;
    }

    RenderFile::Section *pOriginalBank = pInstance->getMaterialBank();
    const uint32_t numBankMaterials = GetNumBankMaterials( pOriginalBank );

    LiveryMaterialHashcodes_t liveryHashcodes;
    char materialName[64];
    for ( int32_t i = 0; i < kNumLiveryVariants; i++ ) {
        const char variant = static_cast<char>( 'A' + i );

        snprintf( materialName, sizeof( materialName ), "FU_Air%c", variant );
        liveryHashcodes[0][i] = GetIdentifier64bit( materialName );

        snprintf( materialName, sizeof( materialName ), "FU_Air%c_lod",
                  variant );
        liveryHashcodes[1][i] = GetIdentifier64bit( materialName );

        liveryHashcodes[2][i] = GetIdentifier64bit( "Obj_L" );
    }

    std::vector<int32_t> bankMaterialGroups( numBankMaterials, -1 );
    Render3DDHashEntry *pOriginalEntries =
        reinterpret_cast<Render3DDHashEntry *>( pOriginalBank + 1 );
    for ( uint32_t i = 0; i < numBankMaterials; i++ ) {
        bankMaterialGroups[i] = FindLiveryMaterialGroup(
            liveryHashcodes, pOriginalEntries[i].Hashcode );
    }

    for ( int32_t livery = 0; livery < NumMaterials; livery++ ) {
        uint8_t *pLiveryBank =
            static_cast<uint8_t *>( pBankMatMemory ) + livery * BankMatSize;
        pInstance->copyMaterialBank( pLiveryBank );
        pInstance->setMaterialBank(
            reinterpret_cast<RenderFile::Section *>( pLiveryBank ) );

        Entity *pLiveryEntity =
            *reinterpret_cast<Entity **>( pMaterials[livery].pStream );
        if ( pLiveryEntity == nullptr ||
             pLiveryEntity->getType() != eEntityType::ET_AACM ) {
            continue;
        }

        Render2DM *pLivery2DM =
            static_cast<EntityAircraftMaterials *>( pLiveryEntity )
                ->getRender2DM();
        Render2DMHashEntry *pLiveryEntries =
            pLivery2DM != nullptr ? pLivery2DM->getHashTableHead() : nullptr;
        if ( pLiveryEntries == nullptr ) {
            continue;
        }

        const uint32_t numLiveryEntries =
            std::min( numBankMaterials, pLivery2DM->getNumMaterials() );
        std::array<int32_t, kNumLiveryMaterialGroups> groupEntryIndex;
        groupEntryIndex.fill( -1 );
        for ( uint32_t i = 0; i < numLiveryEntries; i++ ) {
            const int32_t group = FindLiveryMaterialGroup(
                liveryHashcodes, pLiveryEntries[i].Hashcode );
            if ( group >= 0 ) {
                groupEntryIndex[group] = static_cast<int32_t>( i );
            }
        }

        Render3DDHashEntry *pLiveryBankEntries =
            reinterpret_cast<Render3DDHashEntry *>(
                pInstance->getMaterialBank() + 1 );
        for ( uint32_t i = 0; i < numBankMaterials; i++ ) {
            const int32_t group = bankMaterialGroups[i];
            if ( group < 0 || groupEntryIndex[group] < 0 ) {
                continue;
            }

            RenderFile::Section *pMaterialSection =
                pLiveryEntries[groupEntryIndex[group]].pEntry;
            if ( pMaterialSection != nullptr ) {
                pLiveryBankEntries[i].pEntry = pMaterialSection;
            }
        }
    }

    pInstance->setMaterialBank( pOriginalBank );
}

void EntityAircraft::setLivery( int32_t liveryIndex ) {
    // FUN_00904ed0 (inlined)
    if ( liveryIndex <= 0 || pBankMatMemory == nullptr ||
         Model.pInstance == nullptr ) {
        return;
    }

    uint8_t *pLiveryBank = static_cast<uint8_t *>( pBankMatMemory ) +
                           ( liveryIndex - 1 ) * BankMatSize;
    Model.pInstance->setMaterialBank(
        reinterpret_cast<RenderFile::Section *>( pLiveryBank ) );
}

void EntityAircraft::unloadResources() {
    // FUN_007eb180
    if ( pParent == nullptr ) {
        if ( pBankMatMemory != nullptr ) {
            TestDrive::FreeAligned( pBankMatMemory );
        }
        pBankMatMemory = nullptr;
    }

    Entity::unloadResources();
    Model.uninstanciate();
}

void EntityAircraft::tick( PlayerGameMech *pGameMech,
                           float deltaTime,
                           float totalTime ) {
    // FUN_007eb7e0
    EntityActor::tick( pGameMech, deltaTime, totalTime );
}

void EntityAircraft::deserialize( BinaryReader *param_2 ) {
    // FUN_007eb280
    EntityActor::deserialize( param_2 );

    const uint32_t count = *reinterpret_cast<uint32_t *>( param_2->pCursor );
    param_2->pCursor += sizeof( uint32_t );

    OTDU_ASSERT( count <= 10 );
    for ( uint32_t i = 0; i < count; i++ ) {
        pMaterials[i].deserialize( param_2 );
    }
    NumMaterials = count;

    Type = static_cast<eAircraftEntityType>(
        *reinterpret_cast<uint32_t *>( param_2->pCursor ) );
    param_2->pCursor += sizeof( uint32_t );
}

void EntityAircraft::instanciate( MngActor *pMngActor, Entity *param_1 ) {
    // FUN_007eb200
    EntityActor::instanciate( pMngActor, param_1 );

    EntityAircraft *pTarget = static_cast<EntityAircraft *>( param_1 );
    pTarget->NumMaterials = NumMaterials;

    for ( int32_t i = 0; i < NumMaterials; i++ ) {
        pTarget->pMaterials[i].pStream = pMaterials[i].pStream;
    }

    pTarget->Type = Type;
    pTarget->BankMatSize = BankMatSize;
    pTarget->pBankMatMemory = pBankMatMemory;
}

void EntityAircraft::deinstanciate() {
    // FUN_007eb1e0
    if ( pMechanism != nullptr && pMechanism->isCreated() ) {
        pMechanism->removeAndDestroy();
    }
}

void EntityAircraft::updateSkinningMatrices() {
    // FUN_007eb0a0
    bLightingValid = true;
}
