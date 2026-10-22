#include "shared.h"
#include "debug_object_picker.h"

#ifdef OTDU_USE_IMGUI
#include "imgui.h"

#include <algorithm>
#include <filesystem>
#include <unordered_set>
#include <vector>

#include "core/mutex.h"
#include "render/3dd.h"
#include "render/3dg.h"
#include "render/camera/camera_base.h"
#include "render/geometry_buffer.h"
#include "render/gs_render.h"
#include "render/instance.h"
#include "render/render_scene.h"
#include "world/gs_world.h"
#include "world/gs_world_streaming.h"
#include "world/impostor.h"
#include "world/sector_manager.h"
#include "world/streaming_manager.h"

namespace {
    constexpr size_t kMaxNumCandidates = 32768;
    constexpr size_t kMaxNumPickResults = 12;
    constexpr const char *kExportFolder = "PickerExports";

    struct PickerCandidate {
        Instance *pInstance;
    };

    struct PickResult {
        Instance *pInstance;
        RenderObject *pObject;
        uint64_t InstanceHashcode;
        uint64_t GeometryHashcode;
        Eigen::Vector3f AbsolutePosition;
        float Radius;
        float SphereDistance;
        float TriangleDistance; // < 0 when the ray misses the triangles
    };

    TestDriveMutex gCandidatesMutex;
    // Objects submitted during the latest frame (see BeginObjectPickerFrame).
    std::vector<PickerCandidate> gCandidates;
    uint32_t gFrame = 0;
    uint32_t gRecordedFrame = 0;
    std::vector<PickResult> gPickResults;
    bool gbPickerEnabled = false;
    std::string gLastExport;

    // Model hashcodes are packed ASCII names; names longer than 8 characters
    // are folded into the first bytes, which then show as '?'.
    std::string HashcodeToName( uint64_t hashcode ) {
        std::string name;
        for ( int32_t i = 0; i < 8; i++ ) {
            const char c =
                static_cast<char>( ( hashcode >> ( 8 * i ) ) & 0xff );
            if ( c == '\0' ) {
                break;
            }
            name += ( c >= 32 && c < 127 ) ? c : '?';
        }
        return name;
    }

    std::string HashcodeToHex( uint64_t hashcode ) {
        char hex[24];
        snprintf( hex, sizeof( hex ), "0x%016llX",
                  static_cast<unsigned long long>( hashcode ) );
        return hex;
    }

    RenderObject *GetLOD0Object( Instance *pInstance, uint64_t *pOutHashcode ) {
        if ( pInstance->getNumLODs() == 0 ||
             pInstance->getLOD( 0 ).pHashcode == nullptr ) {
            return nullptr;
        }
        Render3DDHashEntry *pEntry = pInstance->getLOD( 0 ).pHashcode;
        uint32_t *pObject = (uint32_t *)(RenderFile::Section *)pEntry->pEntry;
        if ( pObject == nullptr || *pObject == kHeightmapMagic ) {
            return nullptr;
        }
        *pOutHashcode = pEntry->Hashcode;
        return (RenderObject *)( pObject + 4 );
    }

    // Walks the LOD0 triangles in object space (original geometry data, kept
    // after the GPU upload).
    template <typename Callback>
    void ForEachTriangle( RenderObject *pObject, Callback callback ) {
        RenderFile::Section *pSection = pObject->PrimitiveSection;
        for ( uint32_t p = 0; pSection != nullptr && p < pObject->NumPrimitives;
              p++ ) {
            Primitive *pPrimitive = (Primitive *)( (uint8_t *)pSection + 0x10 );
            pSection =
                (RenderFile::Section *)( (uint8_t *)pSection + pSection->Size );

            const uint8_t format = pPrimitive->VertexStreams.Position.Format;
            const uint32_t stride = format == 0 ? 12 : ( format == 7 ? 16 : 0 );
            int8_t *pVertices = (int8_t *)pPrimitive->pVertexBuffer;
            int8_t *pIndices = (int8_t *)pPrimitive->pIndexBuffer;
            if ( stride == 0 || pVertices == nullptr || pIndices == nullptr ||
                 ( pPrimitive->Type != PT_Triangle &&
                   pPrimitive->Type != PT_TriangleStrip ) ) {
                continue;
            }
            const float *pPositions =
                (const float *)( pVertices + kGeometryBufferDataOffset );
            const int8_t *pIndexData = pIndices + kGeometryBufferDataOffset;
            auto getIndex = [&]( uint32_t i ) -> uint32_t {
                return pPrimitive->IndexType == 1
                           ? ( (const uint16_t *)pIndexData )[i]
                           : ( (const uint32_t *)pIndexData )[i];
            };
            auto getVertex = [&]( uint32_t i ) -> Eigen::Vector3f {
                const float *pV =
                    (const float *)( (const int8_t *)pPositions + i * stride );
                return Eigen::Vector3f( pV[0], pV[1], pV[2] );
            };

            const bool bStrip = pPrimitive->Type == PT_TriangleStrip;
            const uint32_t numTriangles =
                bStrip ? ( pPrimitive->NumIndex >= 3 ? pPrimitive->NumIndex - 2
                                                     : 0 )
                       : pPrimitive->NumIndex / 3;
            for ( uint32_t t = 0; t < numTriangles; t++ ) {
                uint32_t i0 = bStrip ? getIndex( t ) : getIndex( t * 3 );
                uint32_t i1 =
                    bStrip ? getIndex( t + 1 ) : getIndex( t * 3 + 1 );
                uint32_t i2 =
                    bStrip ? getIndex( t + 2 ) : getIndex( t * 3 + 2 );
                if ( i0 == i1 || i1 == i2 || i0 == i2 ||
                     std::max( { i0, i1, i2 } ) >= pPrimitive->NumVertex ) {
                    continue;
                }
                if ( bStrip && ( t & 1 ) != 0 ) {
                    std::swap( i1, i2 );
                }
                callback( p, i0, i1, i2, getVertex( i0 ), getVertex( i1 ),
                          getVertex( i2 ) );
            }
        }
    }

