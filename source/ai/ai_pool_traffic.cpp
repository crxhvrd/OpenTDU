#include "shared.h"
#include "ai_pool_traffic.h"

#include "ai/ai_config.h"
#include "ai/ai_pool.h"
#include "ai/ai_sim_bubble.h"
#include "ai/ai_vehicle.h"
#include "ai/gs_ai.h"
#include "core/random/bcpl.h"
#include "database/gs_database.h"
#include "filesystem/gs_file.h"
#include "game/vehicle/bank_vehicle.h"
#include "game/vehicle/car_state.h"
#include "game/vehicle/gs_car_state.h"

extern bool DAT_00fa4030;
extern bool DAT_00fa4031;

static constexpr float kMinFrequency = 1.0e-4f;        // DAT_00f41760
static constexpr float kMaxSpawnWidth = 2.1f;          // FLOAT_00f7c3d0
static constexpr float kMaxSpawnLength = 5.3f;         // FLOAT_00f7c3d4
static constexpr float kMaxSpawnHeight = 2.0f;         // FLOAT_00f7c3d8
static constexpr float kMetersToMillimeters = 1000.0f; // FLOAT_00f41718

int32_t RandomWeightedIndex( RandomGen &random,
                             const float *pWeights,
                             const int32_t count ) {
    // FUN_006d9c00
    float sum = 0.0f;
    for ( int32_t i = 0; i < count; i++ ) {
        sum += pWeights[i];
    }

    const float unit = RandomUnit( random );
    if ( sum <= 0.0f ) {
        const int32_t index = static_cast<int32_t>( unit * count );
        return std::clamp( index, 0, count - 1 );
    }

    const float target = unit * sum;
    float accumulated = 0.0f;
    for ( int32_t i = 0; i < count; i++ ) {
        accumulated += pWeights[i];
        if ( accumulated >= target ) {
            return i;
        }
    }
    return count - 1;
}

int32_t RandomRange( RandomGen &random, const int32_t min, const int32_t max ) {
    // FUN_006d9bb0
    const int32_t value = static_cast<int32_t>(
        RandomBelow( random, static_cast<uint32_t>( max - min + 1 ) ) );
    return std::clamp( value + min, min, max );
}

bool AiPoolTraffic::Template::isLoadCandidate() const {
    // FUN_0091be70
    return pResource == nullptr && ( Frequency > kMinFrequency || bMandatory );
}

bool AiPoolTraffic::Template::hasLoadFailed() const {
    // FUN_0091bea0
    return pResource != nullptr &&
           pResource->Exterior.pBankInstance != nullptr &&
           pResource->Exterior.pBankInstance->hasLoadFailed();
}

bool AiPoolTraffic::Template::isLoaded() const {
    // FUN_0091c940
    return pResource != nullptr && pResource->isLoaded();
}

AiPoolTraffic::AiPoolTraffic( RandomGen *pRandomGen )
    : pRandom( pRandomGen ),
      maxFrequency( 0.0f ),
      meanFrequency( 0.0f ),
      bPaused( false ),
      templates(),
      pFirstTemplateLoaded( nullptr ),
      pFirstTemplateLoading( nullptr ),
      numTotalTemplates( 0 ),
      numLoadableTemplates( 0 ),
      numMandatoryTemplates( 0 ),
      maxLoadedTemplates( 0 ),
      numLoadedTemplates( 0 ),
      numLoadingTemplates( 0 ),
      bCanAddTemplates( true ),
      maxTemplatesToChange( 0 ),
      numTemplatesToChange( 0 ),
      listState( TLS_Idle ),
      templatesList() {
    // FUN_0091c900
}

AiPoolTraffic::Template &AiPoolTraffic::getTemplate( int32_t index ) {
    if ( index < 0 || index >= kMaxTotalTemplates ) {
        index = 0;
    }
    return templates[index];
}

