#pragma once

#include <string>
#include <vector>

#include "base_entity.h"
#include "game/cinematic/cin_res_collection.h"
#include "game/cinematic/cinematic_music_emitter.h"
#include "game/gm_base.h"

class CinematicPlayer;

enum eCinematicEvent : int32_t {
    CE_Requested = 0,
    CE_Stopping = 2,
    CE_Ended = 3,
    CE_Started = 5,
    CE_StartEndFade = 6,
    CE_Finished = 7,
    CE_Begun = 10,
};

class EntityCinematic : public Entity {
public:
    const char *GetName() override { return "SCCinematic"; /* FUN_00425b30 */ }

public:
    inline void setLoop( bool bEnabled ) { bLoop = bEnabled; }
    inline void setStartTime( float time ) { StartTime = time; }
    inline void setBank( ScenaricBank *pBank ) { pSCBank = pBank; }
    inline void setCutsceneName( const char *pName ) { cutsceneName = pName; }
    inline bool hasFocus() const {
        return ( cinematicPlaybackFlags & 0x10u ) == 0;
    }
    inline uint32_t getFlags() const { return cinematicFlags; }
    inline bool hasEnded() const { return ( cinematicFlags & 0x10u ) != 0; }
    inline bool isSkippable() const {
        return ( cinematicPlaybackFlags & 0x4u ) != 0;
    }
    inline void markSkipped() { bSkipped = true; }
    inline void clearEnded() { cinematicFlags &= ~0x10u; }
    inline bool hasMusicEmitter() const { return pMusicEmitter != nullptr; }
    inline bool wasSkipped() const { return bSkipped; }
    inline bool stopsWhenSkipped() const { return bStopWhenSkipped; }
    inline void setStopWhenSkipped( bool bEnabled ) {
        bStopWhenSkipped = bEnabled;
    }
    inline float getFadeTime() const { return FadeTime; }
    inline void setFadeTime( float milliseconds ) { FadeTime = milliseconds; }
    inline void setGameModeInterface( CinematicPlayer *pInterface ) {
        pGMInterface = pInterface;
    }
    inline CinematicPlayer *getGameModeInterface() const {
        return pGMInterface;
    }
    inline bool isLooping() const { return bLoop; }
    inline bool isPlaying() const {
        return ( cinematicFlags & 0x4u ) != 0 && ( cinematicFlags & 0x2u ) == 0;
    }
    void setPaused( bool bEnabled );
    inline bool isPaused() const { return bPaused; }
    inline void setPlaybackSpeed( float speed ) { playbackSpeed = speed; }
    inline float getPlaybackSpeed() const { return playbackSpeed; }

    inline float getTime() const { return Time; }
    inline float getDuration() const { return CineTotalTime; }
    const CinResCollection::CameraView &getCameraView() const {
        return cinematicPlayer.getCameraView();
    }

public:
    EntityCinematic();
    ~EntityCinematic();

    void initialize() override;
    void loadResources( ScenaricBank *param_2 ) override;
    void initializeInstance( void *param_2, void *param_3 ) override;
    void unloadResources() override;
    void tick() override;
    void deserialize( BinaryReader *param_2 ) override;
    void reset() override;
    void instanciate( MngActor *pMngActor, Entity *param_1 ) override;

    void start( bool bBeginNow );
    void stop( bool bEndNow );

    bool launchSpotCine( struct SpotMap *pSpot, float startTime );
    void installMusicBanks();
    bool createTDUInterface();
    void seek( float time );
    void setPlaybackAnchor( const Eigen::Vector3f &pos );

private:
    bool begin();
    bool prepareResources();
    void applyForegroundCine();
    void restoreForegroundCine();
    void end();
    void releaseMusicEmitter();
    void publishCameraView();
    bool calculateSpotMatrix( struct SpotMap *pSpot, Matrix4x4f_t &outMatrix );

private:
    uint32_t cinematicFlags;
    uint32_t cinematicPlaybackFlags;
    uint32_t cinematicPlaybackFlags2;

    float StartTime;
    float Time;
    float CineTotalTime;
    float FadeTime;

    const void *pCameraRef;        
    const void *pZoneRef;          
    float cinematicZNear;          
    float cinematicZFar;           
    const void *pMusic1Ref;        // (bank version < 0x13)
    const void *pSoundEmitter1Ref; // (bank version >= 0x13)
    const void *pMusic2Ref;        // (bank version < 0x13)
    const void *pSoundEmitter2Ref; // (bank version >= 0x13)
    const void *pMicroRef;         
    const void *pStreamCenterRef; 
    uint32_t numRefs;              
    const void *pCinRef;           // the .cin blob
    const void *pSceneRefs;        // the (numRefs - 1) .sce blobs

    ScenaricBank *pSCBank;
    std::unique_ptr<CinematicMusicEmitter> pMusicEmitter;
    CinematicPlayer *pGMInterface;
    CinResCollection cinematicPlayer;
    struct SpotMap *pSpotWorld;
    std::string cutsceneName;
    const void *pCinData;
    uint32_t cinDataSize;
    std::vector<uint8_t> cinematicBuffer;
    bool bPaused;
    float playbackSpeed;

    bool bLoop;
    bool bHideFrontEnd;
    bool bOnFinalFrame;
    bool bSkipped;
    bool bStopWhenSkipped;
    bool bDontSwitchCamera;
    bool bInitialized;
    bool bForceStop;
    bool bResourcesPrepared;      
    bool bForegroundCineApplied;  
    bool bApplyCineMix;         
    bool bRestoreCineMix;        
    uint32_t savedMutedVehicleCategories; 
    bool bStopGuard;
    bool bStackActive;
    bool bCameraHandedOver; 

    eGameMode activeGameMode;
};