    bool IntersectTriangle( const Eigen::Vector3f &origin,
                            const Eigen::Vector3f &direction,
                            const Eigen::Vector3f &v0,
                            const Eigen::Vector3f &v1,
                            const Eigen::Vector3f &v2,
                            float &outT ) {
        const Eigen::Vector3f edge1 = v1 - v0;
        const Eigen::Vector3f edge2 = v2 - v0;
        const Eigen::Vector3f p = direction.cross( edge2 );
        const float det = edge1.dot( p );
        if ( std::abs( det ) < 1e-9f ) {
            return false;
        }
        const float invDet = 1.0f / det;
        const Eigen::Vector3f s = origin - v0;
        const float u = s.dot( p ) * invDet;
        if ( u < 0.0f || u > 1.0f ) {
            return false;
        }
        const Eigen::Vector3f q = s.cross( edge1 );
        const float v = direction.dot( q ) * invDet;
        if ( v < 0.0f || u + v > 1.0f ) {
            return false;
        }
        outT = edge2.dot( q ) * invDet;
        return outT > 0.0f;
    }

    void PickRay( const Eigen::Vector3f &origin,
                  const Eigen::Vector3f &direction ) {
        std::vector<PickerCandidate> candidates;
        {
            ScopedMutexLock lock( &gCandidatesMutex );
            candidates = gCandidates;
        }

        const Eigen::Vector4f streamOrigin =
            gpWorldStreaming->isEnabled() ? gpWorldStreaming->getPosition()
                                          : Eigen::Vector4f::Zero();

        // Mass instances (.PMI) are drawn by the impostor manager, not through
        // RenderContext::submitObject: add every loaded one.
        if ( gpWorld != nullptr && gpWorld->getStreamingManager() != nullptr ) {
            for ( Sector &sector :
                  gpWorld->getStreamingManager()->sectorManager.sectors ) {
                if ( sector.state != 1 ) {
                    continue;
                }
                for ( ImpostorGrid &grid : sector.geometry.impostorGrids ) {
                    for ( MassInstanceImpostor &instance : grid.instances ) {
                        if ( instance.pInstance != nullptr ) {
                            candidates.push_back( { instance.pInstance } );
                        }
                    }
                }
            }
        }

        std::unordered_set<Instance *> seen;
        gPickResults.clear();
        for ( const PickerCandidate &candidate : candidates ) {
            Instance *pInstance = candidate.pInstance;
            if ( !seen.insert( pInstance ).second ) {
                continue;
            }

            uint64_t geometryHashcode = 0;
            RenderObject *pObject =
                GetLOD0Object( pInstance, &geometryHashcode );
            if ( pObject == nullptr ) {
                continue;
            }

            const Matrix4x4f_t &model = pInstance->getModelMatrix();
            const Eigen::Vector4f sphere = pObject->BoundingSphere;
            const Eigen::Vector4f center =
                Eigen::Vector4f( sphere.x(), sphere.y(), sphere.z(), 1.0f )
                    .transpose() *
                model;
            const float radius = sphere.w() * pInstance->getBoundingScale();

            // Ray / bounding sphere.
            const Eigen::Vector3f toCenter = center.head<3>() - origin;
            const float along = toCenter.dot( direction );
            const float distanceSquared =
                toCenter.squaredNorm() - along * along;
            if ( distanceSquared > radius * radius || along + radius < 0.0f ) {
                continue;
            }

            // Ray / LOD0 triangles, in object space.
            const Matrix4x4f_t inverse = model.inverse();
            const Eigen::Vector3f objectOrigin =
                ( Eigen::Vector4f( origin.x(), origin.y(), origin.z(), 1.0f )
                      .transpose() *
                  inverse )
                    .head<3>()
                    .transpose();
            const Eigen::Vector3f objectDirection =
                ( Eigen::Vector4f( direction.x(), direction.y(), direction.z(),
                                   0.0f )
                      .transpose() *
                  inverse )
                    .head<3>()
                    .transpose();
            float nearestT = -1.0f;
            ForEachTriangle( pObject, [&]( uint32_t, uint32_t, uint32_t,
                                           uint32_t, const Eigen::Vector3f &v0,
                                           const Eigen::Vector3f &v1,
                                           const Eigen::Vector3f &v2 ) {
                float t = 0.0f;
                if ( IntersectTriangle( objectOrigin, objectDirection, v0, v1,
                                        v2, t ) &&
                     ( nearestT < 0.0f || t < nearestT ) ) {
                    nearestT = t;
                }
            } );

            PickResult result;
            result.pInstance = pInstance;
            result.pObject = pObject;
            result.InstanceHashcode = pInstance->getHashcode();
            result.GeometryHashcode = geometryHashcode;
            result.AbsolutePosition =
                Eigen::Vector3f( model( 3, 0 ) + streamOrigin.x(),
                                 model( 3, 1 ) + streamOrigin.y(),
                                 model( 3, 2 ) + streamOrigin.z() );
            result.Radius = radius;
            result.SphereDistance = std::max( 0.0f, along - radius );
            result.TriangleDistance = nearestT;
            gPickResults.push_back( result );
        }

        // Triangle hits first (nearest first), then sphere only hits.
        std::sort( gPickResults.begin(), gPickResults.end(),
                   []( const PickResult &a, const PickResult &b ) {
                       const bool bHitA = a.TriangleDistance >= 0.0f;
                       const bool bHitB = b.TriangleDistance >= 0.0f;
                       if ( bHitA != bHitB ) {
                           return bHitA;
                       }
                       return bHitA ? a.TriangleDistance < b.TriangleDistance
                                    : a.SphereDistance < b.SphereDistance;
                   } );
        if ( gPickResults.size() > kMaxNumPickResults ) {
            gPickResults.resize( kMaxNumPickResults );
        }
    }

