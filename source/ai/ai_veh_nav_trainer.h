#pragma once

#include "ai/ai_fsm.h"
#include "ai/ai_veh_nav_traffic.h"

class AiVehicle;
class CarState;

// Navigation type 3: measurement sequences driven by GMAITrainer
class AiVehNavTrainer : public AiVehNavigation, private AiFsm {
public:
    enum eSequence : int32_t {
        SEQ_Nothing = 0,
        SEQ_Sustain1 = 1,
        SEQ_Sustain2 = 2,
        SEQ_BrakeH1 = 3,
        SEQ_BrakeH2 = 4,
        SEQ_SpeedGrad1 = 5,
        SEQ_SpeedGrad2 = 6,
        SEQ_TurningSpeed = 7,
        SEQ_TurningSpeed1 = 8,
        SEQ_SteerAlt1 = 9,
        SEQ_SteerAlt2 = 10,
        SEQ_SteerCircle = 11,
        SEQ_Accel = 12,
        SEQ_CmpLocoPhysic = 13,
        SEQ_CmpLocoManual = 14,
        SEQ_StopDist = 15,
        SEQ_VehPlacement = 16,
        SEQ_CmpLoco2Physic = 17,
        SEQ_CmpLoco2Manual = 18,
        SEQ_Error = 19,
    };

    struct Sample {
        Eigen::Vector4f Position;
        Eigen::Vector4f Velocity;
        float Time;
    };

    // Trajectory recorded during the loco comparison sequences
    struct SampleTrack {
        std::vector<Sample> Samples;
        float Interval = 0.1f;
        float LastTime = -1.0f;

        void clear();
        void record( CarState *pCarState, const float time );
    };

public:
    inline int32_t getSequence() const { return getCurrentState(); }
    inline bool isDone() const { return getCurrentState() == SEQ_Nothing; }
    inline bool isFailed() const { return getCurrentState() == SEQ_Error; }
    inline bool isShowDebug() const { return bShowDebug; }
    inline void setShowDebug( const bool bShow ) { bShowDebug = bShow; }

    inline float getBrakeTime() const { return stepTime; }
    inline float getBrakeDistance() const { return distance; }
    inline float getTurnMaxSpeed() const { return result0; }
    inline float getAccelMaxSpeed() const { return result0; }
    inline float getAccelMean() const { return result1; }
    inline const SampleTrack &getTrack( const int32_t index ) const {
        return tracks[index];
    }

    void getAccelResults( float &outMaxSpeed, float &outMeanAccel ) const;
    void getTurnResult( float &outMaxSpeed ) const;
    void getBrakeResults( float &outTime, float &outDistance ) const;

public:
    AiVehNavTrainer();

    void attach( AiVehicle *pVehicle ) override;
    void reset( const bool bFull ) override;
    void tick( const float deltaTime, const float totalTime ) override;
    void detach() override;
    void debugDraw( DrawList *pDrawList ) override;
    bool getTarget( Eigen::Vector4f &outDirection,
                    float &outSpeed ) const override;
    void getHeightAndTwist( float &outHeight, float &outTwist ) const override;

    void drawDebugText() const;

    void startSustainSpeed( const Matrix4x4f_t &start,
                            const float speed,
                            const float duration );
    void startBrakeTest( const Matrix4x4f_t &start,
                         const float speed,
                         const float stableTime );
    void startSpeedGradations( const Matrix4x4f_t &start,
                               const float maxSpeed,
                               const float step,
                               const float stableTime );
    void startTurnTest( const Matrix4x4f_t &start, const float radius );
    void startSteerAlternate( const Matrix4x4f_t &start,
                              const float speed,
                              const float angleDeg,
                              const float duration,
                              const int32_t count );
    void startSteerCircle( const Matrix4x4f_t &start,
                           const float offset,
                           const float radius,
                           const float maxSpeed,
                           const float duration );
    void startAccelTest( const Matrix4x4f_t &start,
                         const float startSpeed,
                         const float maxSpeed );
    void startCompareLocos( const Matrix4x4f_t &start,
                            const float lowSpeed,
                            const float highSpeed );
    void startStopDistance( const Matrix4x4f_t &start,
                            const float speed,
                            const float stopDistance );
    void startCompareLocos2( const Matrix4x4f_t &start,
                             const float speed,
                             const float totalTime,
                             const float period );

private:
    AiVehicle *pOwner;
    bool bShowDebug;
    int32_t sequenceAfterPlacement;
    float paramA;
    float paramB;
    float paramC;
    float paramD;
    int32_t paramCount;
    Matrix4x4f_t startMatrix;
    float startTime;
    float simTime;
    float slidingTime;
    float overSteerTime;
    float underSteerTime;
    float stallStartTime;
    float stallTime;
    float minSpeed;
    float maxSpeed;
    float meanSpeed;
    float instSpeed;
    float smoothSpeed;
    float instAccel;
    float smoothAccel;
    float stableStartTime;
    float stableTime;
    float stepStartTime;
    float stepTime;
    Eigen::Vector4f pointA;
    Eigen::Vector4f pointB;
    Eigen::Vector4f pointC;
    float distance;
    float result0;
    float result1;
    int32_t stepCount;
    SampleTrack tracks[2];
    float targetSpeed;
    AiVehNavTraffic::StopRamp stop;

private:
    bool handle( const int32_t event, const int32_t state ) override;

    void resetStats();
    void updateStats( const float deltaTime, const float totalTime );
    void placeVehicle( const Matrix4x4f_t &matrix, float speed );
    float accumulateStableTime();
    Eigen::Vector4f computeCircleDirection( const Eigen::Vector4f &center,
                                            const float radius ) const;
    float computeFlatRadiusError() const;
};
