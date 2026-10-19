#include "shared.h"
#include "ai_bot_personality.h"

#include "core/markup.h"
#include "core/random/bcpl.h"

static constexpr float kMinSteer = 0.02f; // DAT_00f417f0

static float ParseAttribFloat( const CMarkup &xml,
                               const char *pName,
                               const float fallback ) {
    const std::string value = xml.getAttrib( pName );
    return value.empty() ? fallback
                         : static_cast<float>( atof( value.c_str() ) );
}

static int32_t ParseAttribInt( const CMarkup &xml,
                               const char *pName,
                               const int32_t fallback ) {
    const std::string value = xml.getAttrib( pName );
    return value.empty() ? fallback : atoi( value.c_str() );
}

static void
ParseAttribBool( const CMarkup &xml, const char *pName, bool &bValue ) {
    const std::string value = xml.getAttrib( pName );
    if ( value.empty() || value == "false" ) {
        bValue = false;
    } else if ( value == "true" ) {
        bValue = true;
    }
}

static float ApplyFactorDeadZone( const float factor,
                                  const float minFactor,
                                  const float maxFactor,
                                  const float deadZone ) {
    if ( factor <= 1.0f ) {
        if ( minFactor > factor ) {
            return minFactor;
        }
        return std::min( factor, 1.0f - deadZone );
    }

    if ( 1.0f + deadZone > factor ) {
        return 1.0f + deadZone;
    }
    return std::min( factor, maxFactor );
}

static float SampleFactor( RandomGen &random,
                           const float minFactor,
                           const float maxFactor ) {
    return minFactor + ( maxFactor - minFactor ) * RandomUnit( random );
}

AiVehIndDPCPattern::AiVehIndDPCPattern() { reset(); }

void AiVehIndDPCPattern::reset() {
    // FUN_0040be60
    AvoidJumpFactorDeadZone = 0.0f;
    BrakingDistanceFactorDeadZone = 0.0f;
    CurveSpeedFactorDeadZone = 0.0f;
    RoadWidthFactorDeadZone = 0.0f;
    SteeringSecurityFactorDeadZone = 0.0f;
    bAvoidCollision = false;
    AvoidJumpFactorMin = 1.0f;
    AvoidJumpFactorMax = 1.0f;
    AvoidJumpLengthMin = 10.0f;
    AvoidJumpLengthMax = 30.0f;
    BetLevel = 1.0f;
    bBlockPlayer = false;
    BrakingDistFactorMin = 1.0f;
    BrakingDistFactorMax = 1.0f;
    ChaseDistMin = 500.0f;
    ChaseDistMax = 1000.0f;
    CurveSpeedFactorMin = 1.0f;
    CurveSpeedFactorMax = 1.0f;
    bKeepOneLane = false;
    FrontABSFactorMin = 1.0f;
    FrontABSFactorMax = 1.0f;
    RearABSFactorMin = 1.0f;
    RearABSFactorMax = 1.0f;
    MaxAccel = 1.0f;
    PathOptLevel = 1;
    bPreferLinearPath = false;
    bRespectRoadSign = false;
    bRespectWay = false;
    RoadWidthFactorMin = 1.0f;
    RoadWidthFactorMax = 1.0f;
    RoaringLevelMin = 0.3f;
    RoaringLevelMax = 1.0f;
    RPMToChangeGearIndex = 1;
    SteeringSecurityFactorMin = 1.0f;
    SteeringSecurityFactorMax = 1.0f;
    bTaunt = true;
    TractionControlFactorMin = 1.0f;
    TractionControlFactorMax = 1.0f;
    bWaitForPlayer = false;
}

AiVehIndDPC::AiVehIndDPC() { reset(); }

void AiVehIndDPC::reset() {
    // FUN_0090ddf0
    BotID = -1;
    pName[0] = '\0';
    BotSectorIndex = -1;
    Difficulty = 1;
    Personality = 2;
    DPCPattern.reset();
    AvoidJumpFactor = 1.0f;
    BrakingDistanceFactor = 1.0f;
    CurveSpeedFactor = 1.0f;
    FrontABSFactor = 1.0f;
    RearABSFactor = 1.0f;
    RoadWidthFactor = 1.0f;
    SteeringSecurityFactor = 1.0f;
    TCFactor = 1.0f;
    NumOutsideWanted = 0;
}