    void Pick() {
        const Matrix4x4f_t &camToWorld =
            gpRender->getRenderPass<eRenderPass::RP_Near>()
                .pCamera->getCamToWorld();
        PickRay( camToWorld.row( 3 ).head<3>().transpose(),
                 camToWorld.row( 2 ).head<3>().transpose().normalized() );
    }

    void ExportObj( const PickResult &result ) {
        std::filesystem::create_directories( kExportFolder );
        std::string name = HashcodeToName( result.InstanceHashcode );
        if ( name.find( '?' ) != std::string::npos ) {
            name = HashcodeToHex( result.InstanceHashcode );
        }
        const std::string path =
            std::string( kExportFolder ) + "/" + name + ".obj";
        FILE *pFile = fopen( path.c_str(), "w" );
        if ( pFile == nullptr ) {
            gLastExport = "Failed to write " + path;
            return;
        }

        fprintf( pFile, "# OpenTDU object picker: %s (%s), geometry %s, LOD0\n",
                 HashcodeToName( result.InstanceHashcode ).c_str(),
                 HashcodeToHex( result.InstanceHashcode ).c_str(),
                 HashcodeToHex( result.GeometryHashcode ).c_str() );
        fprintf( pFile, "# Object space (y up), placed at %.3f %.3f %.3f\n",
                 result.AbsolutePosition.x(), result.AbsolutePosition.y(),
                 result.AbsolutePosition.z() );
        fprintf( pFile, "o %s\n", name.c_str() );

        // One OBJ group per primitive, vertices emitted per triangle corner.
        uint32_t numVertices = 0;
        uint32_t currentPrimitive = ~0u;
        ForEachTriangle( result.pObject, [&]( uint32_t primitive, uint32_t,
                                              uint32_t, uint32_t,
                                              const Eigen::Vector3f &v0,
                                              const Eigen::Vector3f &v1,
                                              const Eigen::Vector3f &v2 ) {
            if ( primitive != currentPrimitive ) {
                currentPrimitive = primitive;
                fprintf( pFile, "g prim_%u\n", primitive );
            }
            for ( const Eigen::Vector3f *pV : { &v0, &v1, &v2 } ) {
                fprintf( pFile, "v %f %f %f\n", pV->x(), pV->y(), pV->z() );
            }
            fprintf( pFile, "f %u %u %u\n", numVertices + 1, numVertices + 2,
                     numVertices + 3 );
            numVertices += 3;
        } );
        fclose( pFile );
        gLastExport = "Exported " + std::to_string( numVertices / 3 ) +
                      " triangles to " +
                      std::filesystem::absolute( path ).string();
        OTDU_LOG_ALWAYS( "%s\n", gLastExport.c_str() );
    }
} // namespace

