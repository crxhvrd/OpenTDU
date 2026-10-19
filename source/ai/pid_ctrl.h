#pragma once

// PID controller used by the AI
class PIDCtrl {
public:
    enum eType : int32_t {
        PT_ErrorDerivative = 0,
        PT_MeasureDerivative = 1,
        PT_Raw = 2,
    };

    struct FuncDef {
        int32_t Type = 0;
        float A = 1.0f;
        float B = 0.0f;
    };

public:
    inline float getOutput() const { return output; }
    inline float getErrorProportional() const { return errorProportional; }
    inline float getCurrentValue() const { return currentValue; }
    inline float getWantedValue() const { return wantedValue; }
    inline void setType( const int32_t newType ) { type = newType; }

public:
    PIDCtrl();

    void initialize( const float min,
                     const float max,
                     const float newIntegralTime,
                     const bool bNewTimeCorrection,
                     const int32_t newMaxValues );
    void setCoefs( const float p, const float i, const float d );
    void reset( const bool bClearHistory );
    void resetWithType( const int32_t newType );
    void release();
    void update( const float current, const float wanted, const float dt );

private:
    struct ErrorVal {
        float Error;
        float Dt;
    };

private:
    int32_t type;
    float minOutput;
    float maxOutput;
    std::vector<ErrorVal> history;
    int32_t historyCount;
    int32_t historyFront; // Index before the oldest sample
    int32_t historyBack;  // Next write
    int32_t maxValues;
    float integralSum;
    float integralTime;
    float currentIntegralTime;
    float errorProportional;
    float errorIntegral;
    float errorDerivative;
    FuncDef funcP;
    FuncDef funcI;
    FuncDef funcD;
    float coefP;
    float coefI;
    float coefD;
    float currentValue;
    float wantedValue;
    float output;
    bool bInitialized;
    bool bTimeCorrection;

private:
    void updateIntegral( const float dt );
    void updateErrorDerivative( const float current,
                                const float wanted,
                                const float dt );
    void updateMeasureDerivative( const float current,
                                  const float wanted,
                                  const float dt );
    void updateRaw( const float current, const float wanted, const float dt );
    float combine();
    void clearHistory();
};
