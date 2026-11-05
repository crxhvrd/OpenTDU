#include "shared.h"
#include "cin_builder.h"

#include "game/cinematic/cinematic_format.h"
#include "config/gs_config.h"
#include "core/binary_helpers.h"
#include "filesystem/bank.h"
#include "filesystem/bank_writer.h"
#include "filesystem/gs_file.h"
#include "game/vehicle/car_state.h"
#include "physics/gs_physics.h"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

// Bank reference
static constexpr const char *kTemplateBank = "Level/Hawai/Spots/eph_l3a.bnk";

static constexpr float kMaxSampleRate = 30.0f;
static constexpr uint32_t kMaxTrackKeys = 4000u;

static constexpr uint32_t kPropPosition = 0xd7e2d8c4u;
static constexpr uint32_t kPropHeading = 0x64c8d3b1u;
static constexpr uint32_t kPropFov = 0x00766f46u;

// Scene DATA header fields rewritten by the builder
static constexpr size_t kSceneDurationOffset = 0x0;
static constexpr size_t kSceneNumActorsOffset = 0xc;

static constexpr size_t kShotKeyDuration = 0;
static constexpr size_t kShotKeyEnd = 4;
static constexpr size_t kShotKeyFadeIn = 6;
static constexpr size_t kShotKeyFadeOut = 11;

static constexpr size_t kMediaNumPropsOffset = 0x10;
static constexpr size_t kPropHeaderSize = 0xc;

static constexpr float kShowcaseCarGroundTolerance = 1.0f;
static constexpr const char *kCarLineTag = "car";

template <typename T> static void Append( std::vector<uint8_t> &out, T value ) {
    const uint8_t *pBytes = reinterpret_cast<const uint8_t *>( &value );
    out.insert( out.end(), pBytes, pBytes + sizeof( T ) );
}

struct ChunkSpan {
    size_t Offset = 0; // payload
    uint32_t Size = 0;
};

static bool FindChunk( const std::vector<uint8_t> &blob,
                       const uint32_t magic,
                       ChunkSpan &outSpan ) {
    size_t cursor = 0;
    while ( cursor + sizeof( CinematicChunk ) <= blob.size() ) {
        CinematicChunk chunk;
        std::memcpy( &chunk, blob.data() + cursor, sizeof( chunk ) );
        if ( chunk.Magic == magic ) {
            outSpan.Offset = cursor + sizeof( CinematicChunk );
            outSpan.Size = chunk.Size;
            return outSpan.Offset + chunk.Size <= blob.size();
        }
        cursor += sizeof( CinematicChunk ) + chunk.Size;
    }
    return false;
}

static void AppendChunk( std::vector<uint8_t> &blob,
                         const uint32_t magic,
                         const uint8_t *pPayload,
                         const size_t size ) {
    Append<uint32_t>( blob, magic );
    Append<uint32_t>( blob, uint32_t( size ) );
    blob.insert( blob.end(), pPayload, pPayload + size );
}

static bool HasSuffixI( const std::string &string, const char *pSuffix ) {
    const size_t suffixLength = strlen( pSuffix );
    return string.size() >= suffixLength &&
           StringCmpI( string.c_str() + string.size() - suffixLength,
                       pSuffix ) == 0;
}

