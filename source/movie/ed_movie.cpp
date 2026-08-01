#include "shared.h"
#include "ed_movie.h"

#include "audio/gs_audio.h"
#include "audio/implementation/ed_audio.h"
#include "audio/implementation/ed_audio_object.h"
#include "bink_movie.h"

edMovie gEdMovie = {}; // DAT_00f5b4b0

static constexpr const char *kEdMovieName = "edMovie"; // DAT_00ef8df0

edMovie::edMovie()
    : edBase( kEdMovieName, 0, 1, 0 ),
      SizeForUnknown( 0u ),
      pUnknownPointer( nullptr ),
      pActiveMovie( nullptr ),
      pActiveMovieIt( nullptr ) {
    buildDate = "Oct 27 2006"; // DAT_00ef8de4
    buildTime = "16:08:14";    // DAT_00ef8dd8
}

edMovie::~edMovie() {}

int32_t edMovie::initialize() {
    // FUN_006755a0
    int32_t iVar1 = 1;
    if ( iVar1 != 0 && bEnabled == false ) {
        bEnabled = true;
    }

    return iVar1 == 0;
}

void edMovie::destroy() {
    // FUN_006755e0
    if ( bEnabled ) {
        bEnabled = false;
    }

    audioPipeline.destroy();
}

bool edMovie::attachAudioVoice( edAudioStreamEntry *pVoice ) {
    if ( pVoice == nullptr ) {
        return false;
    }

    if ( gpAudio == nullptr || gEdAudio.getOutputModule() == nullptr ) {
        OTDU_LOG_WARN( "No audio output, movies stay silent\n" );
        return false;
    }

    if ( audioPipeline.getBuffer() == nullptr &&
         !audioPipeline.create( gEdAudio.getOutputModule() ) ) {
        return false;
    }

    audioPipeline.getBuffer()->addNewEntry( pVoice );

    return true;
}

void edMovie::detachAudioVoice( edAudioStreamEntry *pVoice ) {
    if ( pVoice == nullptr || audioPipeline.getBuffer() == nullptr ) {
        return;
    }

    audioPipeline.getBuffer()->unlinkEntry( pVoice );
}

BinkFramePointer *edMovie::openBik( const char *param_2 ) {
    // FUN_006754b0
    BinkMovie *pBVar2 =
        new ( TestDrive::Alloc( sizeof( BinkMovie ) ) ) BinkMovie( param_2 );

    if ( pBVar2->getState() == MPS_Unknown ) {
        pBVar2->destroy();
        pBVar2->~BinkMovie();
        TestDrive::Free( pBVar2 );
    } else {
        BinkMovieNode *pBVar3 =
            new ( TestDrive::Alloc( sizeof( BinkMovieNode ) ) ) BinkMovieNode();
        pBVar3->pContent = pBVar2;
        pBVar3->pNextFrame = pActiveMovie;
        pActiveMovie = pBVar3;

        BinkFramePointer *ppBVar4 =
            new ( TestDrive::Alloc( sizeof( BinkFramePointer ) ) )
                BinkFramePointer( pBVar2 );
        if ( ppBVar4 != nullptr ) {
            return ppBVar4;
        }
    }

    return nullptr;
}

void edMovie::stepBikPlayback() {
    // FUN_006755f0
    BinkMovieNode **ppPreviousLink = &pActiveMovie;
    BinkMovieNode *pNode = pActiveMovie;

    while ( pNode != nullptr ) {
        BinkMovie *pContent = pNode->pContent;
        pContent->update();

        if ( pContent->getState() == eMoviePlaybackState::MPS_Done ) {
            *ppPreviousLink = pNode->pNextFrame;

            pContent->destroy();
            pContent->~BinkMovie();
            TestDrive::Free( pContent );

            BinkMovieNode *pNextNode = pNode->pNextFrame;
            TestDrive::Free( pNode );
            pNode = pNextNode;
        } else {
            ppPreviousLink = &pNode->pNextFrame;
            pNode = pNode->pNextFrame;
        }
    }
}
