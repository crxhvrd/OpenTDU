#pragma once

#include "shared.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "filesystem/bank.h"
#include "flash/bank/flash_bank_resources.h" // FlashMaterial (needle scratch)
#include "game/mng_base.h"
#include "game/mng_gps.h"
#include "render/2dm.h"
#include "render/draw_list.h"

class CarState;
class FlashPlayer;
class PlayerGameMech;
class Render2DB;
class FlashResource;
struct FlashRECT;

struct HudGaugeFace {
    float X = 0.0f;      // left
    float Y = 0.0f;      // top
    float Width = 0.0f;  // width
    float Height = 0.0f; // height
    // Atlas UV scale
    float UvU = 0.0f;
    float UvV = 0.0f;
    Material *pMaterial = nullptr; // 2DM chunk (RPM or speed)
};

// Piecewise-linear value -> clip-rotation driver used for the gauge needles
// and the minimap cursor.
// An optional midpoint splits the mapping in two segments (MidValue < 0
// disables it).
struct HudFlashGauge {
    FlashPlayer *pPlayer = nullptr;
    char pClipPath[64] = { '\0' };
    float MinValue = 0.0f;
    float MaxValue = 0.0f;
    float MinAngle = 0.0f;
    float MaxAngle = 0.0f;
    float MidValue = -1.0f;
    float MidAngle = -1.0f;
    float CurrentValue = 0.0f;
    float CurrentAngle = 0.0f;
    bool bUnknown = false;

    void update( float value );
};

// Clamped integer pushed to a single flash variable, optionally left-padded
// with zeroes
struct HudFlashNumber {
    FlashPlayer *pPlayer = nullptr;
    char pVarPath[64] = { '\0' };
    int32_t MinValue = 0;
    int32_t MaxValue = 0;
    int32_t CurrentValue = 0;
    bool bPadWithZeroes = false;
    int32_t NumDigits = 0;

    void update( int32_t value );
};

// Clamped integer split into one flash variable per digit, units digit first
// Drives the digital speed readout.
struct HudFlashDigits {
    FlashPlayer *pPlayer = nullptr;
    char pDigitVarPaths[4][64] = { { 0 } };
    int32_t MinValue = 0;
    int32_t MaxValue = 0;
    int32_t CurrentValue = 0;
    bool bUnknown = false;
    int32_t NumDigits = 0;

    void update( int32_t value );
};

// Scrolling text log linked to a flash variable ("/:Log"); messages are
// concatenated in pBuffer and the oldest line is dropped every ScrollPeriod
// seconds.
struct HudMessageLog {
    static constexpr int32_t kMaxNumLines = 3;
    static constexpr size_t kBufferSize = 0x300;

    FlashPlayer *pPlayer = nullptr;
    char pVarPath[64] = { '\0' };
    float CurrentTime = -1.0f;
    float LastScrollTime = -1.0f;
    float ScrollPeriod = 30.0f;
    char *pLines[kMaxNumLines] = {};
    char pBuffer[kBufferSize] = { '\0' };
    int32_t NumLines = kMaxNumLines;

    HudMessageLog();

    void link();
    void scroll();
};

class HudMinimap {
public:
    static constexpr int32_t kNumMapChunks = 5;
    static constexpr int32_t kTraceCapacity = 300;

public:
    // View rect for one zoom level, in absolute world units + map UV.
    struct MapViewRect {
        float UMin = 0.0f;
        float VMin = 0.0f;
        float UMax = 1.0f;
        float VMax = 1.0f;
        float CursorU = 0.0f;
        float CursorV = 0.0f;
        float XMin = 0.0f;
        float XMax = 0.0f;
        float YMin = 0.0f;
        float YMax = 0.0f;
    };

public:
    FlashPlayer *pPlayer = nullptr;
    char pClipPath[64] = { '\0' };
    MngGPS *pGps = nullptr;
    PlayerGameMech *pGameMech = nullptr;
    int32_t MapMode = 3;
    int32_t MapModeTarget = 3;
    float RotationBlend = 1.0f;
    float LureX = 0.0f;
    float LureY = 0.0f;
    float LureWidth = 1.0f;
    float LureHeight = 1.0f;
    float CursorScaleX = 256.0f;
    float CursorScaleY = 256.0f;
    float ViewportU = 1.0f;
    float ViewportV = 1.0f;
    float ViewXMin = 0.0f;
    float ViewXMax = 0.0f;
    float ViewYMin = 0.0f;
    float ViewYMax = 0.0f;
    float ViewRadius = 0.0f;
    float ViewRadiusSq = 0.0f;
    Eigen::Vector4f ViewCenter = Eigen::Vector4f( 0.0f, 0.0f, 0.0f, 1.0f );
    MapViewRect BlendedUV;
    float LogoTimer = 0.0f;
    bool bLogoTick = false;