void AiVehIndDPC::randomizeFactors( RandomGen &random ) {
    // FUN_0090bb40
    const AiVehIndDPCPattern &p = DPCPattern;
    AvoidJumpFactor = ApplyFactorDeadZone(
        SampleFactor( random, p.AvoidJumpFactorMin, p.AvoidJumpFactorMax ),
        p.AvoidJumpFactorMin, p.AvoidJumpFactorMax, p.AvoidJumpFactorDeadZone );
    BrakingDistanceFactor = ApplyFactorDeadZone(
        SampleFactor( random, p.BrakingDistFactorMin, p.BrakingDistFactorMax ),
        p.BrakingDistFactorMin, p.BrakingDistFactorMax,
        p.BrakingDistanceFactorDeadZone );
    CurveSpeedFactor = ApplyFactorDeadZone(
        SampleFactor( random, p.CurveSpeedFactorMin, p.CurveSpeedFactorMax ),
        p.CurveSpeedFactorMin, p.CurveSpeedFactorMax,
        p.CurveSpeedFactorDeadZone );
    FrontABSFactor =
        SampleFactor( random, p.FrontABSFactorMin, p.FrontABSFactorMax );
    RearABSFactor =
        SampleFactor( random, p.RearABSFactorMin, p.RearABSFactorMax );
    RoadWidthFactor = ApplyFactorDeadZone(
        SampleFactor( random, p.RoadWidthFactorMin, p.RoadWidthFactorMax ),
        p.RoadWidthFactorMin, p.RoadWidthFactorMax, p.RoadWidthFactorDeadZone );
    SteeringSecurityFactor = ApplyFactorDeadZone(
        SampleFactor( random, p.SteeringSecurityFactorMin,
                      p.SteeringSecurityFactorMax ),
        p.SteeringSecurityFactorMin, p.SteeringSecurityFactorMax,
        p.SteeringSecurityFactorDeadZone );
    TCFactor = SampleFactor( random, p.TractionControlFactorMin,
                             p.TractionControlFactorMax );
}

float AiVehIndDPC::applySteerDeadZone( const float steer ) const {
    // FUN_0090dd80
    if ( NumOutsideWanted <= 0 ) {
        return steer;
    }
    if ( steer > 0.0f && steer < 1.0f && steer < kMinSteer ) {
        return kMinSteer;
    }
    if ( steer < 0.0f && steer > -1.0f && steer > -kMinSteer ) {
        return -kMinSteer;
    }
    return steer;
}

AiBotPersonalities::AiBotPersonalities() : patterns() {}

