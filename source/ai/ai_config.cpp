#include "shared.h"
#include "ai_config.h"

#include "core/markup.h"
#include "core/random/bcpl.h"
#include "database/gs_database.h"

#include <cstdarg>

static_assert( offsetof( AIConfig, MaxNumPoliceVehicle ) == 0x10 );
static_assert( offsetof( AIConfig, bPoliceCountFromIni ) == 0x20 );
static_assert( offsetof( AIConfig, Unknown0x59 ) == 0x59 );
static_assert( offsetof( AIConfig, InfLimit ) == 0x5c );
static_assert( offsetof( AIConfig, bInfAllButCollision ) == 0xb8 );
static_assert( offsetof( AIConfig, OpponentDifficulty ) == 0xd8 );
static_assert( offsetof( AIConfig, bTraceModeEnabled ) == 0xe3 );
static_assert( offsetof( AIConfig, BotsWantedDifficulty ) == 0xe8 );
static_assert( offsetof( AIConfig, bPathOptFixedNormals ) == 0x100 );
static_assert( offsetof( AIConfig, bPathOptEnableAmp ) == 0x134 );
static_assert( offsetof( AIConfig, SpawnConfig ) == 0x154 );
static_assert( offsetof( AIConfig, SpawnDistances ) == 0x16c );
static_assert( offsetof( AIConfig, OpponentSharpTurnLimit ) == 0x198 );
static_assert( offsetof( AIConfig, CullingFirstRadius ) == 0x1a4 );
static_assert( offsetof( AIConfig, MaxNumPhysicsVehicle ) == 0x1b4 );
static_assert( offsetof( AIConfig, bEnableBots ) == 0x1c0 );
static_assert( offsetof( AIConfig, Zones ) == 0x1c4 );
static_assert( offsetof( AIConfig, NumZones ) == 0x83c );
static_assert( offsetof( AIConfig, HardcodedZones ) == 0x840 );
static_assert( offsetof( AIConfig, NumHardcodedZones ) == 0x1240 );
static_assert( offsetof( AIConfig, SpeedDistrib ) == 0x1244 );
static_assert( offsetof( AIConfig, SecDistDistrib ) == 0x1290 );
static_assert( offsetof( AIConfig, VehiclePhysics ) == 0x12dc );
static_assert( offsetof( AIConfig, NumVehiclePhysics ) == 0x2edc );
static_assert( offsetof( AIConfig, PedMaxToDisplay ) == 0x2ee0 );

AIConfig gAIConfig;                            // DAT_0143f720
AIHandlingReference gAIHandlingReference = {}; // DAT_00fa9210

static constexpr float kGravity = 9.81f;                 // DAT_00f419bc
static constexpr float kReferenceSpeed = 27.0f;          // DAT_00f420e0
static constexpr float kReferenceSpeedSquared = 729.0f;  // DAT_00f420e4
static constexpr float kDefaultReferenceValue = 1000.0f; // DAT_00f41718
static constexpr float kUnknownHandlingMark = -1.0f;     // DAT_00f41664
static constexpr float kHandlingMarkScale = 100.0f;      // DAT_00f41708
static constexpr float kTurnLinearEpsilon = 1.0e-4f;     // DAT_00f41760
static constexpr float kDiscriminantEpsilon = -0.001f;   // DAT_00f41a4c
static constexpr int32_t kSerializedVersion = 7;

static inline void ResetDistrib( AIDistrib &distrib ) {
    const float step = 1.0f / static_cast<float>( distrib.NumPoints - 1 );
    distrib.Min = 0.0f;
    distrib.Max = 1.0f;
    float value = 0.0f;
    for ( int32_t i = 0; i < distrib.NumPoints; i++ ) {
        distrib.Points[i].Value = value;
        distrib.Points[i].Frequency = 0.5f;
        value += step;
    }
}

static inline float ParseFloatAttrib( const CMarkup &xml,
                                      const char *pName,
                                      const float fallback ) {
    const std::string value = xml.getAttrib( pName );
    return value.empty() ? fallback
                         : static_cast<float>( atof( value.c_str() ) );
}

static void ParseDistrib( CMarkup &xml, AIDistrib &distrib ) {
    xml.intoElem();

    int32_t numPoints = 0;
    float minValue = -1.0f;
    float maxValue = -1.0f;
    while ( xml.findElem( "POINT" ) ) {
        const std::string valueAttrib = xml.getAttrib( "val" );
        if ( valueAttrib.empty() ) {
            continue;
        }
        float value = static_cast<float>( atof( valueAttrib.c_str() ) );

        const std::string freqAttrib = xml.getAttrib( "freq" );
        if ( freqAttrib.empty() ) {
            continue;
        }
        float frequency = static_cast<float>( atof( freqAttrib.c_str() ) );

        if ( minValue < 0.0f || value < minValue ) {
            minValue = value;
        }
        if ( maxValue < 0.0f || maxValue < value ) {
            maxValue = value;
        }
        distrib.Max = maxValue;
        distrib.Min = minValue;

        if ( value < minValue ) {
            value = minValue;
        }
        if ( value > distrib.Max ) {
            value = distrib.Max;
        }
        frequency = std::clamp( frequency, 0.0f, 1.0f );

        distrib.Points[numPoints].Value = value;
        distrib.Points[numPoints].Frequency = frequency;
        numPoints++;
    }

    for ( ; numPoints < distrib.NumPoints; numPoints++ ) {
        float value = maxValue;
        if ( value < distrib.Min ) {
            value = distrib.Min;
        }
        if ( value > distrib.Max ) {
            value = distrib.Max;
        }
        distrib.Points[numPoints].Value = value;
        distrib.Points[numPoints].Frequency = 0.0f;
    }

    xml.outOfElem();
}

