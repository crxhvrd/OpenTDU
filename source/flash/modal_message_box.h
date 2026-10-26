#pragma once

#include "core/popup_callback.h"
#include "flash/mng_flash.h"

class FlashPlayer;
class ModalMessageBox;

enum eModalPromptType : int32_t {
    MPT_Warning = 0,  // '/avert', label on/on_choice/no_data, /:m_AvertText
    MPT_Default = 1,  // '/avert', label msg, /:m_MsgText
    MPT_Tutorial = 2, // '/tutorial', label on/on_group/on_group_moto
};

using PopupAnswerCallback_t = void ( * )( ePopupAnswer, void *, int32_t );

namespace TestDrive {
    void DisplayModalPrompt( bool bHasCancel,
                             uint64_t messageHashcode,
                             int32_t messageDatabaseIndex,
                             PopupAnswerCallback_t pAnswerCallback,
                             PopupCallback *pUserCallback,
                             void *pUserCallbackData,
                             int32_t userData,
                             const char *pDefaultMessage,
                             bool bHasValidButton,
                             bool bNoData,
                             eModalPromptType promptType,
                             ModalMessageBox *pMessageBox );
} // namespace TestDrive

class ModalMessageBox {
public:
    enum class eInputState : int32_t {
        MMBIS_None = 0,
        MMBIS_Displayed1Sec = 1,
        MMBIS_Displayed3Sec = 2,
        MMBIS_Displayed = 3,
        MMBIS_CloseRequested = 4,
        MMBIS_Closing = 5
    };

    enum eDisplayState : int32_t {
        MMBDS_Closed = 0,
        MMBDS_Opening = 1,
        MMBDS_Opened = 2,
        MMBDS_Closing = 3,
    };

public:
    inline bool getUnknownFlag() const { return bUnknownFlag; }
    inline int32_t getNumOpenMsgBox() const { return numOpenMessageBox; }
    inline FlashPlayer *getFlashPlayer() const { return pFlashPlayer; }

public:
    ModalMessageBox();
    ~ModalMessageBox();

    void tick();
    bool display( uint64_t hashcode,
                  float param_2,
                  int32_t param_5,
                  void ( *pCallback )( void * ),
                  void *pCallbackData,
                  bool param_7 );
    eDisplayState getDisplayState() const;
    eInputState getInputState() const;
    bool close();

    void setPlayer( FlashPlayer *pPlayer );

    void forceClose();
    void FUN_0099feb0();
    bool FUN_00403100();
    void FUN_0099fcc0();
    void FUN_009a0110();
    void FUN_0099fc80();
    bool FUN_0099fbe0();

    static constexpr FlashMessage kCloseMessageBox =
        FlashMessage( 0x756f6567u );
    static constexpr FlashMessage kOpenMessageBox = FlashMessage( 0x6e696567u );

    static constexpr FlashMessage kMouseIn = FlashMessage( 0x6e69636du );
    static constexpr FlashMessage kMouseOut = FlashMessage( 0x756f636du );

    static constexpr FlashMessage kAnswerYes = FlashMessage( 0x65797661u );
    static constexpr FlashMessage kAnswerNo = FlashMessage( 0x6f6e7661u );
    static constexpr FlashMessage kAnswerOk = FlashMessage( 0x7661656eu );

    static constexpr FlashMessage kFriendAccept = FlashMessage( 0x6b6f7266u );
    static constexpr FlashMessage kFriendCancel = FlashMessage( 0x61637266u );

    static constexpr FlashMessage kTextFocus = FlashMessage( 0x636f6674u );
    static constexpr FlashMessage kClms = FlashMessage( 0x736d6c63u );
    static constexpr FlashMessage kCamera = FlashMessage( 0x736d6163u );

    static constexpr FlashMessage kTutorialIn = FlashMessage( 0x6e697574u );
    static constexpr FlashMessage kTutorialOut = FlashMessage( 0x756f7574u );
    static constexpr FlashMessage kWend = FlashMessage( 0x646e6577u );

private:
    friend class UnknownEntity;
    friend void TestDrive::DisplayModalPrompt( bool,
                                               uint64_t,
                                               int32_t,
                                               PopupAnswerCallback_t,
                                               PopupCallback *,
                                               void *,
                                               int32_t,
                                               const char *,
                                               bool,
                                               bool,
                                               eModalPromptType,
                                               ModalMessageBox * );

private:
    FlashPlayer *pFlashPlayer;
    eInputState inputState;
    eDisplayState displayState;

    eModalPromptType promptType;
    bool bHasCancel;
    uint64_t messageHashcode;
    int32_t messageDatabaseIndex;
    const char *pDefaultMessage;

    PopupAnswerCallback_t pAnswerCallback;
    PopupCallback *pUserCallback;
    void *pUserCallbackData;

    void ( *pWaitingCallback )( void * );
    void *pWaitingCallbackData;

    float displayTime;
    float closeAfterDuration;
    ePopupAnswer answer;
    int32_t userData;
    int32_t numOpenMessageBox;

    uint8_t bUnknownFlag : 1;
    uint8_t bMouseActive : 1;
    uint8_t bVisible : 1;
    uint8_t bUnknownFlag2 : 1;

private:
    const char *getFrameToDisplay( const float param_1 ) const;
    void updateInputState( const float param_1 );
};

extern ModalMessageBox *gpFlashMessageBox;
