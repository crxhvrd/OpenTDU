#pragma once

struct AIZoneVehicle {
    float Frequency;
    int32_t DatabaseId;
};
OTDU_SIZE_MUST_MATCH( AIZoneVehicle, 0x8 );

struct AIZone {
    static constexpr int32_t kMaxNumVehicles = 34;

    AIZoneVehicle Vehicles[kMaxNumVehicles];
    float TrafficPerLaneKm;
};
OTDU_SIZE_MUST_MATCH( AIZone, 0x114 );

struct AIHardcodedZone {
    int32_t Sector[4];
    int32_t Zone;
};
OTDU_SIZE_MUST_MATCH( AIHardcodedZone, 0x14 );

struct AIDistribPoint {
    float Value;
    float Frequency;
};

class RandomGen;
struct AIHandlingReference;

struct AIDistrib {
    static constexpr int32_t kMaxNumPoints = 8;

    float Min;
    float Max;
    int32_t NumPoints;
    AIDistribPoint Points[kMaxNumPoints];

    float sample( RandomGen &random ) const;
};
OTDU_SIZE_MUST_MATCH( AIDistrib, 0x4c );

struct AIVehiclePhysics {
    float BrakingDistC1;
    float BrakingDistC2;
    float TurningSpeedC1;
    float TurningSpeedC2;
    float HandlingMark;
    int32_t DatabaseId;
    float LogMaxSpeed;
};
OTDU_SIZE_MUST_MATCH( AIVehiclePhysics, 0x1c );

struct AIConfig {
    static constexpr int32_t kMaxNumZones = 6;
    static constexpr int32_t kMaxNumHardcodedZones = 128;
    static constexpr int32_t kMaxNumVehiclePhysics = 256;

    int32_t MemorySize;
    int32_t MaxNumVehicle;
    int32_t MaxNumTrafficVehicle;
    int32_t MaxNumParkedVehicle;
    int32_t MaxNumPoliceVehicle;
    int32_t MaxNumPoliceDam;
    int32_t MaxNumBots;
    int32_t MaxNumBotsLimited;
    bool bPoliceCountFromIni; // Locks MaxNumPoliceVehicle/MaxNumPoliceDam
    float PoliceDeleteDistance;
    float PoliceSearchDistance;
    float PoliceCaptureDistance;
    float PoliceCaptureSpeed;
    float PoliceCaptureTime;
    bool bPoliceForceChase;
    float PoliceSpawnProba;
    float PoliceSearchSpawnProba;
    bool bPoliceForceDensity;
    float PoliceSearchMinTime;
    float PoliceSearchMinTimeStep;
    bool bPoliceCanBackOnTrack;
    bool bPoliceOutsideAllowed;
    bool bCaptureByTime;
    bool bCaptureBySpeed;
    bool bNeedSpeedAndTime;
    bool bPoliceCanCollide;
    bool bPoliceCanCollideFromBehind;
    bool bPoliceCanCollideFromAhead;
    bool bPoliceCanChaseFromAhead;
    bool Unknown0x59;
    bool bEnableJail;
    bool Unknown0x5B;
    float InfLimit;
    float InfACGraceDelay;
    float InfGracePeriod;
    float InfLoweringFactor;
    float InfCollisionValue;
    float InfSpeedLimitTolerance;
    float InfSpeedLimitFactor;
    float InfOncomingLaneFactor;
    float InfOncomingLaneValue;
    float InfRedLightValue;
    float InfStopValue;
    float InfYieldValue;
    float InfAllowedDistToStop;
    float InfAllowedSpeedToStop;
    float InfAllowedDurationOnControl;
    float InfYieldObsTimeToJunc;
    float InfCopMalusFactor;
    float InfPoliceDamLimit;
    float InfSecondCopLimit;
    float InfThirdCopLimit;
    float InfAggressiveLimit;
    float WitnessedOffenceTime;
    float WitnessedOffenceValue;
    bool bInfAllButCollision;
    bool Unknown0xB9;
    bool Unknown0xBA;
    bool Unknown0xBB;
    float CollisionRepeatDelay; // before a car/object collision counts again
    float GInfMaxValue;
    float GInfLoweringFactor;
    float GInfDestroyCar;
    int32_t FineCarValue;
    int32_t FineSetValue;
    float RecidiveTime;
    float OpponentDifficulty;
    int32_t NumOpponentsOverride;
    bool bAIReplacePlayer;
    bool bSameCarAsPlayer;
    bool bNavOppRespectLane;
    bool bTraceModeEnabled;
    bool Unknown0xE4;
    int32_t BotsWantedDifficulty;
    int32_t BotsWantedBehavior;
    bool bBotBehaviorOverride;
    float Unknown0xF4;
    float Unknown0xF8;
    float Unknown0xFC;
    bool bPathOptFixedNormals;
    float PathOptMinAngle;
    float PathOptTrackDistance;
    float PathOptForceFactor;
    float PathOptReduceFactor;
    float PathOptUncurveToExtractCoeff;
    float PathOptDamping;
    float PathOptCoeffMaxValue;
    int32_t PathOptNbPasses;
    int32_t PathOptMethod;
    int32_t PathOptMethodPower;
    float PathOptMinAmpFactor;
    float PathOptMaxAmpFactor;
    bool bPathOptEnableAmp;
    float PathOptSpeed[3];
    float PathOptCoeff[3];
    float Unknown0x150;
    float SpawnConfig[3];
    float Unknown0x160;
    float FadeInTime;
    float FadeOutTime;
    float SpawnDistances[3];
    int32_t NetTimeCorrectionMethod;
    float NetSendPeriod;
    float NetInterpolationTime;
    int32_t NetInterpolationMethod;
    float NetTimeout;
    float Unknown0x18C;
    float LocoManualMaxAccel;
    float LocoManualMaxBrake;
    float OpponentSharpTurnLimit;
    float OpponentSharpTurnMaxSpeed;
    float OpponentSharpTurnClampValue;
    float CullingFirstRadius;
    float CullingSecondRadius;
    float CullingFovDistance;
    float CullingFovMagnitude; // Degrees
    int32_t MaxNumPhysicsVehicle;
    float LODPeriod;
    float Unknown0x1BC;
    bool bEnableBots;