AIConfig::AIConfig()
    : MemorySize( 0x200000 ),
      MaxNumVehicle( 8 ),
      MaxNumTrafficVehicle( 8 ),
      MaxNumParkedVehicle( 4 ),
      MaxNumPoliceVehicle( 3 ),
      MaxNumPoliceDam( 3 ),
      MaxNumBots( 5 ),
      MaxNumBotsLimited( 1 ),
      bPoliceCountFromIni( false ),
      PoliceDeleteDistance( 1000.0f ),
      PoliceSearchDistance( 500.0f ),
      PoliceCaptureDistance( 50.0f ),
      PoliceCaptureSpeed( 10.0f ),
      PoliceCaptureTime( 5.0f ),
      bPoliceForceChase( false ),
      PoliceSpawnProba( 0.5f ),
      PoliceSearchSpawnProba( 1.0f ),
      bPoliceForceDensity( true ),
      PoliceSearchMinTime( 20.0f ),
      PoliceSearchMinTimeStep( 0.0f ),
      bPoliceCanBackOnTrack( false ),
      bPoliceOutsideAllowed( true ),
      bCaptureByTime( false ),
      bCaptureBySpeed( true ),
      bNeedSpeedAndTime( true ),
      bPoliceCanCollide( true ),
      bPoliceCanCollideFromBehind( true ),
      bPoliceCanCollideFromAhead( false ),
      bPoliceCanChaseFromAhead( true ),
      Unknown0x59( true ),
      bEnableJail( false ),
      Unknown0x5B( false ),
      InfLimit( 6.0f ),
      InfACGraceDelay( 5.0f ),
      InfGracePeriod( 2.0f ),
      InfLoweringFactor( 1.0f ),
      InfCollisionValue( 12.0f ),
      InfSpeedLimitTolerance( 1.1f ),
      InfSpeedLimitFactor( 1.0f ),
      InfOncomingLaneFactor( 1.0f ),
      InfOncomingLaneValue( 3.0f ),
      InfRedLightValue( 3.0f ),
      InfStopValue( 3.0f ),
      InfYieldValue( 3.0f ),
      InfAllowedDistToStop( 10.0f ),
      InfAllowedSpeedToStop( 1.0f ),
      InfAllowedDurationOnControl( 7.0f ),
      InfYieldObsTimeToJunc( 1.0f ),
      InfCopMalusFactor( 1.0f ),
      InfPoliceDamLimit( 30.0f ),
      InfSecondCopLimit( 18.0f ),
      InfThirdCopLimit( 30.0f ),
      InfAggressiveLimit( 30.0f ),
      WitnessedOffenceTime( 0.1f ),
      WitnessedOffenceValue( 12.0f ),
      bInfAllButCollision( true ),
      Unknown0xB9( false ),
      Unknown0xBA( false ),
      Unknown0xBB( false ),
      CollisionRepeatDelay( 1.0f ),
      GInfMaxValue( 100.0f ),
      GInfLoweringFactor( 0.1f ),
      GInfDestroyCar( 20.0f ),
      FineCarValue( 100 ),
      FineSetValue( 25 ),
      RecidiveTime( 180.0f ),
      OpponentDifficulty( 1.0f ),
      NumOpponentsOverride( -1 ),
      bAIReplacePlayer( false ),
      bSameCarAsPlayer( false ),
      bNavOppRespectLane( true ),
      bTraceModeEnabled( false ),
      Unknown0xE4( true ),
      BotsWantedDifficulty( 2 ),
      BotsWantedBehavior( 2 ),
      bBotBehaviorOverride( false ),
      RubberBandMinDistance( 100.0f ),
      RubberBandMaxDistance( 300.0f ),
      RubberBandMaxSlowDown( 0.8f ),
      bPathOptFixedNormals( true ),
      PathOptMinAngle( 0.1f ),
      PathOptTrackDistance( 20.0f ),
      PathOptForceFactor( 0.1f ),
      PathOptReduceFactor( 0.01f ),
      PathOptUncurveToExtractCoeff( 0.5f ),
      PathOptDamping( 0.1f ),
      PathOptCoeffMaxValue( 0.5f ),
      PathOptNbPasses( 10 ),
      PathOptMethod( 0 ),
      PathOptMethodPower( 1 ),
      PathOptMinAmpFactor( 1.0f ),
      PathOptMaxAmpFactor( 191.0f ),
      bPathOptEnableAmp( false ),
      PathOptSpeed{ 25.0f, 50.0f, 75.0f },
      PathOptCoeff{ 1.0f, 1.0f, 1.0f },
      Unknown0x150( 150.0f ),
      SpawnConfig{ 0.2f, 0.01f, 0.2f },
      Unknown0x160( 0.5f ),
      FadeInTime( 2.0f ),
      FadeOutTime( 2.0f ),
      SpawnDistances{ 2.0f, 100.0f, 300.0f },
      NetTimeCorrectionMethod( 0 ),
      NetSendPeriod( 1.0f ),
      NetInterpolationTime( 0.8f ),
      NetInterpolationMethod( 2 ),
      NetTimeout( 10.0f ),
      Unknown0x18C( 5.0f ),
      LocoManualMaxAccel( 2.0f ),
      LocoManualMaxBrake( 20.0f ),
      OpponentSharpTurnLimit( 0.1f ),
      OpponentSharpTurnMaxSpeed( 10.0f ),
      OpponentSharpTurnClampValue( 0.7f ),
      CullingFirstRadius( 100.0f ),
      CullingSecondRadius( 250.0f ),
      CullingFovDistance( 200.0f ),
      CullingFovMagnitude( 45.0f ),
      MaxNumPhysicsVehicle( 4 ),
      LODPeriod( 0.1f ),
      Unknown0x1BC( 50000.0f ),
      bEnableBots( false ),
      Zones(),
      NumZones( 1 ),
      HardcodedZones(),
      NumHardcodedZones( 0 ),
      SpeedDistrib(),
      SecDistDistrib(),
      VehiclePhysics(),
      NumVehiclePhysics( 0 ),
      PedMaxToDisplay( 40 ),
      PedCollideSphereRadius( 0.25f ),
      PedCollideSphereDetectionRadius( 1.0f ),
      PedMinSpeed( 0.2f ),
      PedMaxSpeed( 2.0f ),
      PedMaxLength2Player( 300.0f ) {
    // FUN_009762e0
    SpeedDistrib.NumPoints = AIDistrib::kMaxNumPoints;
    SecDistDistrib.NumPoints = AIDistrib::kMaxNumPoints;
    reset();
}

void AIConfig::reset() {
    // FUN_00976140
    NumZones = 1;
    for ( AIZone &zone : Zones ) {
        for ( AIZoneVehicle &vehicle : zone.Vehicles ) {
            vehicle.Frequency = 0.0f;
            vehicle.DatabaseId = 0;
        }
        zone.TrafficPerLaneKm = 10.0f;
    }

    NumHardcodedZones = 0;
    for ( AIHardcodedZone &hardcodedZone : HardcodedZones ) {
        hardcodedZone = {};
    }

    ResetDistrib( SpeedDistrib );
    ResetDistrib( SecDistDistrib );

    NumVehiclePhysics = 0;
    for ( AIVehiclePhysics &physics : VehiclePhysics ) {
        physics.BrakingDistC1 = 0.3f;
        physics.BrakingDistC2 = 0.033f;
        physics.TurningSpeedC1 = 2.8f;
        physics.TurningSpeedC2 = 0.0f;
        physics.HandlingMark = 100.0f;
        physics.DatabaseId = 0;
        physics.LogMaxSpeed = 0.0f;
    }
}

