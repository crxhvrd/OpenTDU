#pragma once

#include "game/gm_base.h"
#include "game/gs_base.h"

// Codes GSFlash::getInputMask() hands to a movie: one per frame, as the ASCII
// value the movie's ActionScript compares against.
enum eFlashKey : uint32_t {
    FK_Left = 0x01,        // left arrow
    FK_Right = 0x02,       // right arrow
    FK_Pause = 0x03,       // home key (mapped to pause action)
    FK_Back = 0x08,        // backspace
    FK_Validate = 0x0d,    // enter
    FK_Up = 0x0e,          // up arrow
    FK_Down = 0x0f,        // down arrow
    FK_WheelUp = 0x2b,     // '+'
    FK_WheelDown = 0x2d,   // '-'
    FK_SortRight = 0x31,   // '1'
    FK_InGameDown = 0x32,  // '2'
    FK_SortLeft = 0x33,    // '3'
    FK_InGameLeft = 0x34,  // '4'
    FK_Option4 = 0x35,     // '5'
    FK_InGameRight = 0x36, // '6'
    FK_SortInsert = 0x37,  // '7'
    FK_InGameUp = 0x38,    // '8'
    FK_Option3 = 0x39,     // '9'
    FK_Option1 = 0x78,     // 'x'
    FK_Option2 = 0x79,     // 'y'
};

#include "bank/bank_common_flash.h"
#include "bank/flash_bank_resources.h"
#include "core/mutex.h"
#include "flash/audio/flash_sounds.h"
#include "flash/entities/flash_entity.h"
#include "flash/implementation/flash_player.h"
#include "flash_mouse.h"
#include "modal_message_box.h"
#include "movie_player.h"
#include "render/2dm.h"
#include "render/file_collection_2d.h"
#include "render/file_collection_3d.h"

struct InputChannel;

class FlashStreamListener {
public:
    virtual void onResourceStreamed() {}
};

class UnknownEntity : public FlashEntity {
public:
    bool onMessage( FlashMessage *pMessage,
                    class FlashPlayer *pPlayer ) override;

private:
    bool bSavedMouseFlag = false;
};

class GSFlash : public GameSystem, public RenderConfigurationChangeListener {
public:
    const char *getName() const override { return "Service : Flash"; }
    inline void lockMutex() { mutex.lock(); }
    inline void unlockMutex() { mutex.unlock(); }
    inline ScopedMutexLock scopedLock() { return ScopedMutexLock( &mutex ); }
    inline FlashSounds *getFlashSound() { return &audio; }
    inline Material *getNullMaterial() { return pNullMaterial; }
    inline Material *getMaskedMaterial() { return pMaskMaterial; }
    inline FlashMouse *getMouse() { return &mouse; }
    inline char *getContactTypeRW() { return pContactType; }
    inline FileCollection2D &getCollection2D() { return collection2D; }
    inline void setUseStick( const bool bEnabled ) { bUseStick = bEnabled; }

public:
    GSFlash();
    ~GSFlash();

    bool initialize( TestDriveGameInstance * ) override;
    void tick( float totalTime, float deltaTime ) override;
    void terminate() override;
    void draw( float deltaTime ) override;
    void pause() override;

    void setCommonPath( const char *pNewPath );
    void setRootPath( const char *pNewPath );

    void setMaxFlashMovies( const int32_t maxNumMovies );

    void initGameMode( const eGameMode gameMode,
                       FlashEntity *pEntity,
                       const bool param_4 );

    FlashPlayer *getFlashPlayer( const char *pName );
    bool getFlashMovieIndex( uint32_t *pOutIsValidGameMode,
                             uint32_t *pOutMovieIndex,
                             const char *param_4 );

    int32_t getNumMovies( const int32_t param_1 );
    MoviePlayer *getMoviePlayer( int32_t levelIndex, int32_t movieIndex );
    void setLevelMoviesFlag( const int32_t levelIndex,
                             const int32_t flagType,
                             const int32_t bit,
                             const bool bEnable );
    static int32_t GetUnicodeFontIndex( const int32_t locale );
    FlashPlayer *getLoadedFlashPlayer( const int32_t levelIndex,
                                       const int32_t movieIndex );
    int32_t getUnicodeFontLocale() const;
    void setUnicodeFontLocale( const int32_t locale );
    void releaseUnicodeFont();
    int32_t
    forEachLoadedPlayer( const std::function<void( FlashPlayer * )> &callback );

    void addMovieForGameMode( const int32_t param_1, FlashEntity *pEntity );
    void addMovie( FlashMovie &movie, FlashEntity *pEntity );

    void flashCallback( const char *param_1 );
    void grantMovieInput( const char *pMovieName );
    void setMovieDrawnOnTop( const char *pMovieName, const bool bOnTop );

    void enablePlayer( const char *param_2,
                       ModalMessageBox::eDisplayState param_3,
                       bool param_4,
                       int8_t param_5 );

    void FUN_0099a740();
    void FUN_0099be10();
    void FUN_0099bd60();

    void pushMovie( FlashMovie &movie );
    bool setMovieRootPath( const char *pMovieName, const char *pRootPath );
    void setLocalizedStrings();

    void bindPlayerEntity( FlashEntity *param_1, const char *param_2 );

#ifdef OTDU_USE_IMGUI
    void drawDebug( float deltaTime ) override;

private:
    void drawFrontEndSoundDebug();

public:
#endif

    bool onConfigurationChanged( const int32_t eventCode ) override;

    bool FUN_0099b1e0( const int32_t param_1, const int32_t param_2 );
    bool FUN_0099b240( const char *param_2, const int32_t param_3 );

    void registerFlashListener( FlashStreamListener *listener );
    void unregisterFlashListener( FlashStreamListener *listener );
    uint32_t getInputMask();

private:
    std::string commonPath;
    std::string rootPath;

    std::vector<FlashMovie> flashMovies;
    std::vector<MoviePlayer> moviePlayers;
    std::set<FlashStreamListener *> streamingListeners;

    int32_t maxFlashMovies;
    int32_t currentLevel;
    int32_t unicodeFontID;

    FileCollection2D collection2D;
    FileCollection3D collection3D;

    Material *pNullMaterial;
    Material *pMaskMaterial;

    void *p2DMMemory[2];
    Render2DM render2DM[2];

    TestDriveMutex mutex;
    FlashSounds audio;

    BankCommonFlash commonResources;
    BankCommonFlash fontResources;

    MoviePlayer *pActiveMoviePlayer;

    FlashStreamListener listener;
    UnknownEntity unknown;
    FlashMouse mouse;

    char pContactType[32];

    uint8_t bInitialized : 1;
    uint8_t bLevelSetupInProgress : 1;
    uint8_t bUnknownBoolean : 1;
    uint8_t bValidateButtonPressed : 1;
    uint8_t bBackButtonPressed : 1;
    uint8_t bOption1ButtonPressed : 1;
    uint8_t bOption2ButtonPressed : 1;
    uint8_t field84_0xfec : 1;
    uint8_t bUseStick : 1;

    InputChannel *pGamepadButton0[4];

private:
    friend class BankCommonFlash;

private:
    void initLevel( FlashEntity *param_1 );
    void updateUnicodeFontBank();
    void setupLevel( const bool param_1 );
    void initMoviePlayer( bool param_1, MoviePlayer &param_2 );
    void setViewport( MoviePlayer &param_1 );
    void FUN_0099a9e0();
};

extern GSFlash gFlash;
extern GSFlash *gpFlash;
