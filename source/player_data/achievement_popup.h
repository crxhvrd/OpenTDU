#pragma once

// The '/achievement' and '/reward' panels of GENERAL movie
class AchievementPopUp {
public:
    static constexpr int32_t kNumSlots = 8;
    static constexpr int32_t kTextLength = 400;

    enum ePopUpDrawMode : int32_t {
        PUDM_None = 0,
        PUDM_Achievement = 1,
        PUDM_Reward = 2,
    };

public:
    AchievementPopUp();

    void clear();
    void push( const char *pTitle, const char *pText );
    void pushReward( const char *pText );
    void tick( const bool bCanDisplay );

private:
    struct PopUpText {
        char Title[kTextLength];
        char Text[kTextLength];
    };

private:
    PopUpText pStack[kNumSlots];
    int32_t current;
    int32_t last;
    char pStackRewards[kNumSlots][kTextLength];
    int32_t currentReward;
    int32_t lastReward;
    float startTime;
    ePopUpDrawMode drawMode;
};
