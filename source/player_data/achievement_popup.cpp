#include "shared.h"
#include "achievement_popup.h"

#include "core/time.h"
#include "flash/gs_flash.h"
#include "flash/implementation/flash_player.h"

static constexpr float kPopupDisplayDuration = 12.0f; // DAT_00f41850

static const char *const kAchievementClip = "/achievement";
static const char *const kRewardClip = "/reward";

AchievementPopUp::AchievementPopUp() { clear(); }

void AchievementPopUp::clear() {
    memset( pStack, 0, sizeof( pStack ) );
    memset( pStackRewards, 0, sizeof( pStackRewards ) );

    current = 0;
    last = 0;
    currentReward = 0;
    lastReward = 0;
    startTime = 0.0f;
    drawMode = PUDM_None;
}

void AchievementPopUp::push( const char *pTitle, const char *pText ) {
    // FUN_0089bc20
    PopUpText &slot = pStack[last];
    strncpy( slot.Title, pTitle, kTextLength );
    strncpy( slot.Text, pText, kTextLength );
    slot.Title[kTextLength - 1] = '\0';
    slot.Text[kTextLength - 1] = '\0';

    last = ( last + 1 ) % kNumSlots;
}

void AchievementPopUp::pushReward( const char *pText ) {
    // FUN_0089bbc0
    char *pSlot = pStackRewards[lastReward];
    strncpy( pSlot, pText, kTextLength );
    pSlot[kTextLength - 1] = '\0';

    lastReward = ( lastReward + 1 ) % kNumSlots;
}

void AchievementPopUp::tick( const bool bCanDisplay ) {
    // FUN_0089d600
    if ( drawMode != PUDM_None ) {
        if ( ( startTime + kPopupDisplayDuration ) < GetTimeSeconds() ) {
            FlashPlayer *pPlayer = gpFlash->getFlashPlayer( "GENERAL" );
            if ( pPlayer == nullptr ) {
                return;
            }

            if ( drawMode == PUDM_Achievement ) {
                pPlayer->showFrame( "close", kAchievementClip, true );
            } else if ( drawMode == PUDM_Reward ) {
                pPlayer->showFrame( "close", kRewardClip, true );
            }

            drawMode = PUDM_None;
        }

        if ( drawMode != PUDM_None ) {
            return;
        }
    }

    if ( !bCanDisplay ) {
        return;
    }

    FlashPlayer *pPlayer = gpFlash->getFlashPlayer( "GENERAL" );
    if ( pPlayer == nullptr ) {
        return;
    }

    startTime = GetTimeSeconds();

    if ( current != last ) {
        if ( !pPlayer->spriteExists( kAchievementClip ) ) {
            return;
        }

        pPlayer->setVariableValue( "/:AchievementsTitle",
                                   pStack[current].Title );
        pPlayer->setVariableValue( "/:AchievementsText", pStack[current].Text );
        pPlayer->showFrame( "open", kAchievementClip, true );

        OTDU_LOG_INFO( "Achievement popup: '%s' / '%s'\n",
                       pStack[current].Title, pStack[current].Text );

        current = ( current + 1 ) % kNumSlots;
        drawMode = PUDM_Achievement;
        return;
    }

    if ( currentReward == lastReward ) {
        return;
    }

    if ( !pPlayer->spriteExists( kRewardClip ) ) {
        return;
    }

    pPlayer->setVariableValue( "/:RewardText", pStackRewards[currentReward] );
    pPlayer->showFrame( "open", kRewardClip, true );

    OTDU_LOG_DEBUG( "Reward popup: '%s'\n", pStackRewards[currentReward] );

    currentReward = ( currentReward + 1 ) % kNumSlots;
    drawMode = PUDM_Reward;
}