static void SamplePathRelative( const std::vector<CinCameraKey> &keys,
                                const double time,
                                const bool bBeforeCut,
                                Eigen::Vector3d &outPosition,
                                Eigen::Vector3d &outTarget,
                                double &outFovDegrees ) {
    const Eigen::Vector3d origin = keys.front().Position.cast<double>();
    auto position = [&]( const CinCameraKey &key ) -> Eigen::Vector3d {
        return key.Position.cast<double>() - origin;
    };
    auto target = [&]( const CinCameraKey &key ) -> Eigen::Vector3d {
        return key.Target.cast<double>() - origin;
    };

    // Shot [first, last]
    size_t first = 0;
    for ( size_t k = 1; k < keys.size(); k++ ) {
        const double cutTime = keys[k].Time;
        if ( keys[k].bCut &&
             ( cutTime < time || ( cutTime == time && !bBeforeCut ) ) ) {
            first = k;
        }
    }
    size_t last = first;
    while ( last + 1 < keys.size() && !keys[last + 1].bCut ) {
        last++;
    }

    auto setKey = [&]( const CinCameraKey &key ) {
        outPosition = position( key );
        outTarget = target( key );
        outFovDegrees = key.FovDegrees;
    };
    if ( first == last || time <= keys[first].Time ) {
        setKey( keys[first] );
        return;
    }
    if ( time >= keys[last].Time ) {
        setKey( keys[last] );
        return;
    }

    size_t i = first;
    while ( i + 1 < last && keys[i + 1].Time <= time ) {
        i++;
    }
    const CinCameraKey &k0 = keys[i];
    const CinCameraKey &k1 = keys[i + 1];
    const double span = std::max( double( k1.Time - k0.Time ), 1e-4 );
    const double t = ( time - k0.Time ) / span;

    auto tangent = [&]( const size_t index, auto getter ) {
        const size_t previous = ( index > first ) ? index - 1 : index;
        const size_t next = ( index < last ) ? index + 1 : index;
        const double dt =
            std::max( double( keys[next].Time - keys[previous].Time ), 1e-4 );
        return Eigen::Vector3d(
            ( getter( keys[next] ) - getter( keys[previous] ) ) / dt * span );
    };
    auto hermite = [&]( auto getter ) {
        const Eigen::Vector3d m0 = tangent( i, getter );
        const Eigen::Vector3d m1 = tangent( i + 1, getter );
        const double t2 = t * t;
        const double t3 = t2 * t;
        return Eigen::Vector3d( ( 2.0 * t3 - 3.0 * t2 + 1.0 ) * getter( k0 ) +
                                ( t3 - 2.0 * t2 + t ) * m0 +
                                ( -2.0 * t3 + 3.0 * t2 ) * getter( k1 ) +
                                ( t3 - t2 ) * m1 );
    };

    outPosition = hermite( position );
    outTarget = hermite( target );
    outFovDegrees = k0.FovDegrees + ( k1.FovDegrees - k0.FovDegrees ) * t;
}

void CinBuilder::SamplePath( const std::vector<CinCameraKey> &keys,
                             const float time,
                             Eigen::Vector3f &outPosition,
                             Eigen::Vector3f &outTarget,
                             float &outFovDegrees ) {
    if ( keys.empty() ) {
        return;
    }

    Eigen::Vector3d position, target;
    double fov = 0.0;
    SamplePathRelative( keys, time, false, position, target, fov );

    const Eigen::Vector3d origin = keys.front().Position.cast<double>();
    outPosition = ( position + origin ).cast<float>();
    outTarget = ( target + origin ).cast<float>();
    outFovDegrees = float( fov );
}

std::string CinBuilder::GetAnchorPath( const char *pBankPath ) {
    return std::string( pBankPath ) + ".anchor";
}

bool CinBuilder::LoadAnchor( const char *pBankPath,
                             Eigen::Vector3f &outAnchor ) {
    std::ifstream file( GetAnchorPath( pBankPath ) );
    file >> outAnchor.x() >> outAnchor.y() >> outAnchor.z();
    return bool( file );
}

ShowcaseCarInstance::ShowcaseCarInstance() : bPending( false ) {}

ShowcaseCarInstance::~ShowcaseCarInstance() { destroy(); }

void ShowcaseCarInstance::spawn( const CinShowcaseCar &car ) {
    destroy();
    showcaseCar = car;
    const int32_t carID = car.CarID;
    if ( carID < 0 ) {
        return;
    }

    bPending = true;
}

void ShowcaseCarInstance::destroy() {
    brandCar.destroy();
    bPending = false;
}

void ShowcaseCarInstance::tick() {
    if ( bPending && gpPhysics != nullptr ) {
        const Eigen::Vector3f &position = showcaseCar.Position;
        const float groundHeight =
            gpPhysics->getGroundHeight( position.x(), position.z() );
        if ( std::abs( groundHeight - position.y() ) <=
             kShowcaseCarGroundTolerance ) {
            bPending = false;
            brandCar.load( nullptr, showcaseCar.CarID, 0,
                           showcaseCar.getSpawnMatrix() );
        }
    }

    brandCar.tickInstanciation();
}

