#pragma once

#include <memory>
#include <string>
#include <vector>

#include "filesystem/bank.h"
#include "flash_list_cin_viewer.h"
#include "game/gm_world.h"
#include "game/scene_interior.h"
#include "game/scenaric/scenaric_bank.h"

class EntityCinematic;

// Cinematic viewer game mode - browse and play spot / interior cinematics.
class GMCinViewer : public GameModeWorld {
public:
    GMCinViewer();
    ~GMCinViewer();

    void initialize() override;
    void reset() override;
    void tick( float deltaTime, float totalTime ) override;
    void pause() override;
    void destroy() override;
    bool onMessage( FlashMessage *pMessage ) override;
    bool executeAction( int32_t param_2,
                        void *param_3,
                        void *pData = nullptr ) override;
    bool usesFrontEndDirections() const override { return true; }

private:
    void scanContentBanks( const char *pSubDir );
    void positionAtSpot( const char *pBankName );
    void loadBankCinematics( const char *pBankFolder, const char *pBankName );
    void playCine( const char *pCineName );
    void releaseCurrentCine();
    void stopCine();
    std::string resolveCineResource( ScenaricBank &cineBank,
                                     const char *pCineName );
    void togglePlayPause();      // "play"
    void stepFrame( float dir ); // "frwd" (+1) / "rewd" (-1)
    void restartCine();          // "strt"
    struct SpotMap *findSpotForBank( const char *pBankName,
                                     int32_t *pOutIndex = nullptr );
    bool isSpotFrameReady();
    void enterInteriorPreview( const char *pBankName );
    void leaveInteriorPreview();

private:
    std::vector<std::string> contentBanks;
    FlashListCinViewer flashListBank;
    std::vector<std::string> contentCine;
    FlashListCinViewer flashListCine;
    struct SpotMap *pCurrentSpot = nullptr;
    int32_t spotSectorWaitTicks = 0;
    std::string selectedBankName;
    std::string selectedCineName;
    bool bPlayRequested = false;
    bool bPlayPending = false;
    EntityCinematic *pCurrentCine = nullptr;
    ScenaricBank playbackBank;
    bool bPlaybackBankActive = false;
    float playbackSpeed = 1.0f;
    Matrix4x4f_t freeCineFrame;
    Bank interiorBank;
    std::unique_ptr<SceneInterior> pInteriorScene;
};