    AIZone Zones[kMaxNumZones];
    int32_t NumZones;

    AIHardcodedZone HardcodedZones[kMaxNumHardcodedZones];
    int32_t NumHardcodedZones;

    AIDistrib SpeedDistrib;
    AIDistrib SecDistDistrib;

    AIVehiclePhysics VehiclePhysics[kMaxNumVehiclePhysics];
    int32_t NumVehiclePhysics;

    uint32_t PedMaxToDisplay;
    float PedCollideSphereRadius;
    float PedCollideSphereDetectionRadius;
    float PedMinSpeed;
    float PedMaxSpeed;
    float PedMaxLength2Player;

    AIConfig();

    void reset();
    void parse( const char *pXml );
    int32_t serialize( char *pBuffer, const uint32_t bufferSize ) const;

    float computeMinTurnRadius() const;
    float computeMinBrakingDistance() const;
    float computeHandlingMark( const int32_t carID,
                               AIHandlingReference &reference ) const;

private:
    const AIVehiclePhysics *findVehiclePhysics( const int32_t carID ) const;
};
OTDU_SIZE_MUST_MATCH( AIConfig, 0x2ef8 );

struct AIHandlingReference {
    float MaxTopSpeed;
    float MinAcceleration;
    float MinTurnRadius;
    float MinBrakingDistance;
};

extern AIConfig gAIConfig;
extern AIHandlingReference gAIHandlingReference; // DAT_00fa9210

int32_t GetNextAvailableCarID( const int32_t carID );
float ComputeMaxTopSpeed();
float ComputeMinAcceleration();

static constexpr int32_t kFirstIteratedCarID = 0;

template <typename TFunctor>
static inline void ForEachAvailableCar( TFunctor functor ) {
    const int32_t firstCarID = GetNextAvailableCarID( kFirstIteratedCarID );
    if ( firstCarID == -1 ) {
        return;
    }

    int32_t carID = firstCarID;
    do {
        functor( carID );
        carID = GetNextAvailableCarID( carID );
    } while ( carID != firstCarID );
}