CarState *ShowcaseCarInstance::getCarState() const {
    return brandCar.getCarState();
}

VehicleBase *ShowcaseCarInstance::getVehicle() const {
    CarState *pCarState = brandCar.getCarState();
    return ( pCarState != nullptr ) ? pCarState->getVehicleInstance() : nullptr;
}

bool CinBuilder::SaveKeys( const char *pPath,
                           const std::vector<CinCameraKey> &keys,
                           const CinShowcaseCar &car ) {
    std::error_code errorCode;
    std::filesystem::create_directories(
        std::filesystem::path( pPath ).parent_path(), errorCode );

    std::ofstream file( pPath );
    if ( !file ) {
        return false;
    }
    file << std::setprecision( 9 );
    file << "# time posX posY posZ targetX targetY targetZ fovDegrees cut\n";
    file << "# " << kCarLineTag << " carID posX posY posZ yawDegrees\n";
    if ( car.CarID >= 0 ) {
        file << kCarLineTag << ' ' << car.CarID << ' ' << car.Position.x()
             << ' ' << car.Position.y() << ' ' << car.Position.z() << ' '
             << car.YawDegrees << '\n';
    }
    for ( const CinCameraKey &key : keys ) {
        file << key.Time << ' ' << key.Position.x() << ' ' << key.Position.y()
             << ' ' << key.Position.z() << ' ' << key.Target.x() << ' '
             << key.Target.y() << ' ' << key.Target.z() << ' ' << key.FovDegrees
             << ' ' << int32_t( key.bCut ) << '\n';
    }
    return bool( file );
}

bool CinBuilder::LoadKeys( const char *pPath,
                           std::vector<CinCameraKey> &outKeys,
                           CinShowcaseCar &outCar ) {
    std::ifstream file( pPath );
    if ( !file ) {
        return false;
    }

    outKeys.clear();
    outCar = CinShowcaseCar();
    std::string line;
    while ( std::getline( file, line ) ) {
        if ( line.empty() || line[0] == '#' ) {
            continue;
        }
        std::istringstream stream( line );
        if ( line.rfind( kCarLineTag, 0 ) == 0 ) {
            std::string tag;
            CinShowcaseCar car;
            stream >> tag >> car.CarID >> car.Position.x() >>
                car.Position.y() >> car.Position.z() >> car.YawDegrees;
            if ( !stream ) {
                continue;
            }
            outCar = car;
            continue;
        }
        CinCameraKey key;
        stream >> key.Time >> key.Position.x() >> key.Position.y() >>
            key.Position.z() >> key.Target.x() >> key.Target.y() >>
            key.Target.z() >> key.FovDegrees;
        if ( stream ) {
            // Optional (older files)
            int32_t cut = 0;
            key.bCut = ( stream >> cut ) && cut != 0;
            outKeys.push_back( key );
        }
    }
    std::stable_sort( outKeys.begin(), outKeys.end(),
                      []( const CinCameraKey &a, const CinCameraKey &b ) {
                          return a.Time < b.Time;
                      } );
    return true;
}

static Eigen::Quaternionf ComputeHeading( const Eigen::Vector3d &position,
                                          const Eigen::Vector3d &target ) {
    // BuildRotationMatrix rows: right, up, forward (the cinematic camera looks
    // down row 2)
    Eigen::Vector3d forward = target - position;
    if ( forward.squaredNorm() < 1e-12 ) {
        forward = Eigen::Vector3d::UnitZ();
    }
    forward.normalize();

    Eigen::Vector3d right = Eigen::Vector3d::UnitY().cross( forward );
    if ( right.squaredNorm() < 1e-12 ) {
        right = Eigen::Vector3d::UnitX();
    }
    right.normalize();
    const Eigen::Vector3d up = forward.cross( right );

    Eigen::Matrix3d rotation;
    rotation.row( 0 ) = right;
    rotation.row( 1 ) = up;
    rotation.row( 2 ) = forward;
    return Eigen::Quaterniond( rotation ).normalized().cast<float>();
}