void AiPoolTraffic::initialize() {
    // FUN_0091c760
    pFirstTemplateLoaded = nullptr;
    pFirstTemplateLoading = nullptr;
    templates.fill( Template() );
    numTotalTemplates = 0;
    numMandatoryTemplates = 0;

    for ( const CarConfig &car : gpDatabase->getCarList() ) {
        const DBEntryCarPhysicsData *pCarData =
            gpDatabase->getCarDBEntry( car.Hash );
        if ( pCarData == nullptr || car.Name != "TRAFFIC" ) {
            continue;
        }
        if ( ( pCarData->BitField & 1 ) == 0 || pCarData->Brand == nullptr ||
             ( pCarData->Brand->Flags & 1 ) == 0 ) {
            continue;
        }

        Template &newTemplate = getTemplate( numTotalTemplates );
        newTemplate.bMandatory = ( car.Hash == kMandatoryCarID );
        newTemplate.IDCar = car.Hash;
        newTemplate.IDTire = 0;
        newTemplate.pParent = this;
        newTemplate.pResource = nullptr;
        numTotalTemplates++;
        if ( car.Hash == kMandatoryCarID ) {
            numMandatoryTemplates++;
        }
    }

    maxLoadedTemplates = kMaxLoadedTemplates;
    numLoadedTemplates = 0;
    numLoadingTemplates = 0;
    numTemplatesToChange = 0;
    refreshFrequencies();

    templatesList.clear();
    templatesList.reserve( 16 );
    listState = TLS_Idle;
}

void AiPoolTraffic::terminate() {
    // FUN_0091c420
    templates.fill( Template() );
    templatesList.clear();
}

void AiPoolTraffic::refreshFrequencies() {
    // FUN_0091c480
    float sum = 0.0f;
    numLoadableTemplates = 0;
    maxFrequency = 0.0f;
    for ( int32_t i = 0; i < numTotalTemplates; i++ ) {
        Template &currentTemplate = getTemplate( i );
        if ( !currentTemplate.bMandatory ) {
            currentTemplate.Frequency = 0.0f;
        }
    }

    if ( !gForcedCarModels.empty() ) {
        const int32_t numForcedModels =
            std::min( static_cast<int32_t>( gForcedCarModels.size() ),
                      kMaxTotalTemplates );
        for ( int32_t i = 0; i < numForcedModels; i++ ) {
            const std::string &forcedModel = gForcedCarModels[i];
            if ( forcedModel.empty() ) {
                continue;
            }

            const std::string model =
                forcedModel.substr( 0, forcedModel.size() - 1 );
            const std::string version =
                forcedModel.substr( forcedModel.size() - 1 );
            const int32_t carID = gpDatabase->getVehicleId(
                "TRAFFIC", model.c_str(), version.c_str(), nullptr );
            if ( carID < 0 ) {
                continue;
            }

            for ( int32_t j = 0; j < numTotalTemplates; j++ ) {
                Template &currentTemplate = getTemplate( j );
                if ( currentTemplate.IDCar != carID ) {
                    continue;
                }
                currentTemplate.Frequency = 1.0f;
                sum += 1.0f;
                maxFrequency = std::max( maxFrequency, 1.0f );
                numLoadableTemplates++;
            }
        }
    } else {
        const AIZone *pZone = gpAI->getSimBubble().getCurrentZone();
        for ( const AIZoneVehicle &zoneVehicle : pZone->Vehicles ) {
            for ( int32_t j = 0; j < numTotalTemplates; j++ ) {
                Template &currentTemplate = getTemplate( j );
                if ( currentTemplate.IDCar != zoneVehicle.DatabaseId ) {
                    continue;
                }
                currentTemplate.Frequency = zoneVehicle.Frequency;
                if ( zoneVehicle.Frequency > kMinFrequency ) {
                    sum += zoneVehicle.Frequency;
                    if ( zoneVehicle.Frequency > maxFrequency ) {
                        maxFrequency = zoneVehicle.Frequency;
                    }
                    numLoadableTemplates++;
                }
            }
        }
    }

    meanFrequency = ( numLoadableTemplates > 0 )
                        ? sum / static_cast<float>( numLoadableTemplates )
                        : 0.0f;

    if ( !DAT_00fa4031 && !DAT_00fa4030 &&
         numLoadableTemplates > maxLoadedTemplates ) {
        maxTemplatesToChange = std::max( maxLoadedTemplates, 1 );
    } else {
        maxTemplatesToChange = 0;
    }
}

void AiPoolTraffic::PushFront( Template *pTemplate, Template **ppHead ) {
    // FUN_0091be50
    pTemplate->pNext = *ppHead;
    pTemplate->pPrevious = nullptr;
    if ( *ppHead != nullptr ) {
        ( *ppHead )->pPrevious = pTemplate;
    }
    *ppHead = pTemplate;
}

