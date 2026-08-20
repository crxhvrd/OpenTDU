#include "shared.h"
#include "extra_challenge_cine_bank.h"

#include "game/scenaric/gs_scenaric.h"

static constexpr const char *kIntroPackageName =
    "ExtraChallengesCine_Intro"; // DAT_00f2b6c8
static constexpr const char *kOutroPackageName =
    "ExtraChallengesCine_Outro"; // DAT_00f2b6e4
static constexpr const char *kPackageFolder = "scenarics/Cinematics";

bool ExtraChallengeCineBank::load( eMode requestedMode ) {
    // FUN_007dc590
    if ( requestedMode == mode ) {
        return true;
    }

    unload();

    const char *pPackageName = nullptr;
    switch ( requestedMode ) {
    case ECBM_Intro:
        pPackageName = kIntroPackageName;
        break;
    case ECBM_Outro:
        pPackageName = kOutroPackageName;
        break;
    default:
        return false;
    }

    state = ECBS_Loading;

    GSScenaric::LoadScenaricBank( kPackageFolder, pPackageName, &bank, true );

    if ( !bank.isLoaded() ) {
        state = ECBS_Unloaded;
        return false;
    }

    state = ECBS_Loaded;
    mode = requestedMode;
    return true;
}

void ExtraChallengeCineBank::unload() {
    // FUN_007dc510
    if ( state != ECBS_Loaded ) {
        return;
    }

    if ( gpScenaric != nullptr ) {
        gpScenaric->unloadBank( &bank );
    }

    state = ECBS_Unloaded;
    mode = ECBM_None;
}
