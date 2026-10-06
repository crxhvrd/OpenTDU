#pragma once

#include "game/mng_base.h"

class FlashPlayer;

static constexpr int32_t kNumLocalizedStrings = 32;
static constexpr int32_t kNumLocalizeSysVars = 10;
static constexpr int32_t kMaxLocalizeArgs = 10;

// A formatted sentence
struct FlashLocalizedString {
    char pFlashVarName[64];
    char pFormattedString[512];
};
OTDU_SIZE_MUST_MATCH( FlashLocalizedString, 0x240 );

// A ingame data macro (e.g. "NB_PTS\xC2\xA7")
struct FlashLocalizeSysVar {
    char pVarName[18];
    char pValue[64];
};
OTDU_SIZE_MUST_MATCH( FlashLocalizeSysVar, 0x52 );

class MngFlashLocalize : public Manager {
public:
    static char VersionString[64];

public:
    const char *getName() const override { return "MngFlashLocalize"; }

    inline void setDatabaseContext( const int32_t context ) {
        dbContext = context;
    }

public:
    MngFlashLocalize();
    ~MngFlashLocalize();

    virtual bool initialize( TestDriveGameInstance * ) override;

    static void OnFlashInstall();

    int32_t computeNumArgs( const char *pValue );

    void setVarLocalized( FlashPlayer *pPlayer,
                          const char *pValue,
                          const char *pVarName,
                          const char *pArg1 = nullptr );
    void setVarLocalized( FlashPlayer *pPlayer,
                          const char *pVarName,
                          uint64_t hashcode,
                          const char *pArg1 = nullptr );

    void clear();
    void clearAllSysVar();
    void setSysVar( const char *pName, const char *pValue );
    void setSysVar( const char *pName, const int32_t value );
    void setSysVar( const char *pName, const float value );
    void makeSentence( char *pDest, size_t maxLen, const char *pSrc, ... );
    void makeSentenceFromHashcode( char *pDest,
                                   size_t maxLen,
                                   uint64_t hashcode,
                                   ... );

private:
    FlashLocalizedString localizedStrings[kNumLocalizedStrings];
    int32_t dbContext;
    FlashLocalizeSysVar sysVars[kNumLocalizeSysVars];

private:
    bool makeLocalizedSentence( char *pDest,
                                size_t maxLen,
                                const char *pSrc,
                                const char *const *pArgs,
                                int32_t numArgs );
    void makeSentenceInternal( char *pDest,
                               size_t maxLen,
                               const char *pSrc,
                               va_list args );
    void expandMacroToken( const char *pToken, char *pDest );
    const char *findSysVar( const char *pToken ) const;
    void addSentenceAndSet( FlashPlayer *pPlayer,
                            const char *pVarName,
                            const char *pValue );
};

extern MngFlashLocalize *gpMngFlashLocalize;