void AIConfig::parse( const char *pXml ) {
    // FUN_009784d0
    reset();

    CMarkup xml( pXml );
    if ( !xml.isWellFormed() ) {
        return;
    }
    if ( !xml.findElem( "AICONFIG" ) ) {
        return;
    }
    xml.intoElem();

    if ( xml.findElem( "VEHICLE_REPARTITION" ) ) {
        const std::string numZonesAttrib = xml.getAttrib( "nb_zones" );
        if ( numZonesAttrib.empty() || atoi( numZonesAttrib.c_str() ) < 1 ) {
            NumZones = 1;
        } else {
            NumZones = atoi( numZonesAttrib.c_str() );
        }
        if ( NumZones > kMaxNumZones ) {
            NumZones = kMaxNumZones;
        }

        xml.intoElem();
        for ( int32_t zoneIndex = 0; zoneIndex < NumZones; zoneIndex++ ) {
            AIZone &zone = Zones[zoneIndex];

            const bool bFoundZone = xml.findElem( "ZONE" );
            if ( bFoundZone ) {
                const std::string densityAttrib =
                    xml.getAttrib( "nb_traffic_per_lane_kilometer" );
                if ( !densityAttrib.empty() ) {
                    zone.TrafficPerLaneKm =
                        static_cast<float>( atof( densityAttrib.c_str() ) );
                }
                xml.intoElem();
            }

            int32_t vehicleIndex = 0;
            while ( xml.findElem( "VEHICLE" ) ) {
                const std::string idAttrib = xml.getAttrib( "database_id" );
                if ( !idAttrib.empty() ) {
                    AIZoneVehicle &vehicle = zone.Vehicles[vehicleIndex];
                    vehicle.DatabaseId = atoi( idAttrib.c_str() );
                    vehicle.Frequency =
                        ParseFloatAttrib( xml, "frequency", 0.0f );
                }
                vehicleIndex++;
            }

            if ( bFoundZone ) {
                xml.outOfElem();
            }
        }
        xml.outOfElem();
    }

    if ( xml.findElem( "VEHICLE_PHYSICS" ) ) {
        NumVehiclePhysics = 0;
        xml.intoElem();
        while ( xml.findElem( "VEHICLE" ) ) {
            AIVehiclePhysics &physics = VehiclePhysics[NumVehiclePhysics];
            const std::string idAttrib = xml.getAttrib( "database_id" );
            if ( !idAttrib.empty() ) {
                physics.DatabaseId = atoi( idAttrib.c_str() );
                physics.BrakingDistC1 =
                    ParseFloatAttrib( xml, "braking_dist_c1", 0.3f );
                physics.BrakingDistC2 =
                    ParseFloatAttrib( xml, "braking_dist_c2", 0.033f );
                physics.TurningSpeedC1 =
                    ParseFloatAttrib( xml, "turning_speed_c1", 2.8f );
                physics.TurningSpeedC2 =
                    ParseFloatAttrib( xml, "turning_speed_c2", 0.0f );
                physics.HandlingMark =
                    ParseFloatAttrib( xml, "handling_mark", 100.0f );

                const std::string frictionAttrib =
                    xml.getAttrib( "friction_lat" );
                if ( !frictionAttrib.empty() ) {
                    physics.TurningSpeedC1 = static_cast<float>(
                        sqrt( atof( frictionAttrib.c_str() ) *
                              static_cast<double>( kGravity ) ) );
                }

                physics.LogMaxSpeed =
                    ParseFloatAttrib( xml, "log_max_speed", 0.0f );
            }
            NumVehiclePhysics++;
        }
        xml.outOfElem();
    }

    if ( xml.findElem( "SPEED_DISTRIB" ) ) {
        ParseDistrib( xml, SpeedDistrib );
    }

    if ( xml.findElem( "SECDIST_DISTRIB" ) ) {
        ParseDistrib( xml, SecDistDistrib );
    }

    if ( xml.findElem( "HARDCODED_ZONES" ) ) {
        NumHardcodedZones = 0;
        xml.intoElem();
        while ( xml.findElem( "HZONE" ) ) {
            const std::string sectorAttrib = xml.getAttrib( "sector" );
            if ( sectorAttrib.size() != 4 ) {
                continue;
            }

            AIHardcodedZone &hardcodedZone = HardcodedZones[NumHardcodedZones];
            for ( int32_t i = 0; i < 4; i++ ) {
                hardcodedZone.Sector[i] =
                    static_cast<int32_t>( sectorAttrib[i] ) - '0';
            }

            const std::string zoneAttrib = xml.getAttrib( "zone" );
            hardcodedZone.Zone =
                zoneAttrib.empty()
                    ? 0
                    : static_cast<int32_t>( zoneAttrib[0] ) - '0';

            hardcodedZone.Sector[0] =
                std::clamp( hardcodedZone.Sector[0], 0, 10 );
            hardcodedZone.Sector[1] =
                std::clamp( hardcodedZone.Sector[1], 0, 8 );
            hardcodedZone.Sector[2] =
                std::clamp( hardcodedZone.Sector[2], 0, 7 );
            hardcodedZone.Sector[3] =
                std::clamp( hardcodedZone.Sector[3], 0, 7 );
            if ( hardcodedZone.Zone < 0 ) {
                hardcodedZone.Zone = 0;
            }
            if ( hardcodedZone.Zone >= NumZones ) {
                hardcodedZone.Zone = NumZones - 1;
            }
            NumHardcodedZones++;
        }
        xml.outOfElem();
    }

    // TODO: FIXME: CollideSphereRadiu/CollideSphereDetectionRadius are never
    // read due to a bug in the original p-code
    if ( xml.findElem( "Pedestrians" ) ) {
        xml.intoElem();
        if ( xml.findElem( "Max2Display" ) ) {
            PedMaxToDisplay = static_cast<uint32_t>( ReadInt( xml ) );
            if ( PedMaxToDisplay > 40 ) {
                PedMaxToDisplay = 40;
            }
        }
        if ( xml.findElem( "MinSpeed" ) ) {
            PedMinSpeed = ReadFloat( xml );
            if ( !( 0.0f < PedMinSpeed ) ) {
                PedMinSpeed = 2.0f;
            }
        }
        if ( xml.findElem( "MaxSpeed" ) ) {
            PedMaxSpeed = ReadFloat( xml );
            if ( !( 0.0f < PedMaxSpeed ) ) {
                PedMaxSpeed = std::min( PedMinSpeed + 0.1f, 2.0f );
            }
        }
        if ( xml.findElem( "CollideSphereRadius" ) ) {
            PedCollideSphereRadius = ReadFloat( xml );
            if ( PedCollideSphereRadius <= 0.0f ) {
                PedCollideSphereRadius = 0.25f;
            }
        }
        if ( xml.findElem( "CollideSphereDetectionRadius" ) ) {
            PedCollideSphereDetectionRadius = ReadFloat( xml );
            if ( PedCollideSphereDetectionRadius <= 0.0f ) {
                PedCollideSphereDetectionRadius = 1.0f;
            }
        }
        if ( xml.findElem( "MaxLength2Player" ) ) {
            PedMaxLength2Player = ReadFloat( xml );
            if ( PedMaxLength2Player <= 0.0f ) {
                PedMaxLength2Player = 300.0f;
            }
        }
        xml.outOfElem();
    }

    if ( xml.findElem( "OppAndCop" ) ) {
        xml.intoElem();
        if ( xml.findElem( "PathOptimizerParams" ) ) {
            xml.intoElem();
            ParseBool( xml, "PathOptFixedNormals", bPathOptFixedNormals );
            ParseFloat( xml, "PathOptMinAngle", PathOptMinAngle );
            ParseFloat( xml, "PathOptTrackDistance", PathOptTrackDistance );
            ParseFloat( xml, "PathOptForceFactor", PathOptForceFactor );
            ParseFloat( xml, "PathOptDamping", PathOptDamping );
            ParseFloat( xml, "PathOptCoeffMaxValue", PathOptCoeffMaxValue );
            ParseInt( xml, "PathOptNbPasses", PathOptNbPasses );
            ParseInt( xml, "PathOptMethod", PathOptMethod );
            ParseInt( xml, "PathOptMethodPower", PathOptMethodPower );
            ParseFloat( xml, "PathOptMinAmpFactor", PathOptMinAmpFactor );
            ParseFloat( xml, "PathOptMaxAmpFactor", PathOptMaxAmpFactor );
            ParseFloat( xml, "PathOptReduceFactor", PathOptReduceFactor );
            ParseFloat( xml, "PathOptUncurveToExtractCoeff",
                        PathOptUncurveToExtractCoeff );
            ParseFloat( xml, "Speed1", PathOptSpeed[0] );
            ParseFloat( xml, "Speed2", PathOptSpeed[1] );
            ParseFloat( xml, "Speed3", PathOptSpeed[2] );
            ParseFloat( xml, "Coeff1", PathOptCoeff[0] );
            ParseFloat( xml, "Coeff2", PathOptCoeff[1] );
            ParseFloat( xml, "Coeff3", PathOptCoeff[2] );
            ParseBool( xml, "PathOptEnableAmp", bPathOptEnableAmp );
            xml.outOfElem();
        }

        if ( xml.findElem( "PoliceParams" ) ) {
            xml.intoElem();
            // The original p-code tests the global config, not the one being
            // parsed
            if ( xml.findElem( "PoliceMaxCount" ) &&
                 !gAIConfig.bPoliceCountFromIni ) {
                MaxNumPoliceVehicle = ReadInt( xml );
            }
            if ( xml.findElem( "PoliceDamMaxCount" ) &&
                 !gAIConfig.bPoliceCountFromIni ) {
                MaxNumPoliceDam = ReadInt( xml );
            }
            ParseFloat( xml, "PoliceDeleteDistance", PoliceDeleteDistance );
            ParseFloat( xml, "PoliceSearchDistance", PoliceSearchDistance );
            ParseFloat( xml, "PoliceCaptureDistance", PoliceCaptureDistance );
            ParseFloat( xml, "PoliceCaptureSpeed", PoliceCaptureSpeed );
            ParseFloat( xml, "PoliceCaptureTime", PoliceCaptureTime );
            ParseBool( xml, "PoliceForceChase", bPoliceForceChase );
            ParseFloat( xml, "PoliceSpawnProba", PoliceSpawnProba );
            ParseFloat( xml, "PoliceSearchSpawnProba", PoliceSearchSpawnProba );
            ParseFloat( xml, "PoliceSearchMinTime", PoliceSearchMinTime );
            ParseFloat( xml, "PoliceSearchMinTimeStep",
                        PoliceSearchMinTimeStep );
            ParseBool( xml, "PoliceCanBackOnTrack", bPoliceCanBackOnTrack );
            ParseBool( xml, "PoliceForceDensity", bPoliceForceDensity );
            ParseBool( xml, "OutsideAllowed", bPoliceOutsideAllowed );
            ParseBool( xml, "CaptureByTime", bCaptureByTime );
            ParseBool( xml, "CaptureBySpeed", bCaptureBySpeed );
            ParseBool( xml, "NeedSpeedANDTime", bNeedSpeedAndTime );
            ParseBool( xml, "CanCollide", bPoliceCanCollide );
            ParseBool( xml, "CanCollideFromBehind",
                       bPoliceCanCollideFromBehind );
            ParseBool( xml, "CanCollideFromAhead", bPoliceCanCollideFromAhead );
            ParseBool( xml, "CanChaseFromAhead", bPoliceCanChaseFromAhead );
            xml.outOfElem();
        }

        if ( xml.findElem( "Infringement" ) ) {
            xml.intoElem();
            ParseFloat( xml, "InfLimit", InfLimit );
            ParseFloat( xml, "InfACGraceDelay", InfACGraceDelay );
            ParseFloat( xml, "InfGracePeriod", InfGracePeriod );
            ParseFloat( xml, "InfLoweringFactor", InfLoweringFactor );
            ParseFloat( xml, "InfCollisionValue", InfCollisionValue );
            ParseFloat( xml, "InfSpeedLimitTolerance", InfSpeedLimitTolerance );
            ParseFloat( xml, "InfSpeedLimitFactor", InfSpeedLimitFactor );
            ParseFloat( xml, "InfOncomingLaneFactor", InfOncomingLaneFactor );
            ParseFloat( xml, "InfOncomingLaneValue", InfOncomingLaneValue );
            ParseFloat( xml, "InfRedLightValue", InfRedLightValue );
            ParseFloat( xml, "InfStopValue", InfStopValue );
            ParseFloat( xml, "InfYieldValue", InfYieldValue );
            ParseFloat( xml, "InfAllowedDistToStop", InfAllowedDistToStop );
            ParseFloat( xml, "InfAllowedSpeedToStop", InfAllowedSpeedToStop );
            ParseFloat( xml, "InfAllowedDurationOnControl",
                        InfAllowedDurationOnControl );
            ParseFloat( xml, "InfYieldObsTimeToJunc", InfYieldObsTimeToJunc );
            ParseFloat( xml, "InfCopMalusFactor", InfCopMalusFactor );
            ParseFloat( xml, "InfPoliceDamLimit", InfPoliceDamLimit );
            ParseFloat( xml, "InfSecondCopLimit", InfSecondCopLimit );
            ParseFloat( xml, "InfThirdCopLimit", InfThirdCopLimit );
            ParseFloat( xml, "InfAggressiveLimit", InfAggressiveLimit );
            ParseFloat( xml, "WitnessedOffenceTime", WitnessedOffenceTime );
            ParseFloat( xml, "WitnessedOffenceValue", WitnessedOffenceValue );
            ParseBool( xml, "InfAllButCollision", bInfAllButCollision );
            ParseInt( xml, "FineCarValue", FineCarValue );
            ParseInt( xml, "FineSetValue", FineSetValue );
            ParseFloat( xml, "RecidiveTime", RecidiveTime );
            ParseFloat( xml, "GInfMaxValue", GInfMaxValue );
            ParseFloat( xml, "GInfLoweringFactor", GInfLoweringFactor );
            ParseFloat( xml, "GInfDestroyCar", GInfDestroyCar );
            xml.outOfElem();
        }

        // TODO: FIXME: The original p-code skips Opponents configuration due to
        // a bug.
        xml.outOfElem();
    }
}