// Property = {i16 subtype, i16, i32 advance, u32 type hash}, then the track:
// {u16 key count, u16 track size (bit 0 clear: float key times)}, key times,
// key values
static void AppendProperty( std::vector<uint8_t> &out,
                            const uint8_t *pTemplateHeader,
                            const std::vector<float> &times,
                            const std::vector<float> &values ) {
    const uint32_t trackSize =
        uint32_t( 4 + ( times.size() + values.size() ) * sizeof( float ) );
    const size_t start = out.size();
    out.insert( out.end(), pTemplateHeader, pTemplateHeader + 4 );
    Append<uint32_t>( out, uint32_t( kPropHeaderSize + trackSize ) );
    out.insert( out.end(), pTemplateHeader + 8, pTemplateHeader + 12 );
    Append<uint16_t>( out, uint16_t( times.size() ) );
    Append<uint16_t>( out, uint16_t( trackSize ) );
    for ( const float time : times ) {
        Append<float>( out, time );
    }
    for ( const float value : values ) {
        Append<float>( out, value );
    }
    OTDU_ASSERT( out.size() - start == kPropHeaderSize + trackSize );
}

static bool BuildScene( const std::vector<uint8_t> &templateScene,
                        const std::vector<CinCameraKey> &keys,
                        const float duration,
                        std::vector<uint8_t> &outScene,
                        std::string &outError ) {
    ChunkSpan version, data, refs;
    if ( !FindChunk( templateScene, kCinematicSceneMagic, version ) ||
         !FindChunk( templateScene, kCinematicDataMagic, data ) ||
         !FindChunk( templateScene, kCinematicRefsMagic, refs ) ) {
        outError = "template scene has no SCE!/DATA/REFS chunk";
        return false;
    }

    const uint8_t *pData = templateScene.data() + data.Offset;
    const uint8_t *pCamera = pData + kCinSceneMediaArrayOffset;
    CinSceneMediaHeader camera;
    std::memcpy( &camera, pCamera, sizeof( camera ) );
    const uint32_t propOffset = Read<uint32_t>( pCamera, 4 );
    if ( camera.TypeHash != kCinMediaCamera ||
         Read<uint32_t>( pCamera, kMediaNumPropsOffset ) != 3 ) {
        outError = "template scene does not start with a 3-track camera";
        return false;
    }

    // Template property headers, by type
    const uint8_t *pPositionHeader = nullptr;
    const uint8_t *pHeadingHeader = nullptr;
    const uint8_t *pFovHeader = nullptr;
    const uint8_t *pProp = pCamera + propOffset;
    for ( int32_t i = 0; i < 3; i++ ) {
        switch ( Read<uint32_t>( pProp, 8 ) ) {
        case kPropPosition:
            pPositionHeader = pProp;
            break;
        case kPropHeading:
            pHeadingHeader = pProp;
            break;
        case kPropFov:
            pFovHeader = pProp;
            break;
        default:
            break;
        }
        pProp += Read<uint32_t>( pProp, 4 );
    }
    if ( pPositionHeader == nullptr || pHeadingHeader == nullptr ||
         pFovHeader == nullptr ) {
        outError = "template camera lacks a position/heading/fov track";
        return false;
    }

    // Each cut is a pair of keys at the cut time (the shot it ends, then the
    // one it starts): the reader interpolates from the last key at or before
    // the time, so the camera jumps with no in-between frame
    std::vector<float> cutTimes;
    for ( size_t k = 1; k < keys.size(); k++ ) {
        if ( keys[k].bCut && keys[k].Time > 0.0f && keys[k].Time < duration ) {
            cutTimes.push_back( keys[k].Time );
        }
    }
    cutTimes.erase( std::unique( cutTimes.begin(), cutTimes.end() ),
                    cutTimes.end() );
    const uint32_t numCutKeys = uint32_t( cutTimes.size() ) * 2u;
    if ( numCutKeys + 2u >= kMaxTrackKeys ) {
        outError = "too many cuts";
        return false;
    }

    struct Sample {
        float Time;
        bool bBeforeCut;
    };
    std::vector<Sample> samples;
    const float sampleRate = std::min(
        kMaxSampleRate, float( kMaxTrackKeys - 1 - numCutKeys ) / duration );
    const uint32_t numUniformSamples = uint32_t( duration * sampleRate ) + 2;
    // Uniform samples closer than this to a cut are dropped (key times must
    // increase)
    const float cutMargin = 0.25f / sampleRate;
    size_t nextCut = 0;
    for ( uint32_t i = 0; i < numUniformSamples; i++ ) {
        const float time = std::min( duration, float( i ) * duration /
                                                   ( numUniformSamples - 1 ) );
        while ( nextCut < cutTimes.size() && cutTimes[nextCut] <= time ) {
            samples.push_back( { cutTimes[nextCut], true } );
            samples.push_back( { cutTimes[nextCut], false } );
            nextCut++;
        }
        const bool bNearCut =
            ( nextCut > 0 && time - cutTimes[nextCut - 1] < cutMargin ) ||
            ( nextCut < cutTimes.size() &&
              cutTimes[nextCut] - time < cutMargin );
        if ( !bNearCut && ( samples.empty() || time > samples.back().Time ) ) {
            samples.push_back( { time, false } );
        }
    }

    std::vector<float> times, positions, headings, fovs;
    Eigen::Quaternionf previousHeading = Eigen::Quaternionf::Identity();
    for ( size_t i = 0; i < samples.size(); i++ ) {
        const Sample &sample = samples[i];
        Eigen::Vector3d relativePosition, relativeTarget;
        double fov = 0.0;
        SamplePathRelative( keys, sample.Time, sample.bBeforeCut,
                            relativePosition, relativeTarget, fov );
        // Small values: the reader's linear interpolation of world space
        // coordinates (~60 km) rounds to a different float from frame to frame
        const Eigen::Vector3f position = relativePosition.cast<float>();

        Eigen::Quaternionf heading =
            ComputeHeading( relativePosition, relativeTarget );
        if ( i > 0 && heading.dot( previousHeading ) < 0.0f ) {
            heading.coeffs() = -heading.coeffs();
        }
        previousHeading = heading;

        times.push_back( sample.Time );
        positions.insert( positions.end(),
                          { position.x(), position.y(), position.z() } );
        headings.insert( headings.end(), { heading.x(), heading.y(),
                                           heading.z(), heading.w() } );
        fovs.push_back( float( fov ) );
    }

    std::vector<uint8_t> properties;
    AppendProperty( properties, pPositionHeader, times, positions );
    AppendProperty( properties, pHeadingHeader, times, headings );
    AppendProperty( properties, pFovHeader, times, fovs );

    // Scene DATA: header, then the camera alone (the template's door actors
    // are dropped)
    std::vector<uint8_t> sceneData( pData, pData + kCinSceneMediaArrayOffset );
    Write<float>( sceneData.data(), kSceneDurationOffset, duration );
    Write<int32_t>( sceneData.data(), kCinSceneMediaCountOffset, 1 );
    Write<int32_t>( sceneData.data(), kSceneNumActorsOffset, 0 );

    const size_t cameraStart = sceneData.size();
    sceneData.insert( sceneData.end(), pCamera, pCamera + propOffset );
    sceneData.insert( sceneData.end(), properties.begin(), properties.end() );
    Write<uint32_t>( sceneData.data(), cameraStart,
                     uint32_t( sceneData.size() - cameraStart ) );

    outScene.clear();
    AppendChunk( outScene, kCinematicSceneMagic,
                 templateScene.data() + version.Offset, version.Size );
    AppendChunk( outScene, kCinematicDataMagic, sceneData.data(),
                 sceneData.size() );
    AppendChunk( outScene, kCinematicRefsMagic,
                 templateScene.data() + refs.Offset, refs.Size );
    return true;
}

