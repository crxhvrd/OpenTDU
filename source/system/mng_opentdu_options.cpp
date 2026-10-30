#include "shared.h"
#include "mng_opentdu_options.h"

#include "database/gs_database.h"
#include "database/port_strings.h"
#include "flash/gs_flash.h"
#include "flash/implementation/flash_display_list.h"
#include "flash/implementation/flash_player.h"
#include "flash/implementation/flash_sprite.h"
#include "tdu_instance.h"

static MngOpenTDUOptions gOpenTDUOptions = {};
MngOpenTDUOptions *gpOpenTDUOptions = &gOpenTDUOptions;

static constexpr const char *kMovieName = "OPENTDU";
static constexpr const char *kOptionsMovieName = "CONFIGPC";
static constexpr const char *kGeneralMovieName = "GENERAL";

static constexpr int8_t kOpenTDUOptionsFlagBit = 5;

// Both movies share the CONFIGPC page layout
static constexpr const char *kPageClips[] = { "/menus", "/help" };
// The settings pages (OptionsWidgetPage)
static constexpr const char *kSettingsClip = "/keymap";
static constexpr const char *kBackgroundClip = "/fond";
static constexpr const char *kMenusClip = "/menus";
static constexpr const char *kHelpClip = "/help";
static constexpr const char *kMouseClip = "/mouse";
static constexpr const char *kListPath = "/menus/ss_menus/ssm";
static constexpr const char *kCategoryListOwnerClip = "/menus/ss_menus";

// Set by the '/menus/ss_menus' script that fills the options list. Moving the
// cursor sends '/help' to the label '<WhereTo>_<entry>'; validating an entry
// sends '/menus/ss_menus' there too. OPENTDU has these labels for its own
// list owner (see AssetExtractor::BuildGeneratedMovies), CONFIGPC does not.
static constexpr const char *kListOwnerVariable = "/:WhereTo";
static constexpr const char *kOptionsListOwner = "options";
static constexpr const char *kOpenTDUListOwner = "opentdu";
// The validated entry (1-based), written by the entry widget
static constexpr const char *kValidatedEntryVariable = "/:num_ssm_temp";
static constexpr const char *kReleaseButtonVariable = "/:release_button";
static constexpr const char *kTitleVariable = "/:m_Options";
// Shown by the help frame the OPENTDU list labels point at
static constexpr const char *kCategoryHelpVariable = "/:mpc_GameSettingsHelp";

static constexpr const char *kOpenTDUEntryHelpLabel = "back";

static constexpr FlashMessage kPlaySound = FlashMessage( "sndp" );

static constexpr int32_t kNumRetailOptionsEntries = 8;
// Opens the OpenTDU video settings instead of the retail video page
static constexpr int32_t kRetailVideoEntry = 7;
static constexpr int32_t kOpenTDUEntry = kNumRetailOptionsEntries + 1;
// sur1..sur12 are placed in the list sprite
static constexpr int32_t kMaxListEntries = 12;

static constexpr uint32_t kLocalizationContextConfigPC = 2;

struct OpenTDUCategoryDesc {
    // Localized label set on the movie root by setFlashLocalization, or a
    // port string when null
    const char *pLabelVariable;
    ePortString Label;
    ePortString Help;
    bool bLoginOnly;
};

static constexpr OpenTDUCategoryDesc kCategories[] = {
    {"/:mpc_Controls",   PS_Count, PS_OpenTDUControlsHelp,     false},
    {"/:m_GameSettings", PS_Count, PS_OpenTDUGameSettingsHelp, false},
    {"/:mpc_Video",      PS_Count, PS_OpenTDUVideoHelp,        false},
    // Reloads the databases
    {nullptr,            PS_Mods,  PS_OpenTDUModsHelp,         true },
};

// Their list entries open the controls page
// (AssetExtractor::BuildGeneratedMovies)
static constexpr int32_t kVideoCategory = 2;
static constexpr int32_t kModsCategory = 3;
static constexpr int32_t kNumCategories = int32_t( std::size( kCategories ) );
static_assert( kNumCategories <= kMaxListEntries );

static bool
SetClipVisible( FlashPlayer *pPlayer, const char *pPath, const bool bVisible ) {
    FlashClip clip;
    if ( pPlayer == nullptr || !pPlayer->getClipPlayer( pPath, &clip ) ||
         clip.pSprite == nullptr ) {
        return false;
    }

    clip.setProperty( eFlashProperty::FP_Visible, bVisible );
    return true;
}

static bool IsListOwner( FlashPlayer *pPlayer, const char *pOwner ) {
    char owner[256] = {};
    return pPlayer->getVariableAsString( kListOwnerVariable, owner ) &&
           strcmp( owner, pOwner ) == 0;
}

