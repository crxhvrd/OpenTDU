#pragma once

class FlashResource;
struct edFlashConfig;
struct FlashClip;
class FlashSprite;
struct edFlashInput;
struct CXForm;
enum class eFlashProperty;

enum class eFlashCloneMirror : int32_t {
    FCM_None = 0,
    // Mirror the translation only (e.g. text must stay readable)
    FCM_Position = 1,
    // Mirror the translation and flip the geometry horizontally
    FCM_Geometry = 2,
};

struct FlashCloneDesc {
    // When null, the source is looked up by SourceDepth instead
    const char *pSourceName = nullptr;
    uint16_t SourceDepth = 0;
    const char *pCloneName = "";
    uint16_t Depth = 0;
    eFlashCloneMirror Mirror = eFlashCloneMirror::FCM_None;
    // In the parent's space (twips)
    float MirrorAxisX = 0.0f;
    // Applied after mirroring (twips)
    float OffsetX = 0.0f;
    float OffsetY = 0.0f;
};

class FlashPlayer {
public:
    inline FlashResource *getBoundFlashResource() { return pFlashResource; }
    inline void setBoundFlashResource( FlashResource *param_1 ) {
        pFlashResource = param_1;
    }

public:
    FlashPlayer( uint8_t *param_1 = nullptr,
                 uint32_t param_2 = 0,
                 edFlashInput *param_4 = nullptr,
                 edFlashConfig *param_5 = nullptr );
    ~FlashPlayer();

    void destroy();

    void showFrame( const char *param_1, const char *param_2, bool param_3 );
    void setVariableValue( const char *param_1, const char *param_2 );
    void setVariableValueNonLinked( const char *param_1, const char *param_2 );
    void setVariableValueWritableLinked( const char *param_1, char *param_2 );
    void setVariableValue( const char *param_1, const int32_t param_2 );
    void setVariableValue( const char *param_1, const uint32_t param_2 );
    void setVariableValue( const char *param_2, const float param_3 );
    void
    setVariableValueVariadic( const char *param_1, const char *param_2, ... );

    int32_t getVariableAsInt( const char *param_1 );
    bool getVariableAsFloat( const char *pVariableName, float &outValue );

    bool getVariableAsString( const char *param_2, char *param_3 );
    bool getClipPlayer( const char *param_2, FlashClip *param_1 );
    void goToFrameAndPlay( int32_t param_2, const char *param_3, bool param_4 );
    void stopClip( const char *param_2 );

    void tick( uint32_t param_1, bool param_2, bool param_3 );

    bool spriteExists( const char *param_2 );
    bool FUN_00441200( const char *param_2 );

    void releaseTextInputFocus();

    bool cloneObject( const char *pParentPath, const FlashCloneDesc &desc );
    void removeObject( const char *pParentPath, uint16_t depth );
    bool getObjectColorTransform( const char *pParentPath,
                                  uint16_t depth,
                                  CXForm *pColorTransform );
    void setObjectColorTransform( const char *pParentPath,
                                  uint16_t depth,
                                  const CXForm &colorTransform );
    void setClipProperty( const char *pClipPath,
                          const eFlashProperty property,
                          const float value );
    bool isClipAtLabel( const char *pClipPath, const char *pLabel );
    bool getObjectMatrix( const char *pParentPath,
                          uint16_t depth,
                          float pMatrix[6] );
    void setObjectMatrix( const char *pParentPath,
                          uint16_t depth,
                          const float pMatrix[6] );

private:
    FlashResource *pFlashResource;
};