float AIDistrib::sample( RandomGen &random ) const {
    // FUN_006d9f10
    static constexpr float kLinearEpsilon = 1.0e-4f; // DAT_00f41760

    float area = 0.0f;
    for ( int32_t i = 0; i < NumPoints - 1; i++ ) {
        area += ( Points[i + 1].Value - Points[i].Value ) *
                ( Points[i].Frequency + Points[i + 1].Frequency ) * 0.5f;
    }
    const float target = RandomUnit( random ) * area;

    float accumulated = 0.0f;
    for ( int32_t i = 0; i < NumPoints - 1; i++ ) {
        const float x0 = Points[i].Value - Min;
        const float x1 = Points[i + 1].Value - Min;
        const float f0 = Points[i].Frequency;
        const float f1 = Points[i + 1].Frequency;
        const float segment = ( x1 - x0 ) * ( f0 + f1 ) * 0.5f;
        if ( accumulated + segment < target ) {
            accumulated += segment;
            continue;
        }

        const float remaining = target - accumulated;
        const float slope = ( f1 - f0 ) / ( x1 - x0 );
        const float intercept = f0 - slope * x0;
        const float a = 0.5f * slope;
        const float c =
            -( intercept * x0 ) - 0.5f * slope * x0 * x0 - remaining;
        float xa;
        float xb;
        if ( a < kLinearEpsilon && a > -kLinearEpsilon ) {
            xa = -c / intercept;
            xb = xa;
        } else {
            const float discriminant =
                sqrtf( intercept * intercept - 4.0f * a * c );
            xa = ( -intercept - discriminant ) / ( 2.0f * a );
            xb = ( discriminant - intercept ) / ( 2.0f * a );
        }
        if ( xa >= x0 && xa <= x1 ) {
            return Min + xa;
        }
        if ( xb >= x0 && xb <= x1 ) {
            return Min + xb;
        }
        return Min + x0;
    }
    return Min;
}

