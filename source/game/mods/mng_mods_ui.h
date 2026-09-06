#pragma once

#include "flash/entities/flash_entity.h"
#include "flash_list_mods.h"

class FlashPlayer;

class MngModsUI : public FlashEntity {
public:
    inline bool isVisible() const { return bVisible; }

public:
    MngModsUI();
    ~MngModsUI();

    static void RegisterMovie();

    void bind();
    void unbind();
    void pollInput();
    int32_t getPackListCapacity() const;
    int32_t getDetailListCapacity() const;

    void tick( float totalTime, float deltaTime ) override;
    bool onMessage( FlashMessage *pMessage, FlashPlayer *pPlayer ) override;

private:
    void show();
    void hide();
    void setClipVisible( const char *pPath, const bool bShow );
    void setListHidden( const bool bHidden );
    void resizePackHighlight();
    void showPanes( const bool bAnimate );
    void updatePaneSequence();
    int32_t getRootFrame();
    void setScreenActive( const bool bActive );
    bool isFrontEndScreenUp();
    void setInputExclusive( const bool bExclusive );
    void rebuildPackRows();
    void rebuildDetailRows();
    void toggleSelectedPack();
    void applyChanges();

private:
    enum ePaneSequence : uint8_t {
        PS_Hidden,
        PS_PackSliding,
        PS_DetailSliding,
        PS_Visible,
    };

private:
    std::vector<ModsListRow> packRows;
    std::vector<ModsListRow> detailRows;

    FlashListMods packList;
    FlashListMods detailList;

    FlashPlayer *pMovie;
    int32_t autoOpenTicks;
    int32_t numOpenLists;
    int32_t paneSequenceTicks;
    ePaneSequence paneSequence;
    uint8_t bInputExclusive : 1;
    uint8_t bListHidden : 1;
    uint8_t bListKeyLatched : 1;
    uint8_t bPaneKeyLatched : 1;
    uint8_t bPanesPlaced : 1;
    uint8_t bAnimationDone : 1;
    uint8_t bSuspended : 1;
    uint8_t bTickSeen : 1;
    uint8_t bVisible : 1;
    uint8_t bBound : 1;
    uint8_t bToggleLatched : 1;
};

extern MngModsUI *gpModsUI;
