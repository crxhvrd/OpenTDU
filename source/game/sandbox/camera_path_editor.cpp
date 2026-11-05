#include "shared.h"
#include "camera_path_editor.h"

#include "core/maths_helper.h"
#include "debug/debug_car_picker.h"
#include "game/vehicle/car_state.h"
#include "physics/gs_physics.h"
#include "render/camera/camera_game.h"
#include "render/camera/camera_mouse.h"
#include "render/camera/gs_camera.h"
#include "world/gs_world_streaming.h"

static constexpr float kDefaultKeySpacing = 3.0f; // seconds
static constexpr float kKeyTargetDistance = 100.0f;
static constexpr float kDefaultCarPlacementDistance = 10.0f;
static constexpr float kNoGround = -1.0e30f;

CameraPathEditor::CameraPathEditor()
    : carPlacementDistance( kDefaultCarPlacementDistance ),
      prePreviewPosition( Eigen::Vector3f::Zero() ),
      prePreviewTarget( Eigen::Vector3f::Zero() ),
      previewTime( 0.0f ),
      bPreviewing( false ) {}

CameraPathEditor::~CameraPathEditor() { previewCar.destroy(); }

void CameraPathEditor::reset() {
    stopPreview();
    if ( CinBuilder::LoadKeys( CinBuilder::kLoginAttractKeysPath, keys,
                               car ) ) {
        status = "Loaded " + std::to_string( keys.size() ) + " key(s)";
    }
    respawnPreviewCar();
}

void CameraPathEditor::tick( const float deltaTime ) {
    previewCar.tick();

    if ( !bPreviewing ) {
        return;
    }
    if ( keys.size() < 2 ) {
        stopPreview();
        return;
    }

    previewTime += deltaTime;
    if ( previewTime > keys.back().Time ) {
        stopPreview();
        return;
    }

    Eigen::Vector3f position, target;
    float fov = 0.0f;
    CinBuilder::SamplePath( keys, previewTime, position, target, fov );

    // The world viewer streams around the free camera
    moveCameraTo( position, target );

    const Eigen::Vector3f origin = gpWorldStreaming->getPosition().head<3>();
    gpCamera->setCinematicCameraOverride( position - origin,
                                          Eigen::Vector3f::UnitY(),
                                          target - origin, fov * kDegToRad );
}

void CameraPathEditor::setKeyFromCamera( CinCameraKey &key ) {
    CameraGame *pCamera = gpCamera->getRenderCamera();
    key.Position = pCamera->getWorldPosition();
    Eigen::Vector3f forward = pCamera->getTarget() - key.Position;
    if ( forward.squaredNorm() < 1e-8f ) {
        forward = Eigen::Vector3f::UnitZ();
    }
    key.Target = key.Position + forward.normalized() * kKeyTargetDistance;
    key.FovDegrees = pCamera->getFOV() / kDegToRad;
}

void CameraPathEditor::addKey() {
    CinCameraKey key;
    key.Time = keys.empty() ? 0.0f : keys.back().Time + kDefaultKeySpacing;
    setKeyFromCamera( key );
    keys.push_back( key );
}

void CameraPathEditor::moveCameraTo( const Eigen::Vector3f &position,
                                     const Eigen::Vector3f &target ) {
    CameraMouse *pCamera = gpCamera->getMouseCamera();
    if ( pCamera == nullptr ) {
        return;
    }
    pCamera->setPosition(
        Eigen::Vector4f( position.x(), position.y(), position.z(), 1.0f ) );
    pCamera->setTarget(
        Eigen::Vector4f( target.x(), target.y(), target.z(), 1.0f ) );
    pCamera->updateMatrix( position, Eigen::Vector3f::UnitY(), target );
    pCamera->initialize();
}

void CameraPathEditor::startPreview() {
    if ( keys.size() < 2 ) {
        status = "Preview needs two keys";
        return;
    }
    previewTime = 0.0f;
    bPreviewing = true;

    CameraMouse *pCamera = gpCamera->getMouseCamera();
    if ( pCamera != nullptr ) {
        prePreviewPosition = pCamera->getWorldPosition();
        prePreviewTarget = pCamera->getTarget();
    }
}

void CameraPathEditor::stopPreview() {
    if ( bPreviewing ) {
        gpCamera->clearCinematicCameraOverride();
        moveCameraTo( prePreviewPosition, prePreviewTarget );
    }
    bPreviewing = false;
}

void CameraPathEditor::respawnPreviewCar() { previewCar.spawn( car ); }

void CameraPathEditor::placeCarInView() {
    // On the ground, ahead of the camera, facing it
    CameraGame *pCamera = gpCamera->getRenderCamera();
    const Eigen::Vector3f position = pCamera->getWorldPosition();
    Eigen::Vector3f forward = pCamera->getTarget() - position;
    forward.y() = 0.0f;
    if ( forward.squaredNorm() < 1e-8f ) {
        forward = Eigen::Vector3f::UnitZ();
    }
    forward.normalize();

    Eigen::Vector3f carPosition = position + forward * carPlacementDistance;
    const float groundHeight =
        gpPhysics->getGroundHeight( carPosition.x(), carPosition.z() );
    if ( groundHeight <= kNoGround ) {
        return;
    }
    carPosition.y() = groundHeight;

    car.Position = carPosition;
    car.YawDegrees = atan2f( -forward.x(), -forward.z() ) / kDegToRad;
    respawnPreviewCar();
}

void CameraPathEditor::sortKeys() {
    std::stable_sort( keys.begin(), keys.end(),
                      []( const CinCameraKey &a, const CinCameraKey &b ) {
                          return a.Time < b.Time;
                      } );
}