int32_t AiBotPersonalities::parse( const char *pDocument ) {
    // FUN_0090e000
    CMarkup xml( pDocument );
    if ( !xml.isWellFormed() ) {
        return 0;
    }
    if ( !xml.findElem( "AIBOTPERS" ) ) {
        return 0;
    }
    xml.intoElem();

    int32_t numPatterns = 0;

    for ( int32_t difficulty = 0; difficulty < kNumDifficulties;
          difficulty++ ) {
        for ( int32_t personality = 0; personality < kNumPersonalities;
              personality++ ) {
            for ( int32_t context = 0; context < kNumContexts; context++ ) {
                char pElemName[16];
                snprintf( pElemName, sizeof( pElemName ), "BotPers_%i_%i_%i",
                          difficulty, personality, context );
                if ( !xml.findElem( pElemName ) ) {
                    continue;
                }
                numPatterns++;

                AiVehIndDPCPattern &p =
                    patterns[difficulty][personality][context];
                ParseAttribBool( xml, "respect_way", p.bRespectWay );
                ParseAttribBool( xml, "keep_one_lane", p.bKeepOneLane );
                p.BrakingDistFactorMin = ParseAttribFloat(
                    xml, "braking_distance_factor_min", 1.0f );
                p.BrakingDistFactorMax = ParseAttribFloat(
                    xml, "braking_distance_factor_max", 1.0f );
                p.BrakingDistanceFactorDeadZone = ParseAttribFloat(
                    xml, "braking_distance_factor_deadzone", 0.0f );
                p.RoadWidthFactorMin =
                    ParseAttribFloat( xml, "road_width_factor_min", 1.0f );
                p.RoadWidthFactorMax =
                    ParseAttribFloat( xml, "road_width_factor_max", 1.0f );
                p.RoadWidthFactorDeadZone =
                    ParseAttribFloat( xml, "road_width_factor_deadzone", 0.0f );
                ParseAttribBool( xml, "respect_road_sign", p.bRespectRoadSign );
                p.CurveSpeedFactorMin =
                    ParseAttribFloat( xml, "curve_speed_factor_min", 1.0f );
                p.CurveSpeedFactorMax =
                    ParseAttribFloat( xml, "curve_speed_factor_max", 1.0f );
                p.CurveSpeedFactorDeadZone = ParseAttribFloat(
                    xml, "curve_speed_factor_deadzone", 0.0f );
                ParseAttribBool( xml, "avoid_collision", p.bAvoidCollision );
                ParseAttribBool( xml, "taunt", p.bTaunt );
                p.ChaseDistMin =
                    ParseAttribFloat( xml, "chase_distance_min", 1.0f );
                p.ChaseDistMax =
                    ParseAttribFloat( xml, "chase_distance_max", 1.0f );
                p.AvoidJumpFactorMin =
                    ParseAttribFloat( xml, "avoid_jump_factor_min", 1.0f );
                p.AvoidJumpFactorMax =
                    ParseAttribFloat( xml, "avoid_jump_factor_max", 1.0f );
                p.AvoidJumpFactorDeadZone =
                    ParseAttribFloat( xml, "avoid_jump_factor_deadzone", 0.0f );
                ParseAttribBool( xml, "prefer_linear_path",
                                 p.bPreferLinearPath );
                p.BetLevel = ParseAttribFloat( xml, "bet_level", 1.0f );
                p.RoaringLevelMin =
                    ParseAttribFloat( xml, "roaring_level_min", 1.0f );
                p.RoaringLevelMax =
                    ParseAttribFloat( xml, "roaring_level_max", 1.0f );
                p.SteeringSecurityFactorMin = ParseAttribFloat(
                    xml, "steering_security_factor_min", 1.0f );
                p.SteeringSecurityFactorMax = ParseAttribFloat(
                    xml, "steering_security_factor_max", 1.0f );
                p.SteeringSecurityFactorDeadZone = ParseAttribFloat(
                    xml, "steering_security_factor_deadzone", 0.0f );
                p.RPMToChangeGearIndex =
                    ParseAttribInt( xml, "rpm_to_change_gear_index", 1 );
                p.TractionControlFactorMin = ParseAttribFloat(
                    xml, "traction_control_factor_min", 1.0f );
                p.TractionControlFactorMax = ParseAttribFloat(
                    xml, "traction_control_factor_max", 1.0f );
                p.FrontABSFactorMin =
                    ParseAttribFloat( xml, "front_abs_factor_min", 1.0f );
                p.FrontABSFactorMax =
                    ParseAttribFloat( xml, "front_abs_factor_max", 1.0f );
                p.RearABSFactorMin =
                    ParseAttribFloat( xml, "rear_abs_factor_min", 1.0f );
                p.RearABSFactorMax =
                    ParseAttribFloat( xml, "rear_abs_factor_max", 1.0f );
                ParseAttribBool( xml, "wait_for_player", p.bWaitForPlayer );
                ParseAttribBool( xml, "block_player", p.bBlockPlayer );
                p.PathOptLevel = ParseAttribInt( xml, "path_opt_level", 1 );
                p.MaxAccel = ParseAttribFloat( xml, "max_accel", 1.0f );
            }
        }
    }

    xml.outOfElem();
    return numPatterns;
}

const AiVehIndDPCPattern *
AiBotPersonalities::getPattern( const int32_t difficulty,
                                const int32_t personality,
                                const int32_t context ) const {
    if ( difficulty < 0 || difficulty >= kNumDifficulties || personality < 0 ||
         personality >= kNumPersonalities || context < 0 ||
         context >= kNumContexts ) {
        return nullptr;
    }
    return &patterns[difficulty][personality][context];
}
