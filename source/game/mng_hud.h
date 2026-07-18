#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "filesystem/bank.h"
#include "flash/bank/flash_bank_resources.h" // FlashMaterial (needle scratch)
#include "game/mng_base.h"
#include "game/mng_gps.h"
#include "render/2dm.h"

class CarState;
class FlashPlayer;
class DrawList;
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
    HudMinimap();
    virtual ~HudMinimap();

    void initialize();
    void tick( float deltaTime );
    void terminate();

    virtual bool updateLure();

public:
    FlashPlayer *pPlayer = nullptr;
    char pClipPath[64] = { '\0' };
    MngGPS *pGps = nullptr;
    int32_t MapMode = 3;
    int32_t MapModeTarget = 3;
    float RotationBlend = 1.0f;
    bool bLureVisible = false;
    bool bInstalled = false;
    DrawList *pDrawList = nullptr;
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
    bool bUnknown3261;
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

extern MngHud *gpMngHud;

extern bool gEnableMinimapDensity;
extern bool gEnableMinimapAI;
extern bool gEnableMinimapNetPlayers;
