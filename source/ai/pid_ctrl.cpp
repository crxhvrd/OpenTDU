#include "shared.h"
#include "pid_ctrl.h"

static constexpr float kMinDeltaTime = 1.0e-5f;      // DAT_00f41710
static constexpr float kErrorDeadZone = 1.0e-4f;     // DAT_00f41760
static constexpr float kTimeCorrectionScale = 0.04f; // DAT_00f41980

static float EvalFunc( const PIDCtrl::FuncDef &func, const float x ) {
    // FUN_00907460
    switch ( func.Type ) {
    case 0:
        return func.A * x + func.B;
    case 1:
        return func.A * x * x + func.B;
    case 2:
        return ( x < 0.0f ) ? func.B - func.A * x * x : func.A * x * x + func.B;
    case 3:
        return func.A * x * x * x + func.B;
    case 4:
        return func.A * cosf( x ) + func.B;
    case 5: {
        const float result = func.A * cosf( x ) + func.B;
        return ( x < 0.0f ) ? -result : result;
    }
    case 6:
        return func.A * sinf( x ) + func.B;
    default:
        return x;
    }
}

PIDCtrl::PIDCtrl()
    : type( PT_ErrorDerivative ),
      minOutput( 0.0f ),
      maxOutput( 1.0f ),
      history(),
      historyCount( 0 ),
      historyFront( -1 ),
      historyBack( 0 ),
      maxValues( 0 ),
      integralSum( 0.0f ),
      integralTime( 0.0f ),
      currentIntegralTime( 0.0f ),
      errorProportional( 0.0f ),
      errorIntegral( 0.0f ),
      errorDerivative( 0.0f ),
      funcP(),
      funcI(),
      funcD(),
      coefP( 0.0f ),
      coefI( 0.0f ),
      coefD( 0.0f ),
      currentValue( 0.0f ),
      wantedValue( 0.0f ),
      output( 0.0f ),
      bInitialized( false ),
      bTimeCorrection( false ) {
    // FUN_009078f0
}

void PIDCtrl::initialize( const float min,
                          const float max,
                          const float newIntegralTime,
                          const bool bNewTimeCorrection,
                          const int32_t newMaxValues ) {
    // FUN_00907370
    minOutput = min;
    maxOutput = max;
    maxValues = newMaxValues;
    integralTime = newIntegralTime;
    history.assign( static_cast<size_t>( newMaxValues ),
                    ErrorVal{ 0.0f, 0.0f } );
    clearHistory();
    bTimeCorrection = bNewTimeCorrection;
    bInitialized = true;
    integralSum = 0.0f;
    currentIntegralTime = 0.0f;
    errorProportional = 0.0f;
    errorIntegral = 0.0f;
    errorDerivative = 0.0f;
    currentValue = 0.0f;
    wantedValue = 0.0f;
    output = 0.0f;
}

void PIDCtrl::setCoefs( const float p, const float i, const float d ) {
    // FUN_00907300
    coefP = p;
    coefI = i;
    coefD = d;
}

void PIDCtrl::clearHistory() {
    historyCount = 0;
    historyFront = maxValues - 1;
    historyBack = 0;
}

void PIDCtrl::reset( const bool bClearHistory ) {
    // FUN_00907320
    integralSum = 0.0f;
    currentIntegralTime = 0.0f;
    errorProportional = 0.0f;
    errorIntegral = 0.0f;
    errorDerivative = 0.0f;
    currentValue = 0.0f;
    wantedValue = 0.0f;
    output = 0.0f;
    if ( bClearHistory ) {
        clearHistory();
    }
}

void PIDCtrl::resetWithType( const int32_t newType ) {
    // FUN_009078a0
    reset( true );
    type = newType;
}

void PIDCtrl::release() {
    // FUN_00907870
    bInitialized = false;
    history.clear();
    historyCount = 0;
    maxValues = 0;
}

void PIDCtrl::update( const float current,
                      const float wanted,
                      const float dt ) {
    // FUN_009077f0
    if ( !bInitialized || dt < kMinDeltaTime ) {
        return;
    }

    switch ( type ) {
    case PT_ErrorDerivative:
        updateErrorDerivative( current, wanted, dt );
        break;
    case PT_MeasureDerivative:
        updateMeasureDerivative( current, wanted, dt );
        break;
    case PT_Raw:
        updateRaw( current, wanted, dt );
        break;
    default:
        break;
    }
}

void PIDCtrl::updateIntegral( const float dt ) {
    // FUN_009071d0
    const float error = currentValue - wantedValue;
    while ( historyCount == maxValues ||
            currentIntegralTime + dt > integralTime ) {
        if ( historyCount == 0 ) {
            currentIntegralTime = 0.0f;
            integralSum = 0.0f;
            break;
        }
        const int32_t oldestIndex = ( historyFront + 1 ) % maxValues;
        const ErrorVal &oldest = history[oldestIndex];
        currentIntegralTime -= oldest.Dt;
        integralSum -= oldest.Error * oldest.Dt;
        historyFront = oldestIndex;
        historyCount--;
    }

    if ( historyCount < maxValues ) {
        history[historyBack] = ErrorVal{ error, dt };
        historyBack = ( historyBack + 1 ) % maxValues;
        historyCount++;
    }
    currentIntegralTime += dt;
    integralSum += error * dt;
    errorIntegral = ( currentIntegralTime > 0.0f )
                        ? integralTime / currentIntegralTime * integralSum
                        : 0.0f;
}

float PIDCtrl::combine() {
    float value = EvalFunc( funcI, errorIntegral ) * coefI +
                  EvalFunc( funcD, errorDerivative ) * coefD +
                  EvalFunc( funcP, errorProportional ) * coefP;
    if ( minOutput > value ) {
        value = minOutput;
    } else if ( value > maxOutput ) {
        value = maxOutput;
    }
    output = value;
    return value;
}

void PIDCtrl::updateErrorDerivative( const float current,
                                     const float wanted,
                                     const float dt ) {
    // FUN_009076d0
    currentValue = current;
    wantedValue = wanted;
    errorProportional = current - wanted;
    if ( errorProportional < kErrorDeadZone &&
         errorProportional > -kErrorDeadZone ) {
        errorProportional = 0.0f;
    }

    if ( historyCount >= 1 ) {
        const int32_t lastIndex = ( historyBack - 1 + maxValues ) % maxValues;
        errorDerivative = ( errorProportional - history[lastIndex].Error ) / dt;
    } else {
        errorDerivative = 0.0f;
    }

    updateIntegral( dt );
    if ( bTimeCorrection ) {
        errorProportional = errorProportional / dt * kTimeCorrectionScale;
    }
    combine();
}

void PIDCtrl::updateMeasureDerivative( const float current,
                                       const float wanted,
                                       const float dt ) {
    // FUN_009075f0
    errorProportional = current - wanted;
    errorDerivative =
        ( historyCount >= 1 ) ? ( current - currentValue ) / dt : 0.0f;
    currentValue = current;
    wantedValue = wanted;
    updateIntegral( dt );
    errorIntegral /= integralTime;
    combine();
}

void PIDCtrl::updateRaw( const float current,
                         const float wanted,
                         const float dt ) {
    // FUN_00907520
    errorProportional = current;
    errorDerivative = ( current - currentValue ) / dt;
    currentValue = current;
    wantedValue = wanted;
    updateIntegral( dt );
    errorIntegral /= integralTime;
    combine();
}