static std::string GetEntryPath( const int32_t entry ) {
    return std::string( kListPath ) + "/sur" + std::to_string( entry );
}

static void SetEntryVariable( FlashPlayer *pPlayer,
                              const char *pName,
                              const int32_t entry,
                              const char *pValue ) {
    const std::string path =
        std::string( kListPath ) + ":" + pName + std::to_string( entry );
    pPlayer->setVariableValueNonLinked( path.c_str(), pValue );
}

static int32_t GetListVariable( FlashPlayer *pPlayer, const char *pName ) {
    const std::string path = std::string( kListPath ) + ":" + pName;
    return pPlayer->getVariableAsInt( path.c_str() );
}

// The list hides the entries past its count as it opens, which is before the
// count can be patched
static void SetEntryCount( FlashPlayer *pPlayer, const int32_t count ) {
    const std::string path = std::string( kListPath ) + ":nb_ssms";
    pPlayer->setVariableValue( path.c_str(), count );

    for ( int32_t i = 1; i <= kMaxListEntries; i++ ) {
        SetClipVisible( pPlayer, GetEntryPath( i ).c_str(), i <= count );
    }
}

// The entry widget plays 'valid' then, from its last frame, sends ss_menus to
// the entry label
static bool IsEntryValidating( FlashPlayer *pPlayer, const int32_t entry ) {
    FlashClip clip;
    if ( !pPlayer->getClipPlayer( GetEntryPath( entry ).c_str(), &clip ) ||
         clip.pSprite == nullptr ) {
        return false;
    }

    ScopedMutexLock lock = gpFlash->scopedLock();
    const int32_t frame =
        clip.pSprite->getDisplayList()->getCurrentFrameIndex();
    const int32_t validFrame = clip.pSprite->findLabel( "valid" );
    const int32_t changeFrame = clip.pSprite->findLabel( "change" );
    return validFrame > 0 && frame >= validFrame && frame < changeFrame;
}

MngOpenTDUOptions::MngOpenTDUOptions()
    : pMovie( nullptr ),
      pOptionsMovie( nullptr ),
      optionsCursor( 0 ),
      categoryCursor( 0 ),
      state( OS_Closed ),
      bBackLatched( false ),
      bVideoPageRequested( false ),
      bDirectVideoPage( false ),
      bPageHidden( false ) {}

MngOpenTDUOptions::~MngOpenTDUOptions() {
    pMovie = nullptr;
    pOptionsMovie = nullptr;
}

void MngOpenTDUOptions::RegisterMovie() {
    if ( gpFlash == nullptr ) {
        return;
    }

    static bool bRegistered = false;
    if ( bRegistered ) {
        return;
    }
    bRegistered = true;

    FlashMovie movie;
    movie.Filename = kMovieName;
    // Loaded with CONFIGPC, in every game mode
    movie.GameMode = -2;
    movie.MaxNumVars = 2048;
    // The settings pages draw their widget pools and texts on top of the
    // copied CONFIGPC pages (DrawList does not check its bounds)
    movie.NumDrawListStrips = 2500;
    movie.NumDrawListVertices = 30000;
    // Generated there by AssetExtractor::BuildGeneratedMovies
    movie.RootPath = "opentdu_data/flash/";

    gpFlash->pushMovie( movie );
}

bool MngOpenTDUOptions::bind() {
    pMovie = gpFlash->getFlashPlayer( kMovieName );
    pOptionsMovie = gpFlash->getFlashPlayer( kOptionsMovieName );
    if ( pMovie == nullptr || pOptionsMovie == nullptr ) {
        pMovie = nullptr;
        pOptionsMovie = nullptr;
        return false;
    }

    gpFlash->bindPlayerEntity( this, kMovieName );
    settingsPage.bind( pMovie );

    state = OS_Closed;
    optionsCursor = 0;
    categoryCursor = 0;
    bBackLatched = false;
    bPageHidden = false;
    return true;
}

void MngOpenTDUOptions::unbind() {
    if ( state != OS_Closed ) {
        finishClose();
    }

    pMovie = nullptr;
    pOptionsMovie = nullptr;
}

// The movie is never paused: its root frame has to run before the page can
// be opened. Hiding the page is enough to keep it off screen.
bool MngOpenTDUOptions::setMovieActive( const bool bActive ) {
    bool bPageFound = SetClipVisible( pMovie, kBackgroundClip, false );
    for ( const char *pClip : kPageClips ) {
        bPageFound &= SetClipVisible( pMovie, pClip, bActive );
    }
    bPageFound &= SetClipVisible( pMovie, kSettingsClip, bActive );

    gpFlash->enablePlayer( kMovieName, ModalMessageBox::MMBDS_Closed, bActive,
                           kOpenTDUOptionsFlagBit );
    gpFlash->setMovieDrawnOnTop( kMovieName, bActive );
    // GENERAL holds the message boxes, drawn over every other movie (it is
    // the first one)
    gpFlash->setMovieDrawnOnTop( kGeneralMovieName, bActive );

    // Flash primitives are bucketed by material and depth tested, so GENERAL's
    // cursor cannot be raised over the whole page: OPENTDU draws its own (its
    // last root instance) while it is up
    SetClipVisible( pMovie, kMouseClip, false );
    if ( FlashPlayer *pGeneral =
             gpFlash->getFlashPlayer( kGeneralMovieName ) ) {
        SetClipVisible( pGeneral, kMouseClip, !bActive );
        gpFlash->getMouse()->setPlayer( bActive ? pMovie : pGeneral );
    }
    return bPageFound;
}

