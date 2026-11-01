#pragma once

#include "game/mng_base.h"
#include "flash/entities/flash_list.h"
#include "flash/entities/flash_list_parameters.h"
#include "game/map/mng_trac_road.h"
#include "render/camera/camera_game.h"
#include "render/gs_render_helper.h"
#include "render/light.h"
#include "render/render_scene.h"
#include "render/setup_node.h"

class CameraGame;
class Entity;
class GMInstantChallenge;
struct Viewport;

// Editor cursor (GPS map pointer used to place checkpoints).
class MngCursor : public Manager {
    friend class CameraGME;

public:
    const char *getName() const override {
        // FUN_0042c6c0
        return "Cursor";
    }

    inline void setViewport( Viewport *pNewViewport ) {
        pViewport = pNewViewport;
    }
    inline void setCursorOff( const bool bOff ) { bCursorOff = bOff; }
    inline bool isKeyLeftDown() const { return bKeyLeft; }
    inline bool isKeyRightDown() const { return bKeyRight; }
    inline bool isKeyUpDown() const { return bKeyUp; }
    inline bool isKeyDownDown() const { return bKeyDown; }
    inline const Eigen::Vector4f &getWorldPosition() const {
        return worldPosition;
    }
    inline const Eigen::Vector4f &getScreenPosition() const {
        return screenPosition;
    }
    inline const Eigen::Vector4f &getGroundPosition() const {
        return groundPosition;
    }
    inline void setMargins( const Eigen::Vector4f &newMargins ) {
        margins = newMargins;
    }
    inline void setScreenPosition( const Eigen::Vector4f &position ) {
        screenPosition = position;
    }
    inline void setGroundPosition( const Eigen::Vector4f &position ) {
        groundPosition = position;
    }

public:
    MngCursor();
    ~MngCursor();

    bool initialize( TestDriveGameInstance * ) override;
    void tick( float deltaTime, float totalTime ) override;
    void reset() override;
    void draw() override;
    void pause() override;
    void terminate() override;

private:
    void updateEdgeFlags();
    void updateInput( const float deltaTime );
    void bindGPSCamera();

private:
    bool bKeyLeft;                  // +0x20
    bool bKeyRight;                 // +0x21
    bool bKeyUp;                    // +0x22
    bool bKeyDown;                  // +0x23
    Eigen::Vector4f worldPosition;  // +0x30
    Eigen::Vector4f screenPosition; // +0x40 (relative, y up)
    Eigen::Vector4f groundPosition; // +0x50
    bool bHasValidCamera;           // +0x60
    CameraGame *pActiveCamera;      // +0x64
    void *pCursorModels[2];         // +0x68
    Viewport *pViewport;            // +0x70
    bool bAtLeftEdge;               // +0x74
    bool bAtRightEdge;              // +0x75
    bool bAtBottomEdge;             // +0x76
    bool bAtTopEdge;                // +0x77
    bool bAtEdge;                   // +0x78
    bool bLockWorldPosition;        // +0x79
    Eigen::Vector4f margins;        // +0x7c (left, bottom, right, top)
    bool bCursorOff;                // +0x8c
};

// Checkpoint preview viewport (render setup + camera)
class MngPreview : public Manager {
public:
    const char *getName() const override {
        // FUN_0042c900
        return "Preview";
    }

    inline void clearEntity() { pPendingEntity = nullptr; }
    inline void setViewportRect( const float x,
                                 const float y,
                                 const float width,
                                 const float height,
                                 const float aspectRatio ) {
        viewport.X = x;
        viewport.Y = y;
        viewport.Width = width;
        viewport.Height = height;
        camera.copyAspectRatio( aspectRatio );
    }

public:
    MngPreview();
    ~MngPreview();

    bool initialize( TestDriveGameInstance * ) override;
    void tick( float deltaTime, float totalTime ) override;
    void reset() override;
    void draw() override;
    void pause() override;
    void terminate() override;

