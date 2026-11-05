#pragma once

#include "filesystem/bank.h"
#include "game/cinematic/cin_res_collection.h"
#include "game/login/cin_builder.h"
#include "render/gs_render_helper.h"
#include "render/mng_env_map.h"
#include "render/mng_shadow.h"
#include "world/mng_background.h"
#include "world/mng_clouds_plane.h"
#include "world/mng_impostor.h"
#include "world/mng_ocean.h"
#include "world/mng_sun_flare.h"
#include "world/mng_tall_grass.h"
#include "world/mng_world.h"
#include "world/weather/time_of_day.h"

class FlashPlayer;
class Manager;

// OTDU_ADDITION: the camera cinematic(s) of a bank built by CinBuilder, played
// in a loop by the game's cinematic player (a camera alone: no actors,
// messages or fades). The camera view it evaluates drives the camera override;
// the bank is read raw.
class LoginAttractCinematic {
public:
    LoginAttractCinematic();
    ~LoginAttractCinematic();

    // Plays the cinematics of the bank at 'pBankPath' whose file name ends
    // with 'pCinematicSuffix', relative to the bank's anchor
    bool load( const char *pBankPath, const char *pCinematicSuffix );
    void release();

    inline bool isLoaded() const { return !cinEntries.empty(); }

    // False until the camera published a view
    bool tick( const float deltaTime, CinResCollection::CameraView &outView );

private:
    bool start( const int32_t index );
    Matrix4x4f_t computeAnchorMatrix() const;

private:
    void *pBankData;
    BankInstance bank;
    std::vector<int32_t> cinEntries;
    Eigen::Vector3f worldAnchor;

    std::unique_ptr<CinResCollection> pPlayer;
    std::vector<uint8_t> playerBuffer;
    int32_t currentCin;
    float time;
    CinResCollection::CameraView lastView;
};

// OTDU_ADDITION: world view behind the login menu. The generated LOG_IN movie
// (AssetExtractor::BuildGeneratedMovies) wraps its backdrop in '/otdu_bg',
// hidden once the area around the camera is streamed.
class LoginWorldScene final : public RenderConfigurationChangeListener {
public:
    static constexpr int32_t kNumManagers = 10;

public:
    // Picks the generated LOG_IN movie, or the retail one when the scene is
    // disabled ('-stocklogin') or the movie was not generated. Returns true
    // when the scene should run.
    static bool SelectLoginMovie();

public:
    LoginWorldScene();
    ~LoginWorldScene();

    std::array<Manager *, kNumManagers> getManagers();

    void reset( FlashPlayer *pLoginPlayer );
    void tick();

    // Render targets were reallocated (resize, HDR toggle)
    bool onConfigurationChanged( const int32_t eventCode ) override;

private:
    void updateCamera();
    Eigen::Vector4f getStreamPosition() const;
    void setAttractCameraPosition( const CinResCollection::CameraView &view );
    void tickShowcaseCar();

private:
    MngShadow mngShadow;
    // Sector lookups of the shadow and env map managers (streaming stays
    // driven by the scene)
    MngWorld mngWorld;
    MngCloudsPlane mngCloudsPlane;
    MngBackground mngBackground;
    MngOcean mngOcean;
    MngSunFlare mngSunFlare;
    MngEnvMap mngEnvMap;
    MngImpostor mngImpostor;
    MngImpostorSimple mngImpostorSimple;
    MngTallGrass mngTallGrass;

    LoginAttractCinematic attractCinematic;
    // Parked showcase car of the camera path file (CinShowcaseCar)
    ShowcaseCarInstance showcaseCar;
    VehicleBase *pEnvMappedCar;

    FlashPlayer *pLoginPlayer;
    Eigen::Vector4f attractCameraPosition; // world space
    TimeOfDay savedTime;
    float savedTimeScale;
    bool bStreamed;
    bool bActive;
};
