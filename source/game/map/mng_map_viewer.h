#pragma once

#include "game/mng_base.h"

class CameraGame;
class FlashPlayer;
struct FlashMessage;
struct SpotMap;

struct MapView {
public:
    float MinSpeed;
    float MaxSpeed;
    bool bEnabled;
    bool bFocusMoved;
    bool bEdgeScrollActive;
    Eigen::Vector4f *pFocus;
    Eigen::Vector4f DefaultBounds; // (min x, min z, max x, max z)
    Eigen::Vector4f MediumBounds;
    Eigen::Vector4f CloseBounds;
    float InputX;
    float InputZ;
    Eigen::Vector4f MoveStart;
    Eigen::Vector4f MoveTarget;
    Eigen::Vector4f MoveVelocity;
    float MoveTime;
    float MoveDuration;
    Eigen::Vector4f DragAnchor;
    float DragVelocityX;
    float DragVelocityZ;

public:
    inline bool isMoving() const { return MoveTime >= 0.0f; }

public:
    MapView();

    void update( float deltaTime );
    void startMove( const Eigen::Vector4f &target, float duration );

private:
    void updateMove( float deltaTime );
    void computeEdgeScroll( float &outX,
                            float &outZ,
                            float minSpeed,
                            float maxSpeed );
    void screenToWorld( const Eigen::Vector4f &cursor,
                        Eigen::Vector4f &outPosition ) const;
};

extern MapView *gpMapView;

class MngMapViewer : public Manager {
public:
    enum eState : int32_t {
        MS_Idle = 0,
        MS_Active = 1,
        MS_Leaving = 2,
        MS_FadingOut = 3,
        MS_FadingIn = 4,
        MS_WaitingForGPSCamera = 5,
    };

public:
    inline void setState( const eState newState ) { state = newState; }
    inline eState getState() const { return state; }
    inline void setSyncSpotsFocus( const bool bSync ) {
        bSyncSpotsFocus = bSync;
    }
    inline const Eigen::Vector4f &getStreamFocus() const {
        return bPendingFocusMove ? pendingFocusTarget : focusPosition;
    }
    inline const Eigen::Vector4f &getFocus() const { return focusPosition; }
    inline bool isPlayerListOpen() const { return bPlayerListOpen; }
    inline void setFocus( const Eigen::Vector4f &position ) {
        focusPosition = position;
    }
    inline void setPendingFocusMove( const Eigen::Vector4f &target ) {
        bPendingFocusMove = true;
        pendingFocusTarget = target;
    }

public:
    MngMapViewer();
    ~MngMapViewer();

    void tick( float deltaTime, float totalTime ) override;
    void pause() override;
    void terminate() override;

    void setSpot( const SpotMap *pNewSpot, FlashPlayer *pPlayer );
    bool onFlashMessage( const FlashMessage *pMessage );

private:
    const SpotMap *pSpot;
    const SpotMap *pSelectedSpot;
    MapView mapView;
    bool bRestoreFilter;
    uint8_t savedFilter;
    int32_t savedSpotMask;
    Eigen::Vector4f focusPosition;
    bool bPendingFocusMove;
    Eigen::Vector4f pendingFocusTarget;
    bool bSyncSpotsFocus;
    bool bPlayerListOpen;
    eState state;
    FlashPlayer *pMapPlayer;
    CameraGame *pPreviousCamera;

private:
    void leave();
    void open();
    void close();
    void restoreSpotFilter();

    static void OnOpenFadedOut( void *pData, void *pSource );
    static void OnOpenFadedIn( void *pData, void *pSource );
    static void OnLeaveFadedOut( void *pData, void *pSource );
};

extern MngMapViewer *gpMngMapViewer;
