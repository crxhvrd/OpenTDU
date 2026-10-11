#pragma once

#include <string>
#include <vector>

// CMarkup (First Objective Software) XML parser used by the original p-code
class CMarkup {
public:
    enum eNodeType : int32_t {
        MNT_None = 0,
        MNT_Element = 1,
        MNT_Text = 2,
        MNT_Whitespace = 4,
        MNT_CDATASection = 8,
        MNT_ProcessingInstruction = 0x10,
        MNT_Comment = 0x20,
        MNT_DocumentType = 0x40,
    };

public:
    inline const std::string &getError() const { return error; }
    inline void resetPos() { setPos( 0, 0, 0 ); }

public:
    CMarkup( const char *pDoc, const bool bCaseInsensitive = false );

    bool isWellFormed() const;
    bool setDoc( const char *pDoc );
    bool findElem( const char *pName );
    bool intoElem();
    bool outOfElem();
    std::string getTagName() const;
    std::string getAttrib( const char *pAttrib ) const;
    std::string getData() const;

private:
    struct ElemPos {
        int32_t StartL = 0;
        int32_t StartR = 0;
        int32_t EndL = 0;
        int32_t EndR = 0;
        int32_t Reserved = 0;
        int32_t ElemParent = 0;
        int32_t ElemChild = 0;
        int32_t ElemNext = 0;
    };

    struct TokenPos {
        const std::string *pDoc = nullptr;
        int32_t L = 0;
        int32_t R = -1;
        int32_t Next = 0;
        bool bIsString = false;
        bool bIgnoreCase = false;

        TokenPos( const std::string &doc, const bool bCaseInsensitive )
            : pDoc( &doc ), bIgnoreCase( bCaseInsensitive ) {}
        bool match( const char *pName ) const;
    };

private:
    std::string doc;
    std::string error;
    std::vector<ElemPos> elements;
    int32_t parentPos;
    int32_t pos;
    int32_t childPos;
    int32_t freePos;
    int32_t nodeType;

    // OpenTDU: case-insensitive tag/attribute names (retail is case-sensitive)
    bool bIgnoreCase;

private:
    ElemPos &elemAt( const int32_t index );
    const ElemPos &elemAt( const int32_t index ) const;
    void
    setPos( const int32_t parent, const int32_t position, const int32_t child );
    int32_t getFreePos();
    int32_t parseElement( const int32_t posParent );
    int32_t parseError( const char *pError, const char *pName = nullptr );
    int32_t findElem( const int32_t posParent,
                      int32_t position,
                      const char *pName ) const;
    bool findAttrib( TokenPos &token, const char *pAttrib ) const;
    std::string getTagName( const int32_t position ) const;
    std::string getAttrib( const int32_t position, const char *pAttrib ) const;
    std::string getData( const int32_t position ) const;
    std::string textFromDoc( int32_t left, const int32_t right ) const;
    std::string getToken( const TokenPos &token ) const;

    static bool FindAny( const char *pDoc, int32_t &nChar );
    static bool FindChar( const char *pDoc, int32_t &nChar, const char c );
    static bool FindToken( TokenPos &token );
    static int32_t ParseNode( TokenPos &token );
};

static inline bool ReadBool( const CMarkup &xml ) {
    const std::string data = xml.getData();
    return data == "true";
}

static inline float ReadFloat( const CMarkup &xml ) {
    return static_cast<float>( atof( xml.getData().c_str() ) );
}

static inline int32_t ReadInt( const CMarkup &xml ) {
    return atoi( xml.getData().c_str() );
}

static inline void
ParseBool( CMarkup &xml, const char *pName, bool &bOutValue ) {
    if ( xml.findElem( pName ) ) {
        bOutValue = ReadBool( xml );
    }
}

static inline void
ParseFloat( CMarkup &xml, const char *pName, float &outValue ) {
    if ( xml.findElem( pName ) ) {
        outValue = ReadFloat( xml );
    }
}

static inline void
ParseInt( CMarkup &xml, const char *pName, int32_t &outValue ) {
    if ( xml.findElem( pName ) ) {
        outValue = ReadInt( xml );
    }
}