    float TraceMinDistanceSq = 256.0f; // DAT_00f419c0
    std::vector<Eigen::Vector2f> TracePoints;
    int32_t TraceCount = 0;
    int32_t TraceRead = 0;
    int32_t TraceWrite = 0;

    bool bLureCallbackRegistered = false;
    bool bLureVisible = false;
    bool bGpsTextureBound = false;

    Matrix4x4f_t WorldToMap = Matrix4x4f_t::Identity();
    Matrix4x4f_t MapRotation = Matrix4x4f_t::Identity();

    Render2DM mapRender2DMs[kNumMapChunks];
    void *pMapRender2DMs[kNumMapChunks] = {};
    Material *pMapMaterials[kNumMapChunks] = {};

    bool bInstalled = false;
    DrawList *pDrawList = nullptr;
    DrawList drawList;

public:
    HudMinimap();
    virtual ~HudMinimap();

    void initialize();
    void tick( float deltaTime );
    void terminate();

    virtual bool updateLure();

public:
    static void LureDrawCallback( FlashRECT *pViewport,
                                  float depth,
                                  DrawList *pDrawList,
                                  void *pUser,
                                  FlashResource *pResource,
                                  const char *pClipName );

private:
    void refreshLureRect();
    void bindGpsTextureToPlayerCar();
    void appendTracePoint();
    void drawTrace();
    void buildMapDrawList();
    void updateViewTransform( float &cursorU, float &cursorV );
    void buildViewMatrix( Matrix4x4f_t &out, bool bWorldSpace );
    void drawMapQuad( float u0,
                      float v0,
                      float u1,
                      float v1,
                      float x0,
                      float y0,
                      float x1,
                      float y1 );

private:
    static void ComputeMapViewRect( int32_t mapMode, MapViewRect &out );
    static void GetMapModeHalfExtent( int32_t mapMode, float &x, float &y );
    static bool GetWorldExtent( float &width, float &height );
};

class HudGaugesBank : public Bank {
public:
    inline bool isReady() const { return state == GBS_Ready; }

public:
    HudGaugesBank();
    ~HudGaugesBank();

    void load( const char *pFilename, void *pFileBuffer, float screenWidth );

    void loadContent() override;
    void release();
    Material *getGaugeMaterial( const char *pName );

private:
    void clampGaugeMaterials();
    void bindGaugeMaterials();
    void teardownRenderObjects();

private:
    enum eGaugesBankState : int32_t {
        GBS_Idle = 0,    // no bank held
        GBS_Loading = 1, // stream requested
        GBS_Ready = 2,   // render objects built
    };

    int32_t state = GBS_Idle;
    bool b2DMInitialized = false;
    Render2DM render2DM;
    std::vector<std::unique_ptr<Render2DB>> render2DBs;
};

// Speed needle calibration set
struct HudGaugeRange {
    float MinValue = 0.0f;
    float MaxValue = 0.0f;
    float MinAngle = 0.0f;
    float MaxAngle = 0.0f;
    float MidValue = 0.0f;
    float MidAngle = 0.0f;
};

struct HudObjectiveMarker {
    Eigen::Vector4f Position = Eigen::Vector4f::Zero();
    const char *Type = nullptr;
};

class MngHud : public Manager {
    friend class HudGaugesBank;
    friend class HudMinimap;

public:
    using MovieFilterCallback_t = bool ( * )( const char *pMovieName );

public:
    static constexpr int32_t kMaxObjectiveMarkers = 8;

public:
    const char *getName() const override {
        // FUN_0042b760
        return "Hud";
    }

    inline void setEnabled( const bool value ) { bEnabled = value; }
    inline void setGPSRotation( const bool value ) { bGPSRotation = value; }
    inline FlashPlayer *getHudPlayer() { return pHudPlayer; }
    inline void setGameMech( PlayerGameMech *pMech ) {
        minimap.pGameMech = pMech;
    }

