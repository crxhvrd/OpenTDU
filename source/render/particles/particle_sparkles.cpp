#include "shared.h"
#include "particle_sparkles.h"

#include "gs_particles.h"
#include "particle_instance.h"

#include "core/maths_helper.h"
#include "physics/gs_physics.h"
#include "render/camera/gs_camera.h"
#include "render/gs_render.h"
#include "render/material.h"
#include "render/postfx/gs_postfx.h"
#include "render/render_context.h"
#include "world/gs_world_streaming.h"
#include "world/streaming_manager.h"

#include <cmath>
#include <cstdlib>

static constexpr int32_t kMaxSparkleInstances = 12;
static constexpr uint32_t kSparkPoolSize = 0x2580;
static constexpr uint32_t kSparkleVerticesPerInstance = 1000;
static constexpr uint32_t kSparkleIndicesPerInstance = 0x4b0;

static constexpr float kRandToUnit = 1.0f / 32767.0f; // DAT_00f41714
static constexpr float kImpactSpeedToKmh = 3600.0f;   // DAT_00f41740
static constexpr float kImpactSpeedScale = 0.001f;    // FLOAT_00f4167c
static constexpr float kImpactMinSpeed = 0.01f;       // DAT_00f416d4
static constexpr float kEmitCooldown = 0.04f;         // DAT_00f41980
static constexpr float kMinWidthScale = 0.025f;       // DAT_00f41734
static constexpr float kMaxWidthScale = 0.035f;       // DAT_00f41d60
static constexpr float kMaxSpeedScale = 14.0f;        // DAT_00f420cc
static constexpr float kMinSpeedScale = 9.0f;         // DAT_00f4192c
static constexpr float kCountScale = 20.0f;           // DAT_00f03ee0
static constexpr float kDirectionSpread = 0.2f;       // DAT_00f41730
static constexpr float kLifetimeRange = 0.8000001f;   // DAT_00f420c8
static constexpr float kLifetimeBase = 1.4f;          // DAT_00f41f2c
static constexpr float kGravity = 9.8f;               // DAT_00f420c4
static constexpr float kGroundProbeLift = 0.2f;       // DAT_00f41730
static constexpr float kNoGroundHeight = -100000.0f;  // DAT_00f418fc
static constexpr float kParallelEpsilon = 0.001f;     // FLOAT_00f4167c
static constexpr float kDefaultRestitution = 0.4f;    // DAT_00f4175c
static constexpr float kColorFadeTime = 2.2f;         // 0x400ccccd
static constexpr float kColorFadeSplit = 0.8f;        // DAT_00f416d0
static constexpr float kColorFadeOutScale = 5.0f;     // DAT_00f41750
static constexpr float kColorWhite = 255.0f;          // DAT_00f41688
static constexpr float kColorEmber = 200.0f;          // DAT_00f4170c

// Impact speed (km/h) mapped to { key, spark count, spark speed, trail length }
static constexpr float kImpactCurve[][4] = {
    // DAT_00f8afb8
    {0.0f,   0.2f, 0.3f, 0.5f},
    {60.0f,  0.4f, 0.6f, 0.5f},
    {140.0f, 0.7f, 0.8f, 1.0f},
    {200.0f, 1.0f, 1.0f, 1.0f},
    {300.0f, 1.0f, 1.5f, 1.2f},
    {-1.0f,  0.0f, 0.0f, 0.0f},
};

static inline float SparkRand01() {
    return static_cast<float>( rand() & 0x7fff ) * kRandToUnit;
}

static void SampleImpactCurve( float impactSpeed, float *pOut ) {
    // FUN_0080aac0
    const float key = impactSpeed * kImpactSpeedToKmh * kImpactSpeedScale;

    const float *pLow = kImpactCurve[0];
    const float *pHigh = kImpactCurve[0];
    if ( 0.0f <= kImpactCurve[0][0] ) {
        for ( ;; ) {
            if ( key < pHigh[0] ) {
                break;
            }
            pLow = pHigh;
            pHigh += 4;
            if ( pHigh[0] < 0.0f ) {
                break;
            }
        }

        if ( pLow != pHigh && 0.0f <= pHigh[0] ) {
            const float t = ( key - pLow[0] ) / ( pHigh[0] - pLow[0] );
            for ( int32_t i = 1; i < 4; i++ ) {
                pOut[i] = ( pHigh[i] - pLow[i] ) * t + pLow[i];
            }
            return;
        }
    }

    for ( int32_t i = 1; i < 4; i++ ) {
        pOut[i] = pLow[i];
    }
}