static bool BuildCinematic( const std::vector<uint8_t> &templateCin,
                            const float duration,
                            std::vector<uint8_t> &outCin,
                            std::string &outError ) {
    ChunkSpan data;
    if ( !FindChunk( templateCin, kCinematicDataMagic, data ) ||
         data.Offset + data.Size != templateCin.size() ) {
        outError = "template cinematic does not end with a DATA chunk";
        return false;
    }

    outCin = templateCin;
    uint8_t *pData = outCin.data() + data.Offset;
    CinematicHeader header;
    std::memcpy( &header, pData, sizeof( header ) );
    if ( header.NumSources != 1 || header.NumMediaControllers != 1 ) {
        outError = "template cinematic is not a single source cinematic";
        return false;
    }

    // Shot controller (type 1): {u16 type, u16, u32 advance, u32}, then its
    // track; the template has a single shot
    uint8_t *pController = pData + sizeof( CinematicHeader ) +
                           header.NumSources * sizeof( CinSourceEntry );
    const uint16_t *pTrack =
        reinterpret_cast<const uint16_t *>( pController + 0xc );
    const uint32_t controllerSize = Read<uint32_t>( pController, 4 );
    if ( Read<uint16_t>( pController, 0 ) != 1 || pTrack[0] != 1 ||
         ( pTrack[1] & 1 ) != 0 || controllerSize == 0 ||
         pController + controllerSize > outCin.data() + outCin.size() ) {
        outError = "template cinematic does not have a single shot";
        return false;
    }
    // Float key times: the values follow the single key time
    float *pShotKey = reinterpret_cast<float *>( pController + 0xc + 4 + 4 );
    pShotKey[kShotKeyDuration] = duration;
    pShotKey[kShotKeyEnd] = duration;
    pShotKey[kShotKeyFadeIn] = 0.0f;
    pShotKey[kShotKeyFadeOut] = 0.0f;

    // The camera is the only entity left, and the template's message tracks
    // (the controllers past the shot) are dropped
    header.TotalLength[0] = duration;
    header.TotalLength[1] = duration;
    header.NumSubAnimatedProps = 0;
    header.NumMessageKeys = 0;
    header.NumEntities = 1;
    header.NumActors = 0;
    std::memcpy( pData, &header, sizeof( header ) );

    const size_t dataSize = size_t( pController + controllerSize - pData );
    outCin.resize( data.Offset + dataSize );
    Write<uint32_t>( outCin.data(), data.Offset - sizeof( uint32_t ),
                     uint32_t( dataSize ) );
    return true;
}

