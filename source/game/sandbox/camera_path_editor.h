#pragma once

#include "game/login/cin_builder.h"

// Keyframed camera path editor for world cutscenes.
// Only used for GMLogin attract cutscene for now
class CameraPathEditor {
public:
    CameraPathEditor();
    ~CameraPathEditor();

    void reset();
    void tick( const float deltaTime );
#ifdef OTDU_USE_IMGUI
    void drawDebug();
#endif

private:
    void addKey();
    void setKeyFromCamera( CinCameraKey &key );
    void moveCameraTo( const Eigen::Vector3f &position,
                       const Eigen::Vector3f &target );
    void startPreview();
    void stopPreview();
    void sortKeys();
    void placeCarInView();
    void respawnPreviewCar();

private:
    std::vector<CinCameraKey> keys;
    CinShowcaseCar car;
    ShowcaseCarInstance previewCar;
    float carPlacementDistance;
    std::string status;
    // Free camera before the preview starts
    Eigen::Vector3f prePreviewPosition;
    Eigen::Vector3f prePreviewTarget;
    float previewTime;
    bool bPreviewing;
};