int32_t GetNextAvailableCarID( const int32_t carID ) {
    // FUN_009ab590 (forward, traffic excluded)
    const GameDatabase &gameDB = gpDatabase->getGameDB();
    const int32_t numCars = static_cast<int32_t>( gameDB.pNumItems[1] );
    if ( numCars == 0 ) {
        return -1;
    }

    int32_t currentIndex = -1;
    for ( int32_t i = 0; i < numCars; i++ ) {
        if ( gameDB.DBCarData[i].CarID == carID ) {
            currentIndex = i;
            break;
        }
    }

    int32_t index = ( currentIndex < 0 ) ? numCars - 1 : currentIndex + 1;
    if ( index >= numCars ) {
        index = 0;
    }
    for ( int32_t numTested = 0; index != currentIndex; numTested++ ) {
        if ( numTested == numCars ) {
            return -1;
        }

        const DBEntryCarPhysicsData &car = gameDB.DBCarData[index];
        if ( ( car.BitField & 1 ) != 0 && car.Brand != nullptr &&
             ( car.Brand->Flags & 1 ) != 0 && !gpDatabase->isTraffic( &car ) &&
             gpDatabase->carBankExists( &car ) ) {
            break;
        }

        index++;
        if ( index >= numCars ) {
            index = 0;
        }
    }
    return gameDB.DBCarData[index].CarID;
}

float ComputeTurningRadius( const float coef1,
                            const float coef2,
                            const float speed ) {
    // FUN_00924280
    float root0 = 0.0f;
    float root1 = 0.0f;
    if ( fabsf( coef2 ) < kTurnLinearEpsilon ) {
        root0 = speed / coef1;
        root1 = root0;
    } else {
        float discriminant = coef1 * coef1 - ( -speed * coef2 ) * 4.0f;
        if ( discriminant < kDiscriminantEpsilon ) {
            return kDefaultReferenceValue;
        }
        if ( discriminant < 0.0f ) {
            discriminant = 0.0f;
        }
        const float root = sqrtf( discriminant );
        root0 = ( -coef1 - root ) / ( 2.0f * coef2 );
        root1 = ( root - coef1 ) / ( 2.0f * coef2 );
    }

    float sqrtRadius = 0.0f;
    if ( root0 < 0.0f ) {
        sqrtRadius = root1;
    } else if ( root1 < 0.0f ) {
        sqrtRadius = root0;
    } else {
        sqrtRadius = std::min( root0, root1 );
    }
    if ( !( sqrtRadius > 0.0f ) ) {
        sqrtRadius = 0.0f;
    }
    return sqrtRadius * sqrtRadius;
}

