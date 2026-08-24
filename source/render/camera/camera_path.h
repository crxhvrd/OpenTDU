#pragma once

#include "camera_game.h"
#include "core/mutex.h"

class Path;

class CameraPath : public CameraGame {
public:
    CameraPath();
    virtual ~CameraPath();

    void initialize() override; 
    bool tick( float deltaTime, bool param_1, bool param_2 ) override;
    void destroy() override; 
    void setStaticView( const Eigen::Vector4f &position,
                        const Eigen::Vector4f &target );
    void setPath( Path *pNewPath,
                  bool bInvert,
                  float newDuration,
                  const Matrix4x4f_t *pOffsetMatrix,
                  bool bStartAtEnd );

private:
    static constexpr float kDefaultReferenceAspect = 1.7777778f;
    static constexpr float kDefaultFOV = 0.69813174f;

private:
    TestDriveMutex mutex; 

    Path *pPath;       
    float time;         
    float duration;     
    bool bInverted;    
    bool bPaused;       
    bool bHasPath;      
    bool bT2Quaternion;

    Matrix4x4f_t offset;     
    bool bOffset;          
    Matrix4x4f_t coordChange;
};