void MngOpenTDUOptions::setOptionsPageVisible( const bool bVisible ) {
    for ( const char *pClip : kPageClips ) {
        SetClipVisible( pOptionsMovie, pClip, bVisible );
    }

    gpFlash->enablePlayer( kOptionsMovieName, ModalMessageBox::MMBDS_Closed,
                           bVisible, kOpenTDUOptionsFlagBit );
}

void MngOpenTDUOptions::open() {
    if ( pMovie == nullptr || state != OS_Closed ) {
        return;
    }

    state = OS_Open;
    categoryCursor = 0;
    bBackLatched = true;

    pMovie->setVariableValueNonLinked( kListOwnerVariable, "" );

    // Done here rather than on bind: the movie's first frame resets every
    // string to its placeholder
    uint32_t localizationIndex = 0;
    gpDatabase->setFlashLocalization( localizationIndex,
                                      kLocalizationContextConfigPC, pMovie );
    pMovie->setVariableValueNonLinked( kTitleVariable,
                                       GetPortString( PS_OpenTDUOptions ) );

    setOptionsPageVisible( false );
    setMovieActive( true );

    // CONFIGPC fades it in from its root timeline, which OPENTDU lacks
    pMovie->setClipProperty( kHelpClip, eFlashProperty::FP_Alpha, 100.0f );

    pMovie->showFrame( "open", kMenusClip, true );
}

void MngOpenTDUOptions::close() {
    if ( state != OS_Open ) {
        return;
    }

    state = OS_Closing;
    pMovie->showFrame( "close", kMenusClip, true );
}

void MngOpenTDUOptions::finishClose() {
    state = OS_Closed;
    bVideoPageRequested = false;
    bDirectVideoPage = false;
    settingsPage.close();

    setMovieActive( false );
    setOptionsPageVisible( true );
}

void MngOpenTDUOptions::tickOptionsEntry() {
    if ( !IsListOwner( pOptionsMovie, kOptionsListOwner ) ) {
        optionsCursor = 0;
        return;
    }

    // The list script sets its own entry count every time it (re)opens
    if ( GetListVariable( pOptionsMovie, "nb_ssms" ) ==
         kNumRetailOptionsEntries ) {
        SetEntryCount( pOptionsMovie, kOpenTDUEntry );
        SetEntryVariable( pOptionsMovie, "name_", kOpenTDUEntry,
                          GetPortString( PS_OpenTDUOptions ) );
        SetEntryVariable( pOptionsMovie, "lock_", kOpenTDUEntry, "0" );
    }

    const int32_t cursor = GetListVariable( pOptionsMovie, "num_ssm" );
    if ( cursor != optionsCursor ) {
        optionsCursor = cursor;
        if ( cursor == kOpenTDUEntry ) {
            pOptionsMovie->showFrame( kOpenTDUEntryHelpLabel, kHelpClip, true );
        }
    }

    // Caught before the widget sends ss_menus to a label CONFIGPC lacks (it
    // would then play on into the next page)
    if ( IsEntryValidating( pOptionsMovie, kOpenTDUEntry ) ) {
        pOptionsMovie->showFrame( "change",
                                  GetEntryPath( kOpenTDUEntry ).c_str(), true );
        pOptionsMovie->setVariableValue( kReleaseButtonVariable, 0 );
        open();
    }

    if ( IsEntryValidating( pOptionsMovie, kRetailVideoEntry ) ) {
        pOptionsMovie->showFrame(
            "change", GetEntryPath( kRetailVideoEntry ).c_str(), true );
        pOptionsMovie->setVariableValue( kReleaseButtonVariable, 0 );
        bVideoPageRequested = true;
        open();
    }
}

