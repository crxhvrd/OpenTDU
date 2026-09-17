#pragma once

#include "game/gs_base.h"

#include "core/color.h"
#include "render/file_collection_2d.h"
#include "render/file_collection_3d.h"

class StreamingManager;
class Render2DM;
class Render2DB;

enum eGroundColorFlags : uint32_t {
    // Sample the ground colour map (heightmap layer 0).
    GCF_Color = 1 << 0,
    // Sample the ground shade / AO map (heightmap layer 1).
    GCF_Shade = 1 << 1,
    // Sample the ground type map instead, and return its red channel as a
    // greyscale (gated on green > 0.5). No port caller uses this yet.
    GCF_Type = 1 << 6,
    // Read GCF_Shade as full RGB. When clear, the shade map's RED channel is
    // replicated across RGB.
    GCF_ShadeRGB = 1 << 7,
};

class GSWorld : public GameSystem {
public:
    const char *getName() const override { return "Service : World"; }

    inline StreamingManager *getStreamingManager() const {
        return pStreamingManager;
    }
    inline Eigen::Vector4f *getStreamPosition() const {
        return pStreamPosition;
    }
    inline Eigen::Vector4f &getStaticStreamPosition() {
        return staticStreamPosition;
    }
    inline const Matrix4x4f_t &getPreviousTransform() const {
        return previousTransformMatrix;
    }
    inline void setPreviousTransform( const Matrix4x4f_t &transform ) {
        previousTransformMatrix = transform;
    }
    inline bool isAreaStreamingEnabled() const { return bAreaStreamingEnabled; }
    inline void setAreaStreamingEnabled( const bool value ) {
        bAreaStreamingEnabled = value;
    }
    inline void setDirtyStreaming( const bool value ) {
        bDirtyStreaming = value;
    }

    inline FileCollection3D &getCollection3D() { return collection3D; }

public:
    GSWorld();
    ~GSWorld();

    bool isWorldLoaded() const;
    bool initialize( TestDriveGameInstance *pGameInstance ) override;
    void tick( float totalTime, float deltaTime ) override;
    void terminate() override;
    void draw( float deltaTime ) override;
    void pause() override;
    void reset() override;

#ifdef OTDU_USE_IMGUI
    void drawDebug( float deltaTime ) override;
#endif

    void pushGlobal2DM( Render2DM *pRender2DM );
    void pushGlobal2DB( Render2DB *pRender2DB );

    void removeGlobal2DB( Render2DB *pRender2DB );
    void removeGlobal2DM( Render2DM *pRender2DM );

    void bindWorldBitmapReferences( Render2DM *pRender2DM );

    void setEnabled( bool param_1 );

    void setPlayerPosition( const Eigen::Vector4f *pPosition );
    void setStreamingPosition( Eigen::Vector4f *pPosition );
    void setStreamingPositionPtr( Eigen::Vector4f *pPosition );

    bool computeGroundColor( Eigen::Vector4f *pOutColor,
                             const Eigen::Vector4f *pWorldPos,
                             const uint32_t flags );
    bool computeGroundColorPacked( ColorRGBA *pOutColor,
                                   const Eigen::Vector4f *pWorldPos,
                                   const uint32_t flags );

private:
    StreamingManager *pStreamingManager;

    FileCollection2D collection2D;
    FileCollection3D collection3D;

    Eigen::Vector4f *pStreamPosition;
    Eigen::Vector4f staticStreamPosition;
    Matrix4x4f_t previousTransformMatrix;
    Eigen::Vector4f camera[3];

    uint8_t bEnabled : 1;
    uint8_t bForceUpdate : 1;
    uint8_t bDirtyStreaming : 1;
    uint8_t bAreaStreamingEnabled : 1;
    uint8_t bIsCameraDirty : 1;
};

extern GSWorld *gpWorld;