#ifdef OTDU_USE_IMGUI
void CameraPathEditor::drawDebug() {
    // TODO: Make this editor generic to allow cinematic edition (e.g. for new
    // spots, world level transition, etc.)
    if ( !ImGui::CollapsingHeader( "Login attract cinematic editor" ) ) {
        return;
    }

    if ( ImGui::Button( "Add key (current camera)" ) ) {
        addKey();
    }
    ImGui::SameLine();
    if ( !bPreviewing ) {
        if ( ImGui::Button( "Preview" ) ) {
            startPreview();
        }
    } else {
        if ( ImGui::Button( "Stop preview" ) ) {
            stopPreview();
        }
        ImGui::SameLine();
        ImGui::Text( "%.1f / %.1f s", previewTime,
                     keys.empty() ? 0.0f : keys.back().Time );
    }

    int32_t removedKey = -1;
    bool bTimeEdited = false;
    if ( ImGui::BeginTable( "##cameraPathKeys", 6,
                            ImGuiTableFlags_Borders |
                                ImGuiTableFlags_RowBg ) ) {
        ImGui::TableSetupColumn( "Time" );
        ImGui::TableSetupColumn( "Position" );
        ImGui::TableSetupColumn( "Target" );
        ImGui::TableSetupColumn( "FOV" );
        ImGui::TableSetupColumn( "Cut" );
        ImGui::TableSetupColumn( "" );
        ImGui::TableHeadersRow();

        for ( int32_t i = 0; i < int32_t( keys.size() ); i++ ) {
            CinCameraKey &key = keys[size_t( i )];
            ImGui::PushID( i );
            ImGui::TableNextRow();

            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth( 70.0f );
            if ( ImGui::DragFloat( "##time", &key.Time, 0.05f, 0.0f, 600.0f,
                                   "%.2f" ) ) {
                bTimeEdited = true;
            }
            ImGui::TableNextColumn();
            ImGui::Text( "%.1f %.1f %.1f", key.Position.x(), key.Position.y(),
                         key.Position.z() );
            ImGui::TableNextColumn();
            ImGui::Text( "%.1f %.1f %.1f", key.Target.x(), key.Target.y(),
                         key.Target.z() );
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth( 60.0f );
            ImGui::DragFloat( "##fov", &key.FovDegrees, 0.1f, 5.0f, 120.0f,
                              "%.1f" );
            ImGui::TableNextColumn();
            if ( i > 0 ) {
                ImGui::Checkbox( "##cut", &key.bCut );
            }
            ImGui::TableNextColumn();
            if ( ImGui::SmallButton( "Go" ) ) {
                stopPreview();
                moveCameraTo( key.Position, key.Target );
            }
            ImGui::SameLine();
            if ( ImGui::SmallButton( "Set" ) ) {
                setKeyFromCamera( key );
            }
            ImGui::SameLine();
            if ( ImGui::SmallButton( "X" ) ) {
                removedKey = i;
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if ( removedKey >= 0 ) {
        keys.erase( keys.begin() + removedKey );
    }
    if ( bTimeEdited && !ImGui::IsAnyItemActive() ) {
        sortKeys();
    }

    if ( ImGui::Button( "Save keys" ) ) {
        sortKeys();
        status =
            CinBuilder::SaveKeys( CinBuilder::kLoginAttractKeysPath, keys, car )
                ? std::string( "Saved " ) + CinBuilder::kLoginAttractKeysPath
                : std::string( "Cannot write " ) +
                      CinBuilder::kLoginAttractKeysPath;
    }
    ImGui::SameLine();
    if ( ImGui::Button( "Reload keys" ) ) {
        stopPreview();
        reset();
    }
    ImGui::SameLine();
    if ( ImGui::Button( "Build .cin" ) ) {
        sortKeys();
        std::string error;
        if ( CinBuilder::SaveKeys( CinBuilder::kLoginAttractKeysPath, keys,
                                   car ) &&
             CinBuilder::BuildCameraBank(
                 keys, CinBuilder::kLoginAttractBankPath, error ) ) {
            status =
                std::string( "Built " ) + CinBuilder::kLoginAttractBankPath;
        } else {
            status = "Build failed: " + error;
        }
    }
    ImGui::SameLine();
    if ( ImGui::Button( "Clear" ) ) {
        stopPreview();
        keys.clear();
    }

    ImGui::SeparatorText( "Showcase car" );
    if ( TestDrive::DrawCarList( car.CarID ) ) {
        respawnPreviewCar();
    }

    ImGui::SetNextItemWidth( 100.0f );
    ImGui::DragFloat( "Distance", &carPlacementDistance, 0.1f, 2.0f, 100.0f,
                      "%.1f m" );
    ImGui::SameLine();
    if ( ImGui::Button( "Place in view" ) ) {
        placeCarInView();
    }
    ImGui::SetNextItemWidth( 100.0f );
    ImGui::DragFloat( "Yaw", &car.YawDegrees, 0.5f, -180.0f, 180.0f,
                      "%.1f deg" );
    if ( ImGui::IsItemDeactivatedAfterEdit() ) {
        respawnPreviewCar();
    }
    ImGui::SameLine();
    if ( ImGui::Button( "Remove car" ) ) {
        car = CinShowcaseCar();
        respawnPreviewCar();
    }
    ImGui::Text( "World Position %.1f %.1f %.1f", car.Position.x(),
                 car.Position.y(), car.Position.z() );

    if ( !status.empty() ) {
        ImGui::TextUnformatted( status.c_str() );
    }
}
#endif
