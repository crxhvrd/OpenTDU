#include "shared.h"
#include "ai_config.h"

#include "core/markup.h"
#include "core/random/bcpl.h"

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

AIConfig gAIConfig; // DAT_0143f720

static constexpr float kGravity = 9.81f; // DAT_00f419bc

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

static inline bool ReadBool( const CMarkup &xml ) {
    const std::string data = xml.getData();
    return data == "true";
}

static inline float ReadFloat( const CMarkup &xml ) {
    return static_cast<float>( atof( xml.getData().c_str() ) );
}

static inline int32_t ReadInt( const CMarkup &xml ) {
    return atoi( xml.getData().c_str() );
}

static inline void
ParseBool( CMarkup &xml, const char *pName, bool &bOutValue ) {
    if ( xml.findElem( pName ) ) {
        bOutValue = ReadBool( xml );
    }
}

static inline void
ParseFloat( CMarkup &xml, const char *pName, float &outValue ) {
    if ( xml.findElem( pName ) ) {
        outValue = ReadFloat( xml );
    }
}

static inline void
ParseInt( CMarkup &xml, const char *pName, int32_t &outValue ) {
    if ( xml.findElem( pName ) ) {
        outValue = ReadInt( xml );
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
      Unknown0xBC( 1.0f ),
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
      Unknown0xF4( 100.0f ),
      Unknown0xF8( 300.0f ),
      Unknown0xFC( 0.8f ),
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
