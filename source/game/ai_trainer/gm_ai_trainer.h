#pragma once

#include "ai/ai_fsm.h"
#include "debug/debug_graph.h"
#include "game/gm_base.h"
#include "render/mng_number.h"
#include "world/mng_background.h"
#include "world/mng_world.h"

class AiVehicle;
class CarState;
class Device;
class DrawList;

// Dev mode measuring the AI driving model of every car (braking distances,
// cornering speeds, acceleration) on the flat test site south of Honolulu.
class GMAITrainer : public GameMode, public AiFsm {
public:
    GMAITrainer();
    ~GMAITrainer();

    void initialize() override;
    void reset() override;
    void tick( float deltaTime, float totalTime ) override;
    void draw( float deltaTime, float totalTime ) override;
    void destroy() override;

protected:
    bool handle( const int32_t event, const int32_t state ) override;

private:
    static constexpr int32_t kMaxNumRecords = 256;
    static constexpr int32_t kMaxNumCars = 256;
    static constexpr int32_t kMaxNumTestValues = 256;
    static constexpr int32_t kMaxNumSamples = 128;
    static constexpr int32_t kMaxNumAccelRuns = 8;
    static constexpr int32_t kNumStartMatrices = 3;

    enum eState : int32_t {
        S_Idle = 0,
        S_StartCampaign = 1,
        S_NextCar = 2,
        S_CampaignOver = 3,
        S_BrakeSetup = 4,
        S_BrakeRun = 5,
        S_BrakeNext = 6,
        S_TurnSetup = 7,
        S_TurnRun = 8,
        S_TurnNext = 9,
        S_AccelNextCar = 10,
        S_AccelRun = 11,
    };

    enum eCarFilter : int32_t {
        CF_AllCars = 0,
        CF_OnlyTraffics = 1,
        CF_OnlyBrands = 2,
        CF_OnlyBikes = 3,
        CF_OnlyBigVehicles = 4,
        CF_OnlySelected = 5,
        CF_FromSelected = 6,
        CF_Count
    };

    enum eGraphMode : int32_t {
        GRM_SpeedAccel = 0,
        GRM_LateralSlip = 1,
        GRM_Acceleration = 2,
        GRM_BrakeModel = 3,
        GRM_TurnModel = 4,
        GRM_Count
    };

    struct BrakeSample {
        bool bFailed = false;
        float Speed = 0.0f;
        float NavTime = 0.0f;
        float Distance = 0.0f;
        float Unknown = 0.0f;
        float TheoreticalDistance = 0.0f;
    };

    struct TurnSample {
        bool bFailed = false;
        float Radius = 0.0f;
        float MaxSpeed = 0.0f;
        float TheoreticalSpeed = 0.0f;
    };

    struct AccelRun {
        bool bFailed = false;
        float Result0 = 0.0f;
        float Result1 = 0.0f;
    };

    struct TrainerRecord {
        int32_t CarID = -1;
        BrakeSample BrakeSamples[kMaxNumSamples];
        int32_t NumBrakeSamples = 0;
        int32_t field_0xc08 = 0;
        float BrakeSpeedStep = 0.0f;
        float BrakeCoef1 = 0.0f;
        float BrakeCoef2 = 0.0f;
        float BrakeStdErr = 0.0f;
        TurnSample TurnSamples[kMaxNumSamples];
        float TurnCoef1 = 0.0f;
        float TurnCoef2 = 0.0f;
        float TurnStdErr = 0.0f;
        int32_t NumTurnSamples = 0;
        AccelRun AccelRuns[kMaxNumAccelRuns];
        int32_t NumAccelRuns = 0;
    };

private:
    MngWorld mngWorld;
    MngNumber mngNumber;
    MngBackground mngBackground;

    AiVehicle *pAiVehicle;
    CarState *pCarState;
    TrainerRecord *pCurrentRecord;
    int32_t carFilter;
    bool bBrakeTest;
    bool bTurnTest;
    float campaignStartTime;
    float carStartTime;
    float carEndTime;
    int32_t numFrames;
    float nextFpsUpdateTime;
    int32_t fps;
    int32_t carList[kMaxNumCars];
    int32_t numCars;
    int32_t currentCarIndex;
    float testValues[kMaxNumTestValues];
    int32_t numTestValues;
    int32_t testValueIndex;
    TrainerRecord records[kMaxNumRecords];
    int32_t numRecords;
    int32_t selectedRecord;
    Matrix4x4f_t startMatrices[kNumStartMatrices];
    int32_t numStartMatrices;
    float testStartTime;
    float lastGraphSampleX;
    bool bShowMenu;
    int32_t graphMode;
    DebugGraph graph;
    DrawList *pDrawList;

private:
    float getElapsedSeconds() const;

    TrainerRecord *findRecord( const int32_t carID );
    TrainerRecord *findRecordOrFirst( const int32_t carID );
    int32_t findRecordIndex( const int32_t carID ) const;

    void loadTrainerXml();
    void loadTrainerResults();
    void saveTrainerXml();
    void writeAIConfig();
    void dumpTurnResults();
    void dumpBrakeResults();
    void dumpAccelResults();

    void buildCarList( const int32_t filter );
    void buildTurnRadii();
    float buildBrakeSpeeds( const int32_t carID );

    bool spawnCar( const int32_t carID );
    void despawnCar();
    void stampCarEnd();

    void fitBrake( TrainerRecord *pRecord );
    bool fitTurn( TrainerRecord *pRecord );
    void fitAllBrakes();

    void startTurnCampaign();
    void startBrakeCampaign();
    void startAccelCampaign();
    void cycleCarFilter();
    void setGraphMode( const int32_t mode );

    void plotBrakeModel();
    void plotTurnModel();
    void updateFpsCounter();
    void drawHelpText();
    void drawStats();

    void processDebugKeys();
    void selectCar( const int32_t carIndex );
};