void AiPoolTraffic::Unlink( Template *pTemplate, Template **ppHead ) {
    // FUN_0091be20
    if ( pTemplate->pPrevious != nullptr ) {
        pTemplate->pPrevious->pNext = pTemplate->pNext;
    }
    if ( pTemplate->pNext != nullptr ) {
        pTemplate->pNext->pPrevious = pTemplate->pPrevious;
    }
    if ( pTemplate == *ppHead ) {
        *ppHead = pTemplate->pNext;
    }
    pTemplate->pPrevious = nullptr;
    pTemplate->pNext = nullptr;
}

bool AiPoolTraffic::loadTemplate( Template *pTemplate, const bool bAsync ) {
    // FUN_0091d440
    pTemplate->NumUsed = 0;
    VehicleBaseResources *pResource = gpCarState->grabFreeResourceEntry();
    if ( pResource != nullptr &&
         !pResource->load( pTemplate->IDCar, 0, pTemplate->IDTire, 0, ~0ull,
                           bAsync ? 1 : 0,
                           eVehicleResourceType::VRT_Traffic ) ) {
        gpCarState->releaseResourceEntry( pResource );
        pResource = nullptr;
    }
    pTemplate->pResource = pResource;
    return pResource != nullptr;
}

void AiPoolTraffic::unloadTemplate( Template *pTemplate ) {
    // FUN_0091cfd0
    if ( pTemplate->pResource != nullptr ) {
        pTemplate->pResource->unload();
        gpCarState->releaseResourceEntry( pTemplate->pResource );
    }
    pTemplate->pResource = nullptr;
    Unlink( pTemplate, &pFirstTemplateLoaded );
    numLoadedTemplates--;
}

void AiPoolTraffic::updateLoadingTemplates() {
    // FUN_0091ca60
    if ( pFirstTemplateLoading == nullptr ) {
        return;
    }

    Template *pOldest = pFirstTemplateLoading;
    while ( pOldest->pNext != nullptr ) {
        pOldest = pOldest->pNext;
    }

    if ( pOldest->isLoaded() ) {
        Unlink( pOldest, &pFirstTemplateLoading );
        PushFront( pOldest, &pFirstTemplateLoaded );
        numLoadingTemplates--;
        numLoadedTemplates++;
    } else if ( pOldest->hasLoadFailed() ) {
        Unlink( pOldest, &pFirstTemplateLoading );
        numLoadingTemplates--;
    }
}

AiPoolTraffic::Template *AiPoolTraffic::pickRandomTemplateToLoad() {
    // FUN_0091bf60
    const int32_t numCandidates =
        numMandatoryTemplates - numLoadedTemplates + numLoadableTemplates;
    if ( numCandidates <= 0 ) {
        return nullptr;
    }

    for ( int32_t i = 0; i < numTotalTemplates; i++ ) {
        Template &currentTemplate = getTemplate( i );
        if ( currentTemplate.isLoadCandidate() && currentTemplate.bMandatory ) {
            return &currentTemplate;
        }
    }

    int32_t pick = static_cast<int32_t>(
        RandomBelow( *pRandom, static_cast<uint32_t>( numCandidates ) ) );
    pick = std::clamp( pick, 0, numCandidates - 1 );

    int32_t candidateIndex = 0;
    for ( int32_t i = 0; i < numTotalTemplates; i++ ) {
        Template &currentTemplate = getTemplate( i );
        if ( !currentTemplate.isLoadCandidate() ) {
            continue;
        }
        if ( candidateIndex == pick ) {
            return &currentTemplate;
        }
        candidateIndex++;
    }
    return nullptr;
}

void AiPoolTraffic::markTemplateForChange( const bool bMark,
                                           Template *pTemplate,
                                           const float delay ) {
    // FUN_0091cd70
    pTemplate->bMarkedForChange = bMark;
    if ( !bMark || delay < 0.0f ) {
        return;
    }

    for ( AiVehicle *pVehicle : gpAI->getAIPool()->getVehicles() ) {
        CarState *pCarState = pVehicle->getCarState();
        if ( pCarState == nullptr || !IsLocallyOwned( pCarState ) ) {
            continue;
        }
        if ( pCarState->getCarID() == pTemplate->IDCar ) {
            pVehicle->requestRemoval( delay, "template is marked for change" );
        }
    }
}