static void UpdateSparkleGroundPlane( ParticleInstance *pInstance ) {
    // FUN_0080ba50
    Eigen::Vector4f probe = pInstance->Matrix.row( 3 ).transpose();
    probe.y() += kGroundProbeLift;

    float height = kNoGroundHeight;
    Eigen::Vector4f normal( 0.0f, 1.0f, 0.0f, 0.0f );
    if ( gpPhysics != nullptr ) {
        gpPhysics->probeSurface( probe, false, height, normal );
    }
    if ( height <= kNoGroundHeight ) {
        height = SampleHeightmapSurface( probe, &normal );
    }

    if ( kNoGroundHeight < height ) {
        probe.y() = height + pInstance->Scale * kMaxWidthScale;
        const Eigen::Vector4f shift =
            ( gpWorldStreaming != nullptr && gpWorldStreaming->isEnabled() )
                ? gpWorldStreaming->getPosition()
                : Eigen::Vector4f( 0.0f, 0.0f, 0.0f, 1.0f );
        pInstance->SparkGroundPlane = normal;
        pInstance->SparkGroundPlane.w() = -normal.dot( probe - shift );
        return;
    }
    pInstance->bSparkCollide = false;
}

static bool IntersectGroundPlane( const Eigen::Vector4f &plane,
                                  const Eigen::Vector4f &from,
                                  const Eigen::Vector4f &to,
                                  Eigen::Vector4f &hit ) {
    // FUN_0080abf0
    const float fromDistance = plane.y() * from.y() + plane.z() * from.z() +
                               plane.x() * from.x() + plane.w();
    if ( fromDistance < 0.0f ) {
        return false;
    }
    const float toDistance = plane.y() * to.y() + plane.z() * to.z() +
                             plane.x() * to.x() + plane.w();
    if ( 0.0f < toDistance ) {
        return false;
    }

    const Eigen::Vector4f delta = ToDirection( to - from );
    const float denominator =
        plane.x() * delta.x() + plane.y() * delta.y() + plane.z() * delta.z();
    if ( -kParallelEpsilon < denominator && denominator < kParallelEpsilon ) {
        return false;
    }

    const float t = -( fromDistance / denominator );
    hit = ToDirection( from + delta * t );
    return true;
}

static ColorRGBA GetSparkColor( float life, float fadeTime ) {
    // FUN_00422de0
    float t = 1.0f - life / fadeTime;
    float fromGrey = kColorWhite;
    float toGrey = kColorEmber;
    if ( t < kColorFadeSplit ) {
        t = t * 2.0f;
    } else {
        t = ( t - kColorFadeSplit ) * kColorFadeOutScale;
        fromGrey = kColorEmber;
        toGrey = 0.0f;
    }
    const float inverse = 1.0f - t;

    const float alphaFrom = kColorWhite * inverse;
    const float alphaTo = kColorWhite * t;
    const float greyFrom = fromGrey * inverse;
    const float greyTo = toGrey * t;
    const uint8_t alpha =
        static_cast<uint8_t>( static_cast<int64_t>( alphaFrom + alphaTo ) );
    const uint8_t grey =
        static_cast<uint8_t>( static_cast<int64_t>( greyFrom + greyTo ) );
    return ColorRGBA( grey, grey, grey, alpha );
}