void TestDrive::RecordObjectPickerCandidate( RenderScene *pScene,
                                             Instance *pInstance ) {
    if ( !gbPickerEnabled || pInstance == nullptr ||
         pScene != gpRender->getRenderPass<eRenderPass::RP_Near>().pScene ) {
        return;
    }
    ScopedMutexLock lock( &gCandidatesMutex );
    if ( gRecordedFrame != gFrame ) {
        gRecordedFrame = gFrame;
        gCandidates.clear();
    }
    if ( gCandidates.size() < kMaxNumCandidates ) {
        gCandidates.push_back( { pInstance } );
    }
}

void TestDrive::BeginObjectPickerFrame() { gFrame++; }

void TestDrive::DrawObjectPickerPanel() {
    if ( !ImGui::CollapsingHeader( "Object Picker" ) ) {
        return;
    }

    if ( ImGui::Checkbox( "Enable (records the near pass objects)",
                          &gbPickerEnabled ) &&
         !gbPickerEnabled ) {
        ScopedMutexLock lock( &gCandidatesMutex );
        gCandidates.clear();
        gPickResults.clear();
    }
    if ( !gbPickerEnabled ) {
        return;
    }

    // Aim with the screen center (the crosshair), then pick.
    ImDrawList *pForeground = ImGui::GetForegroundDrawList();
    const ImVec2 center( ImGui::GetIO().DisplaySize.x * 0.5f,
                         ImGui::GetIO().DisplaySize.y * 0.5f );
    pForeground->AddLine( ImVec2( center.x - 10.0f, center.y ),
                          ImVec2( center.x + 10.0f, center.y ),
                          IM_COL32( 255, 64, 64, 255 ), 1.5f );
    pForeground->AddLine( ImVec2( center.x, center.y - 10.0f ),
                          ImVec2( center.x, center.y + 10.0f ),
                          IM_COL32( 255, 64, 64, 255 ), 1.5f );

    if ( ImGui::Button( "Pick (screen center)" ) ) {
        Pick();
    }
    ImGui::SameLine();
    {
        ScopedMutexLock lock( &gCandidatesMutex );
        ImGui::Text( "%zu objects in the last frame", gCandidates.size() );
    }
    ImGui::TextDisabled( "Export right after picking (streaming can unload the "
                         "mesh). OTDU_LIGHTMODEL takes the name or the 0x "
                         "hashcode." );

    if ( !gPickResults.empty() &&
         ImGui::BeginTable( "PickResults", 6,
                            ImGuiTableFlags_Borders |
                                ImGuiTableFlags_RowBg ) ) {
        ImGui::TableSetupColumn( "Instance" );
        ImGui::TableSetupColumn( "Geometry" );
        ImGui::TableSetupColumn( "Hit" );
        ImGui::TableSetupColumn( "Position (world)" );
        ImGui::TableSetupColumn( "Radius" );
        ImGui::TableSetupColumn( "" );
        ImGui::TableHeadersRow();
        for ( size_t i = 0; i < gPickResults.size(); i++ ) {
            const PickResult &result = gPickResults[i];
            ImGui::PushID( static_cast<int>( i ) );
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            const std::string hex = HashcodeToHex( result.InstanceHashcode );
            ImGui::TextUnformatted(
                HashcodeToName( result.InstanceHashcode ).c_str() );
            if ( ImGui::IsItemHovered() ) {
                ImGui::SetTooltip( "%s", hex.c_str() );
            }
            ImGui::SameLine();
            if ( ImGui::SmallButton( "Copy" ) ) {
                ImGui::SetClipboardText( hex.c_str() );
            }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(
                HashcodeToName( result.GeometryHashcode ).c_str() );
            ImGui::TableNextColumn();
            if ( result.TriangleDistance >= 0.0f ) {
                ImGui::Text( "%.1f m", result.TriangleDistance );
            } else {
                ImGui::TextDisabled( "sphere %.1f m", result.SphereDistance );
            }
            ImGui::TableNextColumn();
            ImGui::Text( "%.2f %.2f %.2f", result.AbsolutePosition.x(),
                         result.AbsolutePosition.y(),
                         result.AbsolutePosition.z() );
            ImGui::TableNextColumn();
            ImGui::Text( "%.1f", result.Radius );
            ImGui::TableNextColumn();
            if ( ImGui::SmallButton( "Export OBJ" ) ) {
                ExportObj( result );
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if ( !gLastExport.empty() ) {
        ImGui::TextWrapped( "%s", gLastExport.c_str() );
    }
}
#endif
