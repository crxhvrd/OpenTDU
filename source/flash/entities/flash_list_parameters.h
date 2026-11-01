#pragma once

#include "flash_list.h"

class GameParameter;

// List of editable game parameters (one line per parameter, left/right arrows
// and checkbox clicks edit the selected one)
class FlashListParameters : public FlashList {
public:
    inline void setWidgetPlayer( FlashPlayer *pPlayer ) {
        pWidgetPlayer = pPlayer;
    }

public:
    FlashListParameters();

    bool onMessage( FlashMessage *pMessage, FlashPlayer *pPlayer ) override;
    void tick( float param_2, float param_3 ) override;
    void update( const int32_t index, const int32_t lineIndex ) override;
    void selectedLineChanged() override;

    virtual GameParameter *getParameter( const int32_t index ) = 0;
    virtual const char *applyClick( GameParameter *pParameter,
                                    int32_t *pOutCheckBoxState ) = 0;
    virtual void onParameterChanged() {
        // FUN_0063f2b0
    }
    virtual bool isNextStepLine( const int32_t index ) {
        // FUN_00607000
        return false;
    }
    virtual bool isParameterEditable( GameParameter *pParameter ) {
        // FUN_0042c6b0
        return true;
    }

protected:
    FlashPlayer *pWidgetPlayer;

private:
    void setLineSubClipVisible( const int32_t lineIndex,
                                const char *pSubClip,
                                const bool bVisible );
    void activateArrowRepeat();
    void tickArrowRepeat( const float totalTime );
    void stepSelectedParameter( const bool bIncrement );

private:
    bool bArrowRepeatActive;
    float arrowInitialDelay;
    float arrowMinDelay;
    float arrowLastStepTime;
    float arrowDelay;
    bool bArrowLeftHeld;
    bool bArrowRightHeld;
    bool bArrowLeftClicked;
    bool bArrowRightClicked;
};
