#include "shared.h"
#include "entity_sound_density.h"

#include "audio/implementation/audio_dispatcher_playback.h"
#include "config/gs_config.h"
#include "filesystem/binary.h"
#include "game/game_mechanism.h"
#include "system/system_shared.h"

EntitySoundDensity::DensitySlot
    EntitySoundDensity::Slots[EntitySoundDensity::kMaxDensitySlots] = {};

EntitySoundDensity::EntitySoundDensity()
    : EntitySoundEmitter(),
      pBankName( nullptr ),
      pBankData( nullptr ),
      SlotIndex( -1 ),
      State( SDS_Idle ) {
    // FUN_007dfec0
}

EntitySoundDensity::~EntitySoundDensity() {
    // FUN_004268c0
}

void EntitySoundDensity::loadResources( ScenaricBank *param_2 ) {
    // FUN_007dff40
    if ( pMechanism != nullptr ) {
        pMechanism->loadResources( param_2 );
    }

    resolveSoundReference();

    pBankData = nullptr;
    pSample = nullptr;
}

void EntitySoundDensity::unloadResources() {
    // FUN_007df100
    uninstallSoundBank();
}

void EntitySoundDensity::deserialize( BinaryReader *param_2 ) {
    // FUN_007df9e0
    Entity::deserialize( param_2 );

    pBankName = reinterpret_cast<const char *>( param_2->pCursor );
    while ( *param_2->pCursor != '\0' ) {
        param_2->pCursor++;
    }
    param_2->pCursor = reinterpret_cast<uint8_t *>(
        ( reinterpret_cast<uintptr_t>( param_2->pCursor ) + 4 ) &
        ~static_cast<uintptr_t>( 3 ) );

    deserializeSoundParameters( param_2 );
}

void EntitySoundDensity::installSoundBank() {
    // FUN_007e0020
    if ( pBankName == nullptr || pSample != nullptr || pBankData != nullptr ||
         State != SDS_Idle ) {
        return;
    }

    SlotIndex = -1;

    int32_t freeSlot = 0;
    while ( freeSlot < kMaxDensitySlots && Slots[freeSlot].pOwner != nullptr ) {
        freeSlot++;
    }

    if ( freeSlot >= kMaxDensitySlots ) {
        return;
    }

    Slots[freeSlot].pOwner = this;
    Slots[freeSlot].State = SDS_Idle;
    SlotIndex = freeSlot;

    std::string bankPath = gpConfig->getResRootPath();
    bankPath += "Sound/Cinematics/";
    bankPath += pBankName;

    OTDU_UNIMPLEMENTED_ONCE; // TODO: AUDIO (cinematic sound bank install)

    Slots[SlotIndex].State = SDS_Finished;
    Slots[SlotIndex].pBankData = nullptr;
    State = SDS_Finished;
}

void EntitySoundDensity::uninstallSoundBank() {
    // FUN_007df110
    if ( State == SDS_Idle ) {
        return;
    }

    if ( pSoundInstance != nullptr ) {
        pSoundInstance->setSample( nullptr );
        pSoundInstance->release();
        pSoundInstance = nullptr;
    }

    if ( State != SDS_Installing ) {
        if ( pBankData != nullptr ) {
            TestDrive::FreeAligned( pBankData );
        }
        pBankData = nullptr;
        pSample = nullptr;

        Slots[SlotIndex].pOwner = nullptr;
        Slots[SlotIndex].pBankData = nullptr;
    }

    State = SDS_Idle;
    Slots[SlotIndex].State = SDS_Idle;
}

void EntitySoundDensity::onSoundBankInstalled() {
    // FUN_007df1d0
    State = SDS_Finished;
    Slots[SlotIndex].State = SDS_Finished;

    OTDU_UNIMPLEMENTED_ONCE; // TODO: AUDIO (pSample = the installed bank)
}