void AiPoolTraffic::tick() {
    // FUN_0091d5b0
    updateLoadingTemplates();
    if ( bPaused ) {
        return;
    }
    updateListState();

    // TODO: ONLINE
    const bool bCanAdd = bCanAddTemplates;

    if ( bCanAdd && numLoadingTemplates == 0 &&
         numLoadedTemplates < maxLoadedTemplates &&
         !gpFile->hasTaskPending() ) {
        Template *pTemplate = pickRandomTemplateToLoad();
        if ( pTemplate != nullptr && pTemplate->isLoadCandidate() ) {
            if ( loadTemplate( pTemplate, true ) ) {
                numLoadingTemplates++;
                PushFront( pTemplate, &pFirstTemplateLoading );
            } else {
                pTemplate->Frequency = 0.0f;
            }
        }
    }

    if ( numTemplatesToChange > 0 ) {
        for ( Template *pTemplate = pFirstTemplateLoaded; pTemplate != nullptr;
              pTemplate = pTemplate->pNext ) {
            if ( !pTemplate->bMarkedForChange ) {
                continue;
            }
            if ( pTemplate->pResource != nullptr &&
                 pTemplate->pResource->RefCount > 0 ) {
                continue;
            }
            pTemplate->bMarkedForChange = false;
            unloadTemplate( pTemplate );
            numTemplatesToChange--;
            break;
        }
    }

    if ( bCanAdd && numTemplatesToChange < maxTemplatesToChange ) {
        static float sSwapThreshold =
            static_cast<float>( gAIConfig.MaxNumVehicle ) / meanFrequency;

        float bestScore = sSwapThreshold;
        Template *pPick = nullptr;
        for ( Template *pTemplate = pFirstTemplateLoaded; pTemplate != nullptr;
              pTemplate = pTemplate->pNext ) {
            if ( pTemplate->bMarkedForChange || pTemplate->bMandatory ) {
                continue;
            }
            if ( pTemplate->Frequency <= kMinFrequency ) {
                pPick = pTemplate;
                break;
            }
            const float score = static_cast<float>( pTemplate->NumUsed ) *
                                ( maxFrequency / pTemplate->Frequency );
            if ( score > bestScore ) {
                bestScore = score;
                pPick = pTemplate;
            }
        }

        if ( pPick != nullptr ) {
            markTemplateForChange( true, pPick, -1.0f );
            numTemplatesToChange++;
        }
    }
}

void AiPoolTraffic::updateListState() {
    // FUN_0091d500
    if ( listState == TLS_Idle ) {
        return;
    }

    if ( listState == TLS_ApplyPending && numTemplatesToChange == 0 ) {
        for ( const AiTemplatesData &data : templatesList ) {
            processTemplateAction( data );
        }
        templatesList.clear();
        listState = TLS_Loading;
    }
    if ( listState == TLS_Loading && numLoadingTemplates == 0 ) {
        templatesList.clear();
        listState = TLS_Idle;
        bCanAddTemplates = true;
    }
    if ( listState == TLS_Deferred && numLoadingTemplates == 0 ) {
        diffTemplateList();
        processRemovals();
        listState = TLS_ApplyPending;
    }
}

void AiPoolTraffic::processTemplateAction( const AiTemplatesData &data ) {
    // FUN_0091d4a0
    Template *pTemplate = data.pRelatedTemplate;
    if ( !pTemplate->isLoadCandidate() ) {
        return;
    }
    if ( loadTemplate( pTemplate, true ) ) {
        numLoadingTemplates++;
        PushFront( pTemplate, &pFirstTemplateLoading );
    } else {
        pTemplate->Frequency = 0.0f;
    }
}

void AiPoolTraffic::diffTemplateList() {
    // FUN_0091c0f0
    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE
}

void AiPoolTraffic::processRemovals() {
    // FUN_0091ce80
    OTDU_UNIMPLEMENTED_ONCE; // TODO: ONLINE
}

void AiPoolTraffic::unloadAll() {
    // FUN_0091d030
    while ( pFirstTemplateLoading != nullptr ) {
        gpFile->tick( 0.0f, 0.0f );
        updateLoadingTemplates();
    }
    while ( pFirstTemplateLoaded != nullptr ) {
        unloadTemplate( pFirstTemplateLoaded );
    }
}

AiPoolTraffic::Template *
AiPoolTraffic::findLoadedTemplate( const int32_t carID ) const {
    for ( Template *pTemplate = pFirstTemplateLoaded; pTemplate != nullptr;
          pTemplate = pTemplate->pNext ) {
        if ( pTemplate->IDCar == carID ) {
            return pTemplate;
        }
    }
    return nullptr;
}