float ComputeMaxTopSpeed() {
    // FUN_00906c40
    float maxTopSpeed = 0.0f;
    ForEachAvailableCar( [&]( const int32_t carID ) {
        const DBEntryCarPhysicsData *pCar = gpDatabase->getCarDBEntry( carID );
        if ( pCar->TopSpeedKMH > maxTopSpeed ) {
            maxTopSpeed = pCar->TopSpeedKMH;
        }
    } );
    return maxTopSpeed;
}

float ComputeMinAcceleration() {
    // FUN_00906b90
    float minAcceleration = kDefaultReferenceValue;
    ForEachAvailableCar( [&]( const int32_t carID ) {
        const DBEntryCarPhysicsData *pCar = gpDatabase->getCarDBEntry( carID );
        if ( minAcceleration > pCar->AccelerationKMH &&
             pCar->AccelerationKMH != 0.0f ) {
            minAcceleration = pCar->AccelerationKMH;
        }
    } );
    return minAcceleration;
}

const AIVehiclePhysics *
AIConfig::findVehiclePhysics( const int32_t carID ) const {
    for ( int32_t i = 0; i < NumVehiclePhysics; i++ ) {
        if ( VehiclePhysics[i].DatabaseId == carID ) {
            return &VehiclePhysics[i];
        }
    }
    return nullptr;
}

float AIConfig::computeMinTurnRadius() const {
    // FUN_009068c0
    float minRadius = kDefaultReferenceValue;
    ForEachAvailableCar( [&]( const int32_t carID ) {
        const AIVehiclePhysics *pPhysics = findVehiclePhysics( carID );
        if ( pPhysics == nullptr ||
             pPhysics->TurningSpeedC1 == kUnknownHandlingMark ) {
            return;
        }

        const float radius =
            ComputeTurningRadius( pPhysics->TurningSpeedC1,
                                  pPhysics->TurningSpeedC2, kReferenceSpeed );
        if ( minRadius > radius && radius != 0.0f ) {
            minRadius = radius;
        }
    } );
    return minRadius;
}

float AIConfig::computeMinBrakingDistance() const {
    // FUN_00906aa0
    float minDistance = kDefaultReferenceValue;
    ForEachAvailableCar( [&]( const int32_t carID ) {
        float coef1 = kDefaultReferenceValue;
        float coef2 = kDefaultReferenceValue;
        const AIVehiclePhysics *pPhysics = findVehiclePhysics( carID );
        if ( pPhysics != nullptr ) {
            coef1 = pPhysics->BrakingDistC1;
            coef2 = pPhysics->BrakingDistC2;
        }

        float distance =
            coef1 * kReferenceSpeed + coef2 * kReferenceSpeedSquared;
        if ( !( distance > 0.0f ) ) {
            distance = 0.0f;
        }
        if ( minDistance > distance && distance != 0.0f ) {
            minDistance = distance;
        }
    } );
    return minDistance;
}

float AIConfig::computeHandlingMark( const int32_t carID,
                                     AIHandlingReference &reference ) const {
    // FUN_00906d60
    if ( reference.MinTurnRadius == kDefaultReferenceValue ) {
        reference.MinTurnRadius = computeMinTurnRadius();
    }

    const AIVehiclePhysics *pPhysics = findVehiclePhysics( carID );
    if ( pPhysics == nullptr ||
         pPhysics->TurningSpeedC1 == kUnknownHandlingMark ) {
        return kUnknownHandlingMark;
    }

    const float radius = ComputeTurningRadius(
        pPhysics->TurningSpeedC1, pPhysics->TurningSpeedC2, kReferenceSpeed );
    if ( radius == 0.0f ) {
        return kUnknownHandlingMark;
    }
    return kHandlingMarkScale /
           ( ( radius * radius ) /
             ( reference.MinTurnRadius * reference.MinTurnRadius ) );
}

static std::string FormatValue( const char *pFormat, ... ) {
    // FUN_006dca10
    char buffer[256];
    va_list args;
    va_start( args, pFormat );
    vsnprintf( buffer, sizeof( buffer ), pFormat, args );
    va_end( args );
    return buffer;
}

static inline void
AddElemValue( CMarkup &xml, const char *pName, const std::string &value ) {
    xml.addElem( pName, value.c_str() );
}

static inline void
AddElemBool( CMarkup &xml, const char *pName, const bool bValue ) {
    AddElemValue( xml, pName, FormatValue( "%s", bValue ? "true" : "false" ) );
}

static inline void
SetAttribValue( CMarkup &xml, const char *pName, const std::string &value ) {
    xml.setAttrib( pName, value.c_str() );
}

static void
SerializeDistrib( CMarkup &xml, const char *pName, const AIDistrib &distrib ) {
    xml.addElem( pName );
    xml.intoElem();
    for ( int32_t i = 0; i < distrib.NumPoints; i++ ) {
        xml.addElem( "POINT" );
        SetAttribValue( xml, "val",
                        FormatValue( "%.2f", distrib.Points[i].Value ) );
        SetAttribValue( xml, "freq",
                        FormatValue( "%.2f", distrib.Points[i].Frequency ) );
    }
    xml.outOfElem();
}