static void SimulateSparkleInstance( ParticleInstance *pInstance,
                                     float deltaTime ) {
    // FUN_0080bbb0
    if ( 1.0f < pInstance->Density ) {
        pInstance->Density = 1.0f;
    }
    if ( deltaTime <= 0.0f ) {
        return;
    }

    const Eigen::Vector4f position =
        ToDirection( pInstance->Matrix.row( 3 ).transpose() );
    const Eigen::Vector4f shift = ToDirection(
        ( gpWorldStreaming != nullptr && gpWorldStreaming->isEnabled() )
            ? gpWorldStreaming->getPosition()
            : Eigen::Vector4f::Zero() );

    bool bEmit = false;
    int32_t numEmitted = 0;
    int32_t maxEmitted = 0;
    float minWidth = 0.0f;
    float maxWidth = 0.0f;
    float minSpeed = 0.0f;
    float maxSpeed = 0.0f;
    Eigen::Vector4f trailEnd = Eigen::Vector4f::Zero();
    Eigen::Vector4f directionMin = Eigen::Vector4f::Zero();
    Eigen::Vector4f directionMax = Eigen::Vector4f::Zero();

    if ( pInstance->State != eParticleState::PS_Killed &&
         pInstance->SparkCooldown <= 0.0f ) {
        Eigen::Vector4f direction = ToDirection( -pInstance->SparkleVelocity );
        const float impactSpeed = direction.head<3>().norm();
        if ( kImpactMinSpeed < impactSpeed ) {
            direction /= direction.norm();
            bEmit = true;
            pInstance->SparkCooldown = kEmitCooldown;
            if ( pInstance->bSparkCollide ) {
                UpdateSparkleGroundPlane( pInstance );
            }

            float impact[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            SampleImpactCurve( impactSpeed, impact );

            minWidth = pInstance->Scale * kMinWidthScale;
            maxWidth = pInstance->Scale * kMaxWidthScale;
            const float speedScale = pInstance->Speed * impact[2];
            maxSpeed = speedScale * kMaxSpeedScale;
            minSpeed = speedScale * kMinSpeedScale;
            maxEmitted = static_cast<int32_t>( impact[1] * pInstance->Density *
                                               kCountScale );

            trailEnd = ToDirection( position + direction * impact[3] );
            directionMax = ToDirection(
                direction + Eigen::Vector4f::Constant( kDirectionSpread ) );
            directionMin = direction;
            directionMin.x() -= kDirectionSpread;
            directionMin.z() -= kDirectionSpread;
        }
    }

    pInstance->NumLiveSparks = 0;
    for ( Spark &spark : pInstance->Sparks ) {
        if ( 0.0f <= spark.Life ) {
            spark.PreviousLife = spark.Life;
            spark.Life -= deltaTime;
        }

        if ( spark.Life < 0.0f && bEmit ) {
            spark.Width = SparkRand01() * ( maxWidth - minWidth ) + minWidth;

            Eigen::Vector4f direction = Eigen::Vector4f::Zero();
            for ( int32_t axis = 0; axis < 3; axis++ ) {
                direction[axis] = SparkRand01() * ( directionMax[axis] -
                                                    directionMin[axis] ) +
                                  directionMin[axis];
            }
            direction /= direction.norm();

            const float speed =
                SparkRand01() * ( maxSpeed - minSpeed ) + minSpeed;
            spark.Velocity = ToDirection( direction * speed );

            const float lifetime =
                SparkRand01() * kLifetimeRange + kLifetimeBase;
            spark.Lifetime = lifetime;
            spark.Life = lifetime;
            spark.PreviousLife = lifetime;

            for ( int32_t axis = 0; axis < 3; axis++ ) {
                spark.Position[axis] =
                    ( SparkRand01() * ( position[axis] - trailEnd[axis] ) +
                      trailEnd[axis] ) -
                    shift[axis];
            }
            spark.GroundPlane = pInstance->SparkGroundPlane;

            numEmitted++;
            if ( maxEmitted <= numEmitted ) {
                bEmit = false;
            }
            spark.PreviousPosition = spark.Position;
        }

        if ( 0.0f <= spark.Life ) {
            Eigen::Vector4f acceleration =
                ToDirection( -spark.Velocity ) * pInstance->SparkDrag;
            acceleration.y() -= kGravity;
            spark.Velocity =
                ToDirection( spark.Velocity + acceleration * deltaTime );
            spark.OlderPosition = spark.PreviousPosition;
            spark.PreviousPosition = spark.Position;
            spark.Position =
                ToDirection( spark.Position + spark.Velocity * deltaTime );

            if ( pInstance->bSparkCollide ) {
                Eigen::Vector4f hit = Eigen::Vector4f::Zero();
                if ( IntersectGroundPlane( spark.GroundPlane,
                                           spark.PreviousPosition,
                                           spark.Position, hit ) ) {
                    const Eigen::Vector4f normal =
                        ToDirection( spark.GroundPlane );
                    const float reflection =
                        ( spark.Velocity.z() * normal.z() +
                          normal.x() * spark.Velocity.x() +
                          normal.y() * spark.Velocity.y() ) *
                        2.0f; // DAT_00f416b0
                    spark.Velocity = ToDirection(
                        ToDirection( spark.Velocity - normal * reflection ) *
                        pInstance->SparkRestitution );
                    spark.Position = hit;
                }
            }
            pInstance->NumLiveSparks++;
        }
    }

    if ( pInstance->State == eParticleState::PS_Killed &&
         pInstance->NumLiveSparks == 0 ) {
        pInstance->State = eParticleState::PS_Dead;
    }
    pInstance->SparkCooldown -= deltaTime;
}

static void BuildSparkleGeometry( ParticleInstance *pInstance,
                                  Material *pMaterial,
                                  DrawList &drawList,
                                  uint32_t &usedVertices,
                                  uint32_t &usedIndices ) {
    // FUN_0080ad80
    if ( pInstance->NumLiveSparks <= 0 || gpCamera == nullptr ||
         gpCamera->getActiveCamera() == nullptr || gpRender == nullptr ) {
        return;
    }

    const Eigen::Vector4f cameraForward = ToDirection(
        gpCamera->getActiveCamera()->getCamToWorld().row( 2 ).transpose() *
        -1.0f );

    int32_t numSegments = 2;
    switch ( gpRender->getLODQuality() ) {
    case 0:
        numSegments = 2;
        break;
    case 1:
        numSegments = 3;
        break;
    case 2:
        numSegments = 4;
        break;
    default:
        break;
    }
    const float segmentStep = 1.0f / static_cast<float>( numSegments );

    const uint32_t numSparks =
        static_cast<uint32_t>( pInstance->NumLiveSparks );
    const uint32_t numVertices = numSparks * ( numSegments * 2 + 2 );
    const uint32_t numIndices = numSparks * ( numSegments * 2 + 4 );
    if ( drawList.getMaxNumVertices() < usedVertices + numVertices ||
         drawList.getMaxNumIndex() < usedIndices + numIndices ) {
        return;
    }

    drawList.setActiveMaterial( pMaterial );
    drawList.setIdentityTransform();
    if ( !drawList.beginPrimitive( PT_TriangleStrip,
                                   static_cast<int32_t>( numVertices ) ) ) {
        return;
    }
    usedVertices += numVertices;
    usedIndices += numIndices;

    uint32_t numStrips = 0;
    for ( const Spark &spark : pInstance->Sparks ) {
        if ( numStrips == numSparks ) {
            break;
        }
        if ( spark.Life < 0.0f ) {
            continue;
        }

        const Eigen::Vector4f direction =
            ToDirection( spark.Position - spark.PreviousPosition );
        const Eigen::Vector4f heading = direction / direction.norm();
        Eigen::Vector4f side(
            heading.y() * cameraForward.z() - heading.z() * cameraForward.y(),
            heading.z() * cameraForward.x() - heading.x() * cameraForward.z(),
            heading.x() * cameraForward.y() - heading.y() * cameraForward.x(),
            0.0f );
        side = ToDirection( side / side.norm() * spark.Width );

        drawList.setDiffuse( 0, GetSparkColor( spark.Life, kColorFadeTime ) );

        float t = 0.0f;
        for ( int32_t segment = 0; segment <= numSegments; segment++ ) {
            const float inverse = 1.0f - t;
            const Eigen::Vector3f point =
                spark.OlderPosition.head<3>() * ( t * t ) +
                ( spark.PreviousPosition.head<3>() * ( inverse * t * 2.0f ) +
                  spark.Position.head<3>() * ( inverse * inverse ) );

            drawList.setTexCoords( 0, Eigen::Vector2f( t, 0.0f ) );
            drawList.pushVertex( point.x() - side.x(), point.y() - side.y(),
                                 point.z() - side.z() );
            drawList.setTexCoords( 0, Eigen::Vector2f( t, 1.0f ) );
            drawList.pushVertex( point.x() + side.x(), point.y() + side.y(),
                                 point.z() + side.z() );

            t += segmentStep;
            if ( 1.0f <= t ) {
                t = 1.0f;
            }
        }

        numStrips++;
        if ( numStrips < numSparks ) {
            drawList.breakPrimitive();
        }
    }
    drawList.commitPrimitive();
}

void InitializeSparkles( Particle *pParticle,
                         uint32_t &memoryUsed,
                         uint32_t memorySize ) {
    // FUN_0080b760
    Material *pMaterial = gpParticles->resolveParticleMaterial(
        GetIdentifier64bit( "sparkles" ) );
    pParticle->pMaterial = pMaterial;
    if ( pMaterial != nullptr ) {
        pMaterial->AlphaRef = 0x14;
        pMaterial->AlphaTest = eComparisonFunction::CF_Never;
        pMaterial->DepthWrite = 2;
        pMaterial->DepthTest = 1;

        if ( pMaterial->NumParams != 0 ) {
            MaterialParameter *pParam = pMaterial->getParameterByIndex( 0 );
            pParam->Hashcode = 0x73078865ca37336dull;
            pParam->Flags = MaterialParameterFlags{};
            pParam->Flags.NumUVMaps = 1;
            pParam->Flags.NumTextures = 1;
            pParam->Flags.bUsingNormals = 0;
            pParam->Flags.NumPrelight = 1;
            if ( gpRenderContext != nullptr ) {
                gpRenderContext->retrieveShadersForMaterial( pMaterial, 0 );
            }
        }

        pMaterial->StencilEnable = 1;
        pMaterial->StencilFail = eStencilFunction::SF_Keep;
        pMaterial->StencilZFail = eStencilFunction::SF_Keep;
        pMaterial->StencilPass = eStencilFunction::SF_Replace;
        pMaterial->StencilFunc = eComparisonFunction::CF_Always;
        pMaterial->StencilRef = 0xffffffff;
        pMaterial->StencilMask = 0xffffffff;
        if ( pMaterial->StencilWriteMask == 0xffffffff ) {
            pMaterial->StencilWriteMask = 0;
        }
        if ( gpPostFX != nullptr ) {
            const uint32_t layer = static_cast<uint32_t>(
                gpPostFX->getEffectLayer<PFXS_GlowPause>() );
            pMaterial->StencilWriteMask |= 1u << ( layer & 0x1f );
        }
    }

    for ( int32_t i = 0; i < kMaxSparkleInstances; i++ ) {
        if ( memorySize - memoryUsed < kSparkPoolSize ) {
            break;
        }
        memoryUsed += kSparkPoolSize;

        ParticleInstance *pInstance = new ParticleInstance();
        pInstance->pParticleInstanciated = pParticle;
        pInstance->State = eParticleState::PS_Dead;
        pInstance->Matrix = Matrix4x4f_t::Identity();
        pInstance->Color = Eigen::Vector4f::Zero();
        pInstance->Scale = 0.0f;
        pInstance->Density = 0.0f;
        pInstance->Speed = 0.0f;
        pInstance->StopTimer = 0.0f;
        pInstance->Alpha = 1.0f;
        pInstance->pPrimaryEmitter = nullptr;
        pInstance->SparkleVelocity = Eigen::Vector4f::Zero();

        Spark emptySpark{};
        emptySpark.Position = Eigen::Vector4f::Zero();
        emptySpark.PreviousPosition = Eigen::Vector4f::Zero();
        emptySpark.OlderPosition = Eigen::Vector4f::Zero();
        emptySpark.Velocity = Eigen::Vector4f::Zero();
        emptySpark.GroundPlane = Eigen::Vector4f::Zero();
        emptySpark.Life = -1.0f;
        pInstance->Sparks.assign( kNumSparksPerInstance, emptySpark );
        pInstance->NumLiveSparks = 0;
        pInstance->SparkCooldown = 0.0f;
        pInstance->SparkGroundPlane = Eigen::Vector4f::Zero();
        pInstance->bSparkCollide = false;
        pInstance->SparkRestitution = 0.0f;
        pInstance->SparkDrag = 0.0f;

        pParticle->Instances.push_back( pInstance );
    }

    const uint32_t numInstances =
        static_cast<uint32_t>( pParticle->Instances.size() );
    DrawStreams streams;
    streams.Diffuse = 1;
    streams.UVMap = 1;
    streams.bIndex16 = 1;
    pParticle->drawList.initialize( numInstances,
                                    numInstances * kSparkleVerticesPerInstance,
                                    numInstances * kSparkleIndicesPerInstance,
                                    true, &streams, "PrtSparkle", true );
}

void SimulateSparkles( Particle *pParticle, float deltaTime ) {
    // FUN_0080c760
    OTDU_UNIMPLEMENTED_ONCE;      // TODO: REPLAY
    const float timeScale = 1.0f; // TODO: Read time scale for replay!

    if ( !pParticle->drawList.isInitialized() ) {
        return;
    }
    pParticle->drawList.reset();

    uint32_t usedVertices = 0;
    uint32_t usedIndices = 0;
    for ( ParticleInstance *pInstance : pParticle->Instances ) {
        if ( pInstance->State == eParticleState::PS_Dead ) {
            continue;
        }
        SimulateSparkleInstance( pInstance, timeScale * deltaTime );
        BuildSparkleGeometry( pInstance, pParticle->pMaterial,
                              pParticle->drawList, usedVertices, usedIndices );
    }
}

void ActivateSparkleInstance( ParticleInstance *pInstance, bool bReset ) {
    // FUN_0080aa50
    pInstance->SparkCooldown = 0.0f;
    pInstance->NumLiveSparks = 0;
    pInstance->bSparkCollide = true;
    pInstance->SparkRestitution = kDefaultRestitution;
    pInstance->SparkDrag = 0.0f;
    if ( !bReset ) {
        return;
    }
    for ( Spark &spark : pInstance->Sparks ) {
        spark.Life = -1.0f; // FLOAT_00f41664
    }
}