AiPoolTraffic::Template *
AiPoolTraffic::pickTemplateForSpawn( const int32_t zoneIndex,
                                     const bool bSizeFilter ) {
    // FUN_0091cb10
    const int32_t numAvailable =
        std::max( numLoadedTemplates - numMandatoryTemplates, 0 );
    float weights[kMaxTotalTemplates] = {};
    Template *pCandidates[kMaxTotalTemplates] = {};

    const AIZone *pZone = gpAI->getSimBubble().getCurrentZone();
    if ( zoneIndex >= 0 && zoneIndex < AIConfig::kMaxNumZones ) {
        pZone = &gAIConfig.Zones[zoneIndex];
    }
    if ( pZone == nullptr ) {
        pZone = &gAIConfig.Zones[0];
    }

    int32_t numCandidates = 0;
    float sum = 0.0f;
    for ( Template *pTemplate = pFirstTemplateLoaded; pTemplate != nullptr;
          pTemplate = pTemplate->pNext ) {
        if ( pTemplate->bMarkedForChange || pTemplate->bMandatory ) {
            continue;
        }
        if ( bSizeFilter ) {
            const DBEntryCarPhysicsData *pCarData =
                gpDatabase->getCarDBEntry( pTemplate->IDCar );
            if ( pCarData != nullptr &&
                 ( pCarData->Width >
                       static_cast<int32_t>( kMaxSpawnWidth *
                                             kMetersToMillimeters ) ||
                   pCarData->Length >
                       static_cast<int32_t>( kMaxSpawnLength *
                                             kMetersToMillimeters ) ||
                   pCarData->Height >
                       static_cast<int32_t>( kMaxSpawnHeight *
                                             kMetersToMillimeters ) ) ) {
                continue;
            }
        }

        float frequency = 0.0f;
        for ( const AIZoneVehicle &zoneVehicle : pZone->Vehicles ) {
            if ( zoneVehicle.DatabaseId == pTemplate->IDCar ) {
                frequency = zoneVehicle.Frequency;
                break;
            }
        }
        weights[numCandidates] = frequency;
        pCandidates[numCandidates] = pTemplate;
        sum += frequency;
        numCandidates++;
    }

    numCandidates = std::min( numCandidates, numAvailable );
    if ( numCandidates <= 0 ) {
        return nullptr;
    }
    if ( sum == 0.0f ) {
        for ( int32_t i = 0; i < numCandidates; i++ ) {
            weights[i] = pCandidates[i]->Frequency;
        }
    }
    return pCandidates[RandomWeightedIndex( *pRandom, weights, numCandidates )];
}

VehicleBaseResources *
AiPoolTraffic::getTemplateResource( const int32_t carID ) {
    // FUN_0091d7f0
    if ( Template *pLoaded = findLoadedTemplate( carID ) ) {
        return pLoaded->pResource;
    }

    while ( numLoadedTemplates >= maxLoadedTemplates ) {
        Template *pLeastUsed = pFirstTemplateLoaded;
        int32_t minRefCount =
            pLeastUsed->pResource ? pLeastUsed->pResource->RefCount : 0;
        for ( Template *pTemplate = pLeastUsed->pNext; pTemplate != nullptr;
              pTemplate = pTemplate->pNext ) {
            const int32_t refCount =
                pTemplate->pResource ? pTemplate->pResource->RefCount : 0;
            if ( refCount < minRefCount ) {
                minRefCount = refCount;
                pLeastUsed = pTemplate;
            }
        }
        forceUnloadTemplate( pLeastUsed->IDCar );
    }

    for ( int32_t i = 0; i < numTotalTemplates; i++ ) {
        Template &currentTemplate = getTemplate( i );
        if ( currentTemplate.IDCar != carID ) {
            continue;
        }
        if ( currentTemplate.pResource != nullptr &&
             currentTemplate.pResource->isLoaded() ) {
            return nullptr;
        }
        if ( !loadTemplate( &currentTemplate, false ) ) {
            return nullptr;
        }
        numLoadedTemplates++;
        PushFront( &currentTemplate, &pFirstTemplateLoaded );
        return currentTemplate.pResource;
    }
    return nullptr;
}

void AiPoolTraffic::forceUnloadTemplate( const int32_t carID ) {
    // FUN_0091d790
    Template *pTemplate = findLoadedTemplate( carID );
    if ( pTemplate == nullptr ) {
        return;
    }

    gpAI->getAIPool()->removeVehiclesWithCar( carID );
    if ( pTemplate->bMarkedForChange ) {
        numTemplatesToChange--;
    }
    if ( pTemplate->bMandatory ) {
        numMandatoryTemplates--;
    }
    pTemplate->bMarkedForChange = false;
    unloadTemplate( pTemplate );
}
