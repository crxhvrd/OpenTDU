#pragma once

class FlashPlayer;

#include "flash_entity.h"

class FlashSlider {
public:
    bool hasFocus() const { return bHasFocus; }
    float getValue() const { return value; }
    bool isDirty() const { return bDirty; }
    float getMinValue() const { return minMaxValues[0]; }
    float getMaxValue() const { return minMaxValues[1]; }
    bool isLeftPressed() const { return bLeftPressed; }
    bool isRightPressed() const { return bRightPressed; }
    int32_t getIndex() const { return index; }
    inline void setIndex( const int32_t value ) { index = value; }
    inline void setVelocity( const float value ) { velocity = value; }
    inline void setMaxValue( const float value ) { minMaxValues[1] = value; }
    inline void setVerticalDrag( const bool bVertical ) {
        dragAxis = bVertical ? 1 : 0;
    }

    inline void rebind( FlashPlayer *pPlayer,
                        const char *pVarName,
                        int32_t orientation = 1 ) {
        // FUN_009b51a0 (inlined)
        pFlashPlayer = pPlayer;
        varName = pVarName;
        isVertical = orientation;
    }

public:
    FlashSlider();
    ~FlashSlider();

    void setFocusState( bool param_2 );
    void setRange( float min, float max );
    void setAutoVelocity( bool param_1 );
    void setValue( float param_1 );
    void bind( FlashPlayer *pFlashPlayer,
               const char *pVarName,
               int32_t orientation,
               float initialValue );

    void tick( float param_1 );
    void disable();

    static bool OnMessage( FlashMessage *pMessage );

private:
    float velocity;
    float value;
    float minMaxValues[2];

    FlashPlayer *pFlashPlayer;
    std::string varName;

    int32_t index;
    int32_t isVertical;
    int32_t dragAxis;

    bool bLeftPressed;
    bool bRightPressed;

    uint8_t bHasFocus : 1;
    uint8_t bDoNotRepeat : 1;
    uint8_t bDirty : 1;
    uint8_t bAutoVelocity : 1;
    uint8_t bSFXPlaying : 1;

    uint8_t field19_0x6a : 1;
    uint8_t field20_0x6b : 1;
    uint8_t field18_0x69 : 1;

private:
    void FUN_0071baf0();
    void playSFXValueChanged();
    void stopSFXValueChanged();

    uint32_t FUN_0071b940();
};