    void setEntity( Entity *pNewEntity );
    void hide();
    void show();
    inline bool isHidden() const { return bHidden; }

private:
    void refreshEntityBounds();

private:
    CameraGame camera;            // +0x20
    Viewport viewport;            // +0x190
    RenderScene scene;            // +0x1b0
    Eigen::Vector4f lightColor;   // +0x350
    AmbientLight light;           // +0x360
    LightSetupNode lightSetup;    // +0x380
    Entity *pDisplayedEntity;     // +0x390
    Entity *pPendingEntity;       // +0x394
    float cameraDistance;         // +0x398
    Eigen::Vector4f cameraTarget; // +0x3a0
    bool bHidden;                 // +0x3b0
    bool bBackgroundColorSet;     // +0x3b1
    bool bEntityDirty;            // +0x3b2
    float orbitAngle;             // +0x3b4
};

// GME flash interface.
class MngGMEFlshI : public Manager {
public:
    const char *getName() const override {
        // FUN_0042c8f0
        return "GMEFlshI";
    }

    inline void setOwner( GMInstantChallenge *pOwnerGM ) { pOwner = pOwnerGM; }

public:
    MngGMEFlshI();
    ~MngGMEFlshI();

    void draw() override;
    void drawInterface();

private:
    GMInstantChallenge *pOwner;
};

// Route ribbon between the edited checkpoints.
class MngTracRoadGME : public MngTracRoad {
public:
    inline void setOwner( GMInstantChallenge *pOwnerGM ) { pOwner = pOwnerGM; }

public:
    MngTracRoadGME();
    ~MngTracRoadGME();

    AiRoadLocation *getWaypoint( const int32_t index ) override;
    void setNumWaypoints( const int32_t numWaypoints ) override;
    bool isActive() const override;
    int32_t getNumWaypoints() const override;
    bool isLoop() const override;
    int32_t getLoopWaypointCount() const override;

private:
    GMInstantChallenge *pOwner;
};

// GME parameter list ("para" widget, /array).
class FlashListGMEParameters : public FlashListParameters {
public:
    inline void setOwner( GMInstantChallenge *pOwnerGM ) { pOwner = pOwnerGM; }
    inline void setEditorPlayer( FlashPlayer *pPlayer ) {
        setWidgetPlayer( pPlayer );
    }

public:
    FlashListGMEParameters();

    bool onMessage( FlashMessage *pMessage, FlashPlayer *pPlayer ) override;
    int32_t getListLength() const override;
    void open() override;
    GameParameter *getParameter( const int32_t index ) override;
    const char *applyClick( GameParameter *pParameter,
                            int32_t *pOutCheckBoxState ) override;
    void onParameterChanged() override;
    bool isNextStepLine( const int32_t index ) override;

private:
    void updateCategory();

private:
    GMInstantChallenge *pOwner; // +0xbc
    int32_t numNextStepLines;   // +0xc0
};

// Saved challenge list ("loch" widget, /array_list). vtbl 0xf32910
class FlashListGMEChallenges : public FlashList {
public:
    inline void setOwner( GMInstantChallenge *pOwnerGM ) { pOwner = pOwnerGM; }
    inline void setEditorPlayer( FlashPlayer *pPlayer ) {
        pEditorPlayer = pPlayer;
    }
    inline int32_t getSelectedIndex() const {
        return firstLine + selectedLineIndex;
    }

public:
    FlashListGMEChallenges();

    int32_t getListLength() const override;
    void open() override;
    void close() override;
    void sort( int32_t sortMode ) override;
    void update( const int32_t index, const int32_t lineIndex ) override;
    void selectedLineChanged() override;

private:
    void selectChallenge( const int32_t index );

private:
    FlashPlayer *pEditorPlayer; // +0x94
    GMInstantChallenge *pOwner; // +0x9c
};

extern MngCursor *gpMngCursor;
extern MngPreview *gpMngPreview;