int32_t AIConfig::serialize( char *pBuffer, const uint32_t bufferSize ) const {
    // FUN_00976850
    CMarkup xml;
    xml.setDoc( "<?xml version=\"1.0\" encoding=\"ISO-8859-1\"?>\r\n" );
    xml.addElem( "AICONFIG" );
    SetAttribValue( xml, "version", FormatValue( "%d", kSerializedVersion ) );
    xml.intoElem();

    xml.addElem( "VEHICLE_REPARTITION" );
    SetAttribValue( xml, "nb_zones", FormatValue( "%d", NumZones ) );
    xml.intoElem();
    for ( int32_t i = 0; i < NumZones; i++ ) {
        const AIZone &zone = Zones[i];
        xml.addElem( "ZONE" );
        SetAttribValue( xml, "nb_traffic_per_lane_kilometer",
                        FormatValue( "%.2f", zone.TrafficPerLaneKm ) );
        xml.intoElem();
        for ( const AIZoneVehicle &vehicle : zone.Vehicles ) {
            xml.addElem( "VEHICLE" );
            SetAttribValue( xml, "database_id",
                            FormatValue( "%d", vehicle.DatabaseId ) );
            SetAttribValue( xml, "frequency",
                            FormatValue( "%.3f", vehicle.Frequency ) );
        }
        xml.outOfElem();
    }
    xml.outOfElem();

    xml.addElem( "VEHICLE_PHYSICS" );
    xml.intoElem();
    for ( int32_t i = 0; i < NumVehiclePhysics; i++ ) {
        const AIVehiclePhysics &physics = VehiclePhysics[i];
        xml.addElem( "VEHICLE" );
        SetAttribValue( xml, "database_id",
                        FormatValue( "%d", physics.DatabaseId ) );
        if ( physics.LogMaxSpeed > 0.0f ) {
            SetAttribValue( xml, "log_max_speed",
                            FormatValue( "%.1f", physics.LogMaxSpeed ) );
        }
        SetAttribValue( xml, "braking_dist_c1",
                        FormatValue( "%.3f", physics.BrakingDistC1 ) );
        SetAttribValue( xml, "braking_dist_c2",
                        FormatValue( "%.3f", physics.BrakingDistC2 ) );
        SetAttribValue( xml, "turning_speed_c1",
                        FormatValue( "%.3f", physics.TurningSpeedC1 ) );
        SetAttribValue( xml, "turning_speed_c2",
                        FormatValue( "%.3f", physics.TurningSpeedC2 ) );
        SetAttribValue( xml, "handling_mark",
                        FormatValue( "%.3f", physics.HandlingMark ) );
    }
    xml.outOfElem();

    SerializeDistrib( xml, "SPEED_DISTRIB", SpeedDistrib );
    SerializeDistrib( xml, "SECDIST_DISTRIB", SecDistDistrib );

    xml.addElem( "HARDCODED_ZONES" );
    xml.intoElem();
    for ( int32_t i = 0; i < NumHardcodedZones; i++ ) {
        const AIHardcodedZone &hardcodedZone = HardcodedZones[i];
        xml.addElem( "HZONE" );
        SetAttribValue( xml, "sector",
                        FormatValue( "%c%c%c%c", hardcodedZone.Sector[0] + '0',
                                     hardcodedZone.Sector[1] + '0',
                                     hardcodedZone.Sector[2] + '0',
                                     hardcodedZone.Sector[3] + '0' ) );
        SetAttribValue( xml, "zone",
                        FormatValue( "%c", hardcodedZone.Zone + '0' ) );
    }
    xml.outOfElem();

    xml.addElem( "Pedestrians" );
    xml.intoElem();
    AddElemValue(
        xml, "Max2Display",
        FormatValue( "%d", static_cast<int32_t>( PedMaxToDisplay ) ) );
    AddElemValue( xml, "CollideSphereRadius",
                  FormatValue( "%.3f", PedCollideSphereRadius ) );
    AddElemValue( xml, "CollideSphereDetectionRadius",
                  FormatValue( "%.3f", PedCollideSphereDetectionRadius ) );
    AddElemValue( xml, "MinSpeed", FormatValue( "%.2f", PedMinSpeed ) );
    AddElemValue( xml, "MaxSpeed", FormatValue( "%.2f", PedMaxSpeed ) );
    AddElemValue( xml, "MaxLength2Player",
                  FormatValue( "%.2f", PedMaxLength2Player ) );
    xml.outOfElem();

    xml.addElem( "OppAndCop" );
    xml.intoElem();

    xml.addElem( "PathOptimizerParams" );
    xml.intoElem();
    AddElemBool( xml, "PathOptFixedNormals", bPathOptFixedNormals );
    AddElemValue( xml, "PathOptMinAngle",
                  FormatValue( "%.8f", PathOptMinAngle ) );
    AddElemValue( xml, "PathOptTrackDistance",
                  FormatValue( "%.3f", PathOptTrackDistance ) );
    AddElemValue( xml, "PathOptForceFactor",
                  FormatValue( "%.3f", PathOptForceFactor ) );
    AddElemValue( xml, "PathOptDamping",
                  FormatValue( "%.3f", PathOptDamping ) );
    AddElemValue( xml, "PathOptCoeffMaxValue",
                  FormatValue( "%.3f", PathOptCoeffMaxValue ) );
    AddElemValue( xml, "PathOptNbPasses",
                  FormatValue( "%i", PathOptNbPasses ) );
    AddElemValue( xml, "PathOptMethod", FormatValue( "%i", PathOptMethod ) );
    AddElemValue( xml, "PathOptMethodPower",
                  FormatValue( "%i", PathOptMethodPower ) );
    AddElemValue( xml, "PathOptMinAmpFactor",
                  FormatValue( "%.3f", PathOptMinAmpFactor ) );
    AddElemValue( xml, "PathOptMaxAmpFactor",
                  FormatValue( "%.3f", PathOptMaxAmpFactor ) );
    AddElemValue( xml, "PathOptReduceFactor",
                  FormatValue( "%.3f", PathOptReduceFactor ) );
    AddElemValue( xml, "PathOptUncurveToExtractCoeff",
                  FormatValue( "%.3f", PathOptUncurveToExtractCoeff ) );
    AddElemValue( xml, "Speed1", FormatValue( "%.3f", PathOptSpeed[0] ) );
    AddElemValue( xml, "Speed2", FormatValue( "%.3f", PathOptSpeed[1] ) );
    AddElemValue( xml, "Speed3", FormatValue( "%.3f", PathOptSpeed[2] ) );
    AddElemValue( xml, "Coeff1", FormatValue( "%.3f", PathOptCoeff[0] ) );
    AddElemValue( xml, "Coeff2", FormatValue( "%.3f", PathOptCoeff[1] ) );
    AddElemValue( xml, "Coeff3", FormatValue( "%.3f", PathOptCoeff[2] ) );
    AddElemBool( xml, "PathOptEnableAmp", bPathOptEnableAmp );
    xml.outOfElem();

    xml.addElem( "PoliceParams" );
    xml.intoElem();
    AddElemValue( xml, "PoliceMaxCount",
                  FormatValue( "%i", MaxNumPoliceVehicle ) );
    AddElemValue( xml, "PoliceDamMaxCount",
                  FormatValue( "%i", MaxNumPoliceDam ) );
    AddElemValue( xml, "PoliceDeleteDistance",
                  FormatValue( "%.3f", PoliceDeleteDistance ) );
    AddElemValue( xml, "PoliceSearchDistance",
                  FormatValue( "%.3f", PoliceSearchDistance ) );
    AddElemValue( xml, "PoliceCaptureDistance",
                  FormatValue( "%.3f", PoliceCaptureDistance ) );
    AddElemValue( xml, "PoliceCaptureSpeed",
                  FormatValue( "%.3f", PoliceCaptureSpeed ) );
    AddElemValue( xml, "PoliceCaptureTime",
                  FormatValue( "%.3f", PoliceCaptureTime ) );
    AddElemBool( xml, "PoliceForceChase", bPoliceForceChase );
    AddElemValue( xml, "PoliceSpawnProba",
                  FormatValue( "%.3f", PoliceSpawnProba ) );
    AddElemValue( xml, "PoliceSearchSpawnProba",
                  FormatValue( "%.3f", PoliceSearchSpawnProba ) );
    AddElemValue( xml, "PoliceSearchMinTime",
                  FormatValue( "%.3f", PoliceSearchMinTime ) );
    AddElemValue( xml, "PoliceSearchMinTimeStep",
                  FormatValue( "%.3f", PoliceSearchMinTimeStep ) );
    AddElemBool( xml, "PoliceCanBackOnTrack", bPoliceCanBackOnTrack );
    AddElemBool( xml, "PoliceForceDensity", bPoliceForceDensity );
    AddElemBool( xml, "OutsideAllowed", bPoliceOutsideAllowed );
    AddElemBool( xml, "CaptureByTime", bCaptureByTime );
    AddElemBool( xml, "CaptureBySpeed", bCaptureBySpeed );
    AddElemBool( xml, "NeedSpeedANDTime", bNeedSpeedAndTime );
    AddElemBool( xml, "CanCollide", bPoliceCanCollide );
    AddElemBool( xml, "CanCollideFromBehind", bPoliceCanCollideFromBehind );
    AddElemBool( xml, "CanCollideFromAhead", bPoliceCanCollideFromAhead );
    AddElemBool( xml, "CanChaseFromAhead", bPoliceCanChaseFromAhead );
    xml.outOfElem();

    xml.addElem( "Infringement" );
    xml.intoElem();
    AddElemValue( xml, "InfLimit", FormatValue( "%.3f", InfLimit ) );
    AddElemValue( xml, "InfACGraceDelay",
                  FormatValue( "%.3f", InfACGraceDelay ) );
    AddElemValue( xml, "InfGracePeriod",
                  FormatValue( "%.3f", InfGracePeriod ) );
    AddElemValue( xml, "InfLoweringFactor",
                  FormatValue( "%.3f", InfLoweringFactor ) );
    AddElemValue( xml, "InfCollisionValue",
                  FormatValue( "%.3f", InfCollisionValue ) );
    AddElemValue( xml, "InfSpeedLimitTolerance",
                  FormatValue( "%.3f", InfSpeedLimitTolerance ) );
    AddElemValue( xml, "InfSpeedLimitFactor",
                  FormatValue( "%.3f", InfSpeedLimitFactor ) );
    AddElemValue( xml, "InfOncomingLaneFactor",
                  FormatValue( "%.3f", InfOncomingLaneFactor ) );
    AddElemValue( xml, "InfOncomingLaneValue",
                  FormatValue( "%.3f", InfOncomingLaneValue ) );
    AddElemValue( xml, "InfRedLightValue",
                  FormatValue( "%.3f", InfRedLightValue ) );
    AddElemValue( xml, "InfStopValue", FormatValue( "%.3f", InfStopValue ) );
    AddElemValue( xml, "InfYieldValue", FormatValue( "%.3f", InfYieldValue ) );
    AddElemValue( xml, "InfAllowedDistToStop",
                  FormatValue( "%.3f", InfAllowedDistToStop ) );
    AddElemValue( xml, "InfAllowedSpeedToStop",
                  FormatValue( "%.3f", InfAllowedSpeedToStop ) );
    AddElemValue( xml, "InfAllowedDurationOnControl",
                  FormatValue( "%.3f", InfAllowedDurationOnControl ) );
    AddElemValue( xml, "InfYieldObsTimeToJunc",
                  FormatValue( "%.3f", InfYieldObsTimeToJunc ) );
    AddElemValue( xml, "InfCopMalusFactor",
                  FormatValue( "%.3f", InfCopMalusFactor ) );
    AddElemValue( xml, "InfPoliceDamLimit",
                  FormatValue( "%.3f", InfPoliceDamLimit ) );
    AddElemValue( xml, "InfSecondCopLimit",
                  FormatValue( "%.3f", InfSecondCopLimit ) );
    AddElemValue( xml, "InfThirdCopLimit",
                  FormatValue( "%.3f", InfThirdCopLimit ) );
    AddElemValue( xml, "InfAggressiveLimit",
                  FormatValue( "%.3f", InfAggressiveLimit ) );
    AddElemValue( xml, "WitnessedOffenceTime",
                  FormatValue( "%.3f", WitnessedOffenceTime ) );
    AddElemValue( xml, "WitnessedOffenceValue",
                  FormatValue( "%.3f", WitnessedOffenceValue ) );
    AddElemBool( xml, "InfAllButCollision", bInfAllButCollision );
    AddElemValue( xml, "FineCarValue", FormatValue( "%i", FineCarValue ) );
    AddElemValue( xml, "FineSetValue", FormatValue( "%i", FineSetValue ) );
    AddElemValue( xml, "RecidiveTime", FormatValue( "%.2f", RecidiveTime ) );
    AddElemValue( xml, "GInfMaxValue", FormatValue( "%.3f", GInfMaxValue ) );
    AddElemValue( xml, "GInfLoweringFactor",
                  FormatValue( "%.3f", GInfLoweringFactor ) );
    AddElemValue( xml, "GInfDestroyCar",
                  FormatValue( "%.3f", GInfDestroyCar ) );
    xml.outOfElem();

    xml.addElem( "Opponents" );
    xml.intoElem();
    AddElemValue( xml, "OpponentDifficulty",
                  FormatValue( "%.3f", OpponentDifficulty ) );
    AddElemValue( xml, "ForceNbOpponent",
                  FormatValue( "%i", NumOpponentsOverride ) );
    AddElemBool( xml, "IAReplacePlayer", bAIReplacePlayer );
    AddElemBool( xml, "SameCarThanPlayer", bSameCarAsPlayer );
    AddElemBool( xml, "NavOppRespectLane", bNavOppRespectLane );
    xml.outOfElem();
    xml.outOfElem();
    xml.outOfElem();

    const std::string &doc = xml.getDoc();
    const uint32_t length = static_cast<uint32_t>( strlen( doc.c_str() ) );
    if ( length + 1 > bufferSize ) {
        return 0;
    }
    memcpy( pBuffer, doc.c_str(), length + 1 );
    return static_cast<int32_t>( length );
}