static int32_t
FindEntryIndex( BankInstance &bank,
                const std::function<bool( ParsedBankEntry & )> &match ) {
    for ( int32_t i = 0; i < bank.getNumElements(); i++ ) {
        ParsedBankEntry entry;
        bank.getEntry( i, entry );
        if ( entry.pData != nullptr && match( entry ) ) {
            return i;
        }
    }
    return -1;
}

bool CinBuilder::BuildCameraBank( const std::vector<CinCameraKey> &keys,
                                  const char *pOutputPath,
                                  std::string &outError ) {
    if ( keys.size() < 2 || keys.back().Time <= keys.front().Time ) {
        outError = "at least two keys with increasing times are needed";
        return false;
    }
    if ( keys.front().Time != 0.0f ) {
        outError = "the first key must be at time 0";
        return false;
    }
    const float duration = keys.back().Time;

    std::string templatePath = gpConfig->getResRootPath();
    templatePath += kTemplateBank;
    void *pTemplate = nullptr;
    uint32_t templateSize = 0;
    if ( !gpFile->loadBank( templatePath.c_str(), &pTemplate, &templateSize,
                            0 ) ) {
        outError = "cannot read " + templatePath;
        return false;
    }
    std::vector<uint8_t> templateBank( static_cast<uint8_t *>( pTemplate ),
                                       static_cast<uint8_t *>( pTemplate ) +
                                           templateSize );
    TestDrive::Free( pTemplate );

    BankInstance bank;
    if ( bank.load( templateBank.data(), uint32_t( templateBank.size() ),
                    false ) != 0 ) {
        outError = "cannot parse " + templatePath;
        return false;
    }

    const int32_t cinIndex = FindEntryIndex( bank, []( ParsedBankEntry &e ) {
        return e.Size >= 4 &&
               *static_cast<const uint32_t *>( e.pData ) == kCinematicMagic &&
               HasSuffixI( e.Filename, kCinematicSuffix );
    } );
    if ( cinIndex < 0 ) {
        outError = "template bank has no house tour cinematic";
        return false;
    }
    ParsedBankEntry cinEntry;
    bank.getEntry( cinIndex, cinEntry );
    const std::vector<uint8_t> templateCin(
        static_cast<uint8_t *>( cinEntry.pData ),
        static_cast<uint8_t *>( cinEntry.pData ) + cinEntry.Size );

    // The scene is the cinematic's single source, referenced by file name
    ChunkSpan refs;
    if ( !FindChunk( templateCin, kCinematicRefsMagic, refs ) ) {
        outError = "template cinematic has no REFS chunk";
        return false;
    }
    std::string sceneName;
    const uint8_t *pRef = templateCin.data() + refs.Offset + 4;
    for ( int32_t i = 0; i < Read<int32_t>( templateCin.data(), refs.Offset );
          i++ ) {
        const uint8_t type = pRef[0];
        const uint8_t flags = pRef[1];
        if ( type == 3 && ( flags & 2 ) != 0 ) {
            sceneName = reinterpret_cast<const char *>(
                pRef + ( ( flags & 1 ) != 0 ? 0xc : 4 ) );
        }
        pRef += Read<uint16_t>( pRef, 2 );
    }
    const int32_t sceneIndex = FindEntryIndex( bank, [&]( ParsedBankEntry &e ) {
        return StringCmpI( e.Filename.c_str(), sceneName.c_str() ) == 0;
    } );
    if ( sceneIndex < 0 ) {
        outError = "template scene '" + sceneName + "' not found";
        return false;
    }
    ParsedBankEntry sceneEntry;
    bank.getEntry( sceneIndex, sceneEntry );
    const std::vector<uint8_t> templateScene(
        static_cast<uint8_t *>( sceneEntry.pData ),
        static_cast<uint8_t *>( sceneEntry.pData ) + sceneEntry.Size );

    std::vector<uint8_t> cin, scene;
    if ( !BuildCinematic( templateCin, duration, cin, outError ) ||
         !BuildScene( templateScene, keys, duration, scene, outError ) ) {
        return false;
    }

    std::vector<uint8_t> withCin, withScene;
    if ( !ReplaceBankEntry( templateBank, cinIndex, cin, withCin, outError ) ||
         !ReplaceBankEntry( withCin, sceneIndex, scene, withScene,
                            outError ) ) {
        return false;
    }

    // The reader finds them by name and content
    BankInstance check;
    ParsedBankEntry checkCin, checkScene;
    if ( check.load( withScene.data(), uint32_t( withScene.size() ), false ) !=
             0 ||
         check.getEntry( cinIndex, checkCin ) != 0 ||
         check.getEntry( sceneIndex, checkScene ) != 0 ||
         size_t( checkCin.Size ) != cin.size() ||
         std::memcmp( checkCin.pData, cin.data(), cin.size() ) != 0 ||
         size_t( checkScene.Size ) != scene.size() ||
         std::memcmp( checkScene.pData, scene.data(), scene.size() ) != 0 ) {
        outError = "the rewritten bank does not read back";
        return false;
    }

    std::error_code errorCode;
    std::filesystem::create_directories(
        std::filesystem::path( pOutputPath ).parent_path(), errorCode );
    std::ofstream file( pOutputPath, std::ios::binary );
    file.write( (const char *)withScene.data(), withScene.size() );
    std::ofstream anchorFile( GetAnchorPath( pOutputPath ) );
    anchorFile << std::setprecision( 9 ) << keys.front().Position.x() << ' '
               << keys.front().Position.y() << ' ' << keys.front().Position.z()
               << '\n';
    if ( !file || !anchorFile ) {
        outError = std::string( "cannot write " ) + pOutputPath;
        return false;
    }

    OTDU_LOG_INFO( "Built camera cinematic '%s' (%.1f s, %zu keys)\n",
                   pOutputPath, duration, keys.size() );
    return true;
}