    inline float getMinimapViewportU() const { return minimap.ViewportU; }
    inline float getMinimapViewportV() const { return minimap.ViewportV; }

public:
    MngHud();
    ~MngHud();

    bool initialize( TestDriveGameInstance * ) override;
    void tick( float deltaTime, float totalTime ) override;
    void terminate() override;
    void draw() override;
    void pause() override;

    void updateUnitSystem();
    void FUN_006e6a00( CarState *param_2 );
    void cycleMinimapZoom();

    void setObjectiveMarker( const int32_t slot,
                             const Eigen::Vector4f &pPosition,
                             const char *pType );

    virtual void initializeFlash();

    static void GaugeRpmFaceCallback( FlashRECT *pViewport,
                                      float depth,
                                      DrawList *pDrawList,
                                      void *pUser,
                                      FlashResource *pResource,
                                      const char *pClipName );
    static void GaugeSpeedFaceCallback( FlashRECT *pViewport,
                                        float depth,
                                        DrawList *pDrawList,
                                        void *pUser,
                                        FlashResource *pResource,
                                        const char *pClipName );

private:
    void loadCarGauges( const char *pFilename );
    void installCarGauges();
    void unloadCarGauges();
    void dropGaugeFaceCallbacks();
    void parseGaugeIni( const char *pFile, uint32_t fileSize );
    void applyGaugeStyle( const char *pArgs );
    void applyGaugeHudRange( const char *pArgs );

    void showCustomGauges();
    void drawSimulationOverlay();

    void installGaugeFaceCallbacks( bool bInterior );
    void FUN_006ed810();

    void setCarName( const char *pName );
    void clearCarName();
    void clearPaintName();

protected:
    HudFlashGauge extNeedle;
    HudFlashGauge rpmNeedle;
    HudFlashGauge speedNeedle;
    HudGaugeRange speedRangeImperial;
    HudGaugeRange speedRangeMetric;
    float screenWidth;
    CarState *pCarState;
    int32_t carDBID;
    FlashPlayer *pHudPlayer;
    FlashPlayer *pGaugesPlayer;
    bool bEnabled;
    bool bUnknown19D;
    HudFlashDigits speedDigits;
    HudFlashNumber odometer;
    HudFlashNumber odometerDecimal;
    HudFlashGauge minimapCursor;
    HudMinimap minimap;
    int32_t unknown11F0[3];
    bool bUnknown11FC;
    MngGPS gps;
    HudMessageLog messageLog;
    float carNameTimer;
    float paintNameTimer;
    float compassAngle;
    float minimapHeading;
    bool bGPSRotation;
    bool bDisplayTrace;       // gates the minimap breadcrumb trail
    bool bGaugeClipCallbackB; // interior dial-face callbacks registered
    bool bGaugeClipCallbackA; // exterior dial-face callbacks registered
    HudGaugeFace rpmFace;
    HudGaugeFace speedFace;
    int32_t gaugesState;
    bool bNeedleBackupDone;
    FlashMaterial needleMaterialBackup;
    FlashMaterial needleMaterialActive;
    int32_t gaugeSyncFrameExt;
    int32_t gaugeSyncFrameInt;
    HudGaugesBank gaugesBank;
    int32_t customGaugesMode;
    void *pGaugesFileBuffer;
    MovieFilterCallback_t pMovieFilter;

    HudObjectiveMarker objectiveMarkers[kMaxObjectiveMarkers];
};

class MngHudChallenge : public MngHud {
public:
    MngHudChallenge();
    ~MngHudChallenge();

    void initializeFlash() override;

private:
    HudFlashNumber timerMinutes;
    HudFlashNumber timerSeconds;
    HudFlashNumber timerCents;
    HudFlashNumber countdownMinutes;
    HudFlashNumber countdownSeconds;
    HudFlashNumber countdownCents;
};

void DrawGaugeQuad( float u0,
                    float v0,
                    float u1,
                    float v1,
                    float xLeft,
                    float yTop,
                    float xRight,
                    float yBottom,
                    FlashResource *pResource );

extern MngHud *gpMngHud;

extern bool gEnableMinimapDensity;
extern bool gEnableMinimapAI;
extern bool gEnableMinimapNetPlayers;