void MngOpenTDUOptions::tickCategoryList() {
    if ( IsListOwner( pMovie, kOptionsListOwner ) ) {
        pMovie->setVariableValueNonLinked( kListOwnerVariable,
                                           kOpenTDUListOwner );
        pMovie->setVariableValue( kValidatedEntryVariable, 0 );
        // Back from a page: the help line is still the page's
        categoryCursor = 0;

        // The list's own scripts went back to it (its key handlers stay active
        // under a page)
        settingsPage.onListRestored();

        SetEntryCount( pMovie, kNumCategories );
        const bool bLogin =
            gpTestDriveInstance->getActiveGameMode() == GM_Login;
        for ( int32_t i = 0; i < kNumCategories; i++ ) {
            const OpenTDUCategoryDesc &desc = kCategories[i];
            char label[256] = {};
            if ( desc.pLabelVariable != nullptr ) {
                pMovie->getVariableAsString( desc.pLabelVariable, label );
            } else {
                strncpy( label, GetPortString( desc.Label ),
                         sizeof( label ) - 1 );
            }
            SetEntryVariable( pMovie, "name_", i + 1, label );
            SetEntryVariable( pMovie, "lock_", i + 1,
                              ( desc.bLoginOnly && !bLogin ) ? "1" : "0" );
        }

        if ( bVideoPageRequested ) {
            bVideoPageRequested = false;
            bDirectVideoPage = true;
            // The page header names the list entry under the cursor
            const std::string cursorPath =
                std::string( kListPath ) + ":num_ssm";
            pMovie->setVariableValue( cursorPath.c_str(), kVideoCategory + 1 );
            const std::string entryLabel = std::string( kOpenTDUListOwner ) +
                                           "_" +
                                           std::to_string( kVideoCategory + 1 );
            pMovie->showFrame( entryLabel.c_str(), kCategoryListOwnerClip,
                               true );
            videoSettings.open( settingsPage, pMovie );
        }
        return;
    }

    if ( !IsListOwner( pMovie, kOpenTDUListOwner ) ) {
        return;
    }

    const int32_t cursor = GetListVariable( pMovie, "num_ssm" );
    if ( cursor != categoryCursor && cursor >= 1 && cursor <= kNumCategories ) {
        categoryCursor = cursor;
        pMovie->setVariableValueNonLinked(
            kCategoryHelpVariable,
            GetPortString( kCategories[cursor - 1].Help ) );
        const std::string helpLabel =
            std::string( kOpenTDUListOwner ) + "_" + std::to_string( cursor );
        pMovie->showFrame( helpLabel.c_str(), kHelpClip, true );
    }

    const int32_t entry = pMovie->getVariableAsInt( kValidatedEntryVariable );
    if ( entry > 0 ) {
        pMovie->setVariableValue( kValidatedEntryVariable, 0 );
        onCategorySelected( entry - 1 );
    }
}

void MngOpenTDUOptions::onCategorySelected( const int32_t category ) {
    if ( category < 0 || category >= kNumCategories ) {
        return;
    }

    // The video settings page opens on its own (the list goes to the
    // controls page label)
    if ( category == kVideoCategory ) {
        videoSettings.open( settingsPage, pMovie );
    } else if ( category == kModsCategory ) {
        modsSettings.open( settingsPage );
    }
}

void MngOpenTDUOptions::tick( float totalTime, float deltaTime ) {
    if ( pMovie == nullptr && !bind() ) {
        return;
    }

    switch ( state ) {
    case OS_Closed:
        if ( !bPageHidden ) {
            bPageHidden = setMovieActive( false );
        }
        tickOptionsEntry();
        break;
    case OS_Open: {
        gpFlash->grantMovieInput( kMovieName );
        // Closed by the movie (the help bar's BACK)
        if ( pMovie->isClipAtLabel( kMenusClip, "default" ) ) {
            finishClose();
            break;
        }

        // A settings page handles its own way back to the list, which may
        // already have happened on the movie side (see tickCategoryList)
        const bool bPageOpen = settingsPage.isOpen();
        tickCategoryList();
        settingsPage.tick( deltaTime );

        // Opened from the retail video entry: back to it
        if ( bDirectVideoPage && bPageOpen && !settingsPage.isOpen() ) {
            finishClose();
            break;
        }

        const bool bBackDown = gpFlash->getInputMask() == FK_Back;
        if ( bBackDown && !bBackLatched && !bPageOpen &&
             IsListOwner( pMovie, kOpenTDUListOwner ) ) {
            close();
        }
        bBackLatched = bBackDown;
        break;
    }
    case OS_Closing:
        if ( pMovie->isClipAtLabel( kMenusClip, "default" ) ) {
            finishClose();
        }
        break;
    }
}

bool MngOpenTDUOptions::onMessage( FlashMessage *pMessage,
                                   FlashPlayer *pPlayer ) {
    if ( pMessage == nullptr || state == OS_Closed ) {
        return false;
    }

    if ( settingsPage.onMessage( pMessage ) ) {
        return true;
    }

    if ( *pMessage == kPlaySound ) {
        return gpFlash->getFlashSound()->playFrontEndSound( pMessage[1].pAsChar,
                                                            0, false );
    }

    return false;
}
