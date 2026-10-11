#include "shared.h"
#include "markup.h"

static constexpr const char *kTokenDelimiters = " \t\n\r<>=\\/?!";
static constexpr const char *kWhitespaces = " \t\n\r";
static constexpr const char *kNameTerminators = " =/[";

static constexpr int32_t kNumEntities = 5;
static constexpr char kEntityChars[kNumEntities] = { '<', '&', '>', '\'',
                                                     '"' }; // DAT_00f3c6e8
static constexpr const char *kEntityNames[kNumEntities] = {
    "lt;", "amp;", "gt;", "apos;", "quot;" }; // PTR_DAT_00fabf08
static constexpr int32_t kEntityLengths[kNumEntities] = { 3, 4, 3, 5,
                                                          5 }; // DAT_00fabef4

CMarkup::CMarkup( const char *pDoc, const bool bCaseInsensitive )
    : doc(),
      error(),
      elements(),
      parentPos( 0 ),
      pos( 0 ),
      childPos( 0 ),
      freePos( 0 ),
      nodeType( 0 ),
      bIgnoreCase( bCaseInsensitive ) {
    // FUN_004067e0
    setDoc( pDoc );
}

bool CMarkup::isWellFormed() const {
    return !elements.empty() && elements[0].ElemChild != 0;
}

bool CMarkup::setDoc( const char *pDoc ) {
    // FUN_006db9f0
    freePos = 1;
    setPos( 0, 0, 0 );
    if ( pDoc != nullptr ) {
        doc = pDoc;
    }

    const int32_t numReserved = static_cast<int32_t>( doc.size() ) / 64 + 8;
    if ( static_cast<int32_t>( elements.size() ) < numReserved ) {
        elements.resize( numReserved );
    }

    bool bWellFormed = false;
    if ( !doc.empty() ) {
        elemAt( 0 ) = ElemPos();
        const int32_t position = parseElement( 0 );
        if ( position > 0 ) {
            elemAt( 0 ).ElemChild = position;
            bWellFormed = true;
        }
    }

    if ( !bWellFormed ) {
        elemAt( 0 ) = ElemPos();
        freePos = 1;
    }
    setPos( 0, 0, 0 );
    return bWellFormed;
}

bool CMarkup::findElem( const char *pName ) {
    // FUN_006db960
    if ( !elements.empty() ) {
        const int32_t position = findElem( parentPos, pos, pName );
        if ( position != 0 ) {
            setPos( elemAt( position ).ElemParent, position, 0 );
            return true;
        }
    }
    return false;
}

bool CMarkup::intoElem() {
    // FUN_006da7d0
    if ( pos != 0 && nodeType == MNT_Element ) {
        setPos( pos, childPos, 0 );
        return true;
    }
    return false;
}

bool CMarkup::outOfElem() {
    // FUN_006da760
    if ( parentPos != 0 ) {
        setPos( elemAt( parentPos ).ElemParent, parentPos, pos );
        return true;
    }
    return false;
}

std::string CMarkup::getTagName() const {
    // FUN_006dbf40
    if ( pos != 0 ) {
        return getTagName( pos );
    }
    return "";
}

std::string CMarkup::getAttrib( const char *pAttrib ) const {
    return getAttrib( pos, pAttrib );
}

std::string CMarkup::getData() const { return getData( pos ); }

CMarkup::ElemPos &CMarkup::elemAt( const int32_t index ) {
    if ( static_cast<int32_t>( elements.size() ) <= index ) {
        elements.resize( index + 1 );
    }
    return elements[index];
}

const CMarkup::ElemPos &CMarkup::elemAt( const int32_t index ) const {
    OTDU_ASSERT( index < static_cast<int32_t>( elements.size() ) );
    return elements[index];
}

void CMarkup::setPos( const int32_t parent,
                      const int32_t position,
                      const int32_t child ) {
    parentPos = parent;
    pos = position;
    childPos = child;
    nodeType = ( position != 0 ) ? MNT_Element : MNT_None;
}

int32_t CMarkup::getFreePos() {
    // FUN_006da710
    if ( freePos == static_cast<int32_t>( elements.size() ) ) {
        elements.resize( freePos / 2 + freePos );
    }
    return freePos++;
}

int32_t CMarkup::parseElement( const int32_t posParent ) {
    // FUN_006db2c0
    const int32_t position = getFreePos();
    elemAt( position ).StartL = elemAt( posParent ).EndL;
    elemAt( position ).ElemParent = posParent;
    elemAt( position ).ElemChild = 0;
    elemAt( position ).ElemNext = 0;

    TokenPos token( doc, bIgnoreCase );
    token.Next = elemAt( posParent ).EndL;

    std::string name;
    while ( name.empty() ) {
        elemAt( position ).StartL = token.Next;
        if ( !FindChar( doc.c_str(), elemAt( position ).StartL, '<' ) ) {
            return parseError( "Element tag not found" );
        }

        elemAt( posParent ).EndL = elemAt( position ).StartL;

        token.Next = elemAt( position ).StartL + 1;
        if ( !FindToken( token ) ) {
            return parseError( "Abrupt end within tag" );
        }
        if ( token.bIsString ) {
            return parseError( "Tag starts with quote" );
        }

        const char firstChar = doc[token.L];
        if ( firstChar == '?' || firstChar == '!' ) {
            token.Next = elemAt( position ).StartL;
            if ( ParseNode( token ) == MNT_None ) {
                return parseError( "Invalid node" );
            }
        } else if ( firstChar == '/' ) {
            freePos--;
            return 0;
        } else {
            name = getToken( token );
            if ( !FindChar( doc.c_str(), token.Next, '>' ) ) {
                return parseError( "End of tag not found" );
            }
        }
    }
    elemAt( position ).StartR = token.Next;

    if ( doc[elemAt( position ).StartR - 1] == '/' ) {
        elemAt( position ).EndL = elemAt( position ).StartR - 1;
        elemAt( position ).EndR = elemAt( position ).StartR;
        return position;
    }

    int32_t innerPos = 0;
    int32_t innerPrevPos = 0;
    elemAt( position ).EndL = elemAt( position ).StartR + 1;
    while ( ( innerPos = parseElement( position ) ) > 0 ) {
        if ( innerPrevPos != 0 ) {
            elemAt( innerPrevPos ).ElemNext = innerPos;
        } else {
            elemAt( position ).ElemChild = innerPos;
        }
        innerPrevPos = innerPos;

        elemAt( position ).EndL = elemAt( innerPos ).EndR + 1;
    }
    if ( innerPos == -1 ) {
        return -1;
    }

    if ( !FindChar( doc.c_str(), elemAt( position ).EndL, '<' ) ) {
        return parseError( "End tag of %s element not found", name.c_str() );
    }

    token.Next = elemAt( position ).EndL + 1;
    int32_t numTokens = 0;
    while ( FindToken( token ) ) {
        numTokens++;
        if ( token.bIsString ) {
            continue;
        }

        if ( numTokens == 1 && doc[token.L] != '/' ) {
            return parseError( "Expecting end tag of element %s",
                               name.c_str() );
        } else if ( numTokens == 2 && !token.match( name.c_str() ) ) {
            return parseError( "End tag does not correspond to %s",
                               name.c_str() );
        } else if ( doc[token.L] == '>' ) {
            break;
        }
    }

    if ( doc.c_str()[token.L] == '\0' || numTokens < 2 ) {
        return parseError( "End tag not completed for element %s",
                           name.c_str() );
    }
    elemAt( position ).EndR = token.L;
    return position;
}

int32_t CMarkup::parseError( const char *pError, const char *pName ) {
    // FUN_006dadb0
    if ( pName != nullptr ) {
        char buffer[256];
        snprintf( buffer, sizeof( buffer ), pError, pName );
        error = buffer;
    } else {
        error = pError;
    }
    freePos--;
    return -1;
}

int32_t CMarkup::findElem( const int32_t posParent,
                           int32_t position,
                           const char *pName ) const {
    // FUN_006db0e0
    if ( position != 0 ) {
        position = elemAt( position ).ElemNext;
    } else {
        position = elemAt( posParent ).ElemChild;
    }

    if ( pName == nullptr || *pName == '\0' ) {
        return position;
    }

    TokenPos token( doc, bIgnoreCase );
    while ( position != 0 ) {
        token.Next = elemAt( position ).StartL + 1;
        FindToken( token );
        if ( token.match( pName ) ) {
            return position;
        }
        position = elemAt( position ).ElemNext;
    }
    return 0;
}

bool CMarkup::findAttrib( TokenPos &token, const char *pAttrib ) const {
    // FUN_006daf10
    int32_t attribIndex = 0;
    for ( int32_t numTokens = 0; FindToken( token ); numTokens++ ) {
        if ( !token.bIsString ) {
            const char c = doc[token.L];
            if ( c == '>' || c == '/' || c == '?' ) {
                break;
            }
            if ( c == '=' ) {
                continue;
            }
            if ( attribIndex == 0 && numTokens != 0 ) {
                if ( pAttrib == nullptr || *pAttrib == '\0' ) {
                    return true;
                }
                if ( token.match( pAttrib ) ) {
                    attribIndex = numTokens;
                }
            }
        } else if ( attribIndex != 0 && numTokens == attribIndex + 2 ) {
            return true;
        }
    }
    return false;
}

std::string CMarkup::getTagName( const int32_t position ) const {
    // FUN_006dbed0
    TokenPos token( doc, bIgnoreCase );
    token.Next = elemAt( position ).StartL + 1;
    if ( position == 0 || !FindToken( token ) ) {
        return "";
    }
    return getToken( token );
}

std::string CMarkup::getAttrib( const int32_t position,
                                const char *pAttrib ) const {
    // FUN_006dbe30
    TokenPos token( doc, bIgnoreCase );
    if ( position == 0 || nodeType != MNT_Element ) {
        return "";
    }
    token.Next = elemAt( position ).StartL + 1;

    if ( pAttrib != nullptr && findAttrib( token, pAttrib ) ) {
        const int32_t docLength = static_cast<int32_t>( doc.size() );
        return textFromDoc( token.L,
                            token.R - ( ( token.R < docLength ) ? 0 : 1 ) );
    }
    return "";
}

std::string CMarkup::getData( const int32_t position ) const {
    // FUN_006dadf0
    const ElemPos &elem = elemAt( position );
    if ( elem.ElemChild != 0 || elem.StartR == elem.EndL + 1 ) {
        return "";
    }

    const char *pDoc = doc.c_str();
    int32_t nChar = elem.StartR + 1;
    if ( FindAny( pDoc, nChar ) && pDoc[nChar] == '<' &&
         nChar + 11 < elem.EndL &&
         strncmp( pDoc + nChar, "<![CDATA[", 9 ) == 0 ) {
        nChar += 9;
        const size_t endCDATA = doc.find( "]]>", nChar ); // FUN_006dc9d0
        if ( endCDATA != std::string::npos &&
             static_cast<int32_t>( endCDATA ) < elem.EndL ) {
            return doc.substr( nChar, endCDATA - nChar );
        }
    }
    return textFromDoc( elem.StartR + 1, elem.EndL - 1 );
}

std::string CMarkup::textFromDoc( int32_t left, const int32_t right ) const {
    // FUN_006daa00
    std::string text;
    text.reserve( std::max( right - left + 2, 0 ) );
    while ( left <= right ) {
        const char c = doc[left];
        if ( c != '&' ) {
            text += c;
            left++;
            continue;
        }

        int32_t entityIndex = 0;
        for ( ; entityIndex < kNumEntities; entityIndex++ ) {
            const int32_t length = kEntityLengths[entityIndex];
            if ( left <= right - length &&
                 strncmp( kEntityNames[entityIndex], &doc[left + 1], length ) ==
                     0 ) {
                break;
            }
        }

        if ( entityIndex < kNumEntities ) {
            text += kEntityChars[entityIndex];
            left += 1 + kEntityLengths[entityIndex];
        } else {
            text += '&';
            left++;
        }
    }
    return text;
}

std::string CMarkup::getToken( const TokenPos &token ) const {
    // FUN_006db260
    if ( token.L > token.R ) {
        return "";
    }
    const int32_t docLength = static_cast<int32_t>( doc.size() );
    return doc.substr( token.L, token.R - token.L +
                                    ( ( token.R < docLength ) ? 1 : 0 ) );
}

bool CMarkup::TokenPos::match( const char *pName ) const {
    // FUN_00430d90
    const size_t length = static_cast<size_t>( R - L + 1 );
    const int32_t result = bIgnoreCase
                               ? StringNCmpI( pDoc->c_str() + L, pName, length )
                               : strncmp( pDoc->c_str() + L, pName, length );
    if ( result != 0 ) {
        return false;
    }
    return pName[length] == '\0' ||
           strchr( kNameTerminators, pName[length] ) != nullptr;
}

bool CMarkup::FindAny( const char *pDoc, int32_t &nChar ) {
    // FUN_006da200
    while ( pDoc[nChar] != '\0' && strchr( kWhitespaces, pDoc[nChar] ) ) {
        nChar++;
    }
    return pDoc[nChar] != '\0';
}

bool CMarkup::FindChar( const char *pDoc, int32_t &nChar, const char c ) {
    // FUN_006da6e0
    const char *pChar = pDoc + nChar;
    while ( *pChar != '\0' && *pChar != c ) {
        pChar++;
    }
    nChar = static_cast<int32_t>( pChar - pDoc );
    return *pChar != '\0';
}

bool CMarkup::FindToken( TokenPos &token ) {
    // FUN_006dacf0
    const char *pDoc = token.pDoc->c_str();
    int32_t nChar = token.Next;
    token.bIsString = false;

    if ( !FindAny( pDoc, nChar ) ) {
        token.L = nChar;
        token.R = nChar;
        token.Next = nChar;
        return false;
    }

    const char quote = pDoc[nChar];
    if ( quote == '"' || quote == '\'' ) {
        nChar++;
        token.L = nChar;
        token.bIsString = true;
        FindChar( pDoc, nChar, quote );
        token.R = nChar - 1;
        if ( pDoc[nChar] != '\0' ) {
            nChar++;
        }
        token.Next = nChar;
        return true;
    }

    token.L = nChar;
    while ( pDoc[nChar] != '\0' &&
            strchr( kTokenDelimiters, pDoc[nChar] ) == nullptr ) {
        nChar++;
    }
    if ( nChar == token.L ) {
        nChar++;
    }
    token.R = nChar - 1;
    token.Next = nChar;
    return true;
}

int32_t CMarkup::ParseNode( TokenPos &token ) {
    // FUN_006daf90
    const char *pDoc = token.pDoc->c_str();
    token.L = token.Next;
    const char *pNode = pDoc + token.Next;

    if ( *pNode != '<' ) {
        if ( *pNode == '\0' ) {
            return MNT_None;
        }
        if ( FindAny( pDoc, token.Next ) && pDoc[token.Next] != '<' ) {
            FindChar( pDoc, token.Next, '<' );
            return MNT_Text;
        }
        return MNT_Whitespace;
    }

    if ( pNode[1] == '\0' || pNode[2] == '\0' ) {
        return MNT_None;
    }

    int32_t type = MNT_None;
    const char *pEndOfNode = nullptr;
    if ( pNode[1] == '?' ) {
        type = MNT_ProcessingInstruction;
        pEndOfNode = "?>";
    } else if ( pNode[1] == '!' ) {
        if ( pNode[2] == '[' ) {
            type = MNT_CDATASection;
            pEndOfNode = "]]>";
        } else if ( pNode[2] == '-' ) {
            type = MNT_Comment;
            pEndOfNode = "-->";
        } else {
            int32_t numBrackets = 0;
            while ( FindToken( token ) ) {
                if ( token.bIsString ) {
                    continue;
                }
                const char c = pDoc[token.L];
                if ( c == '[' ) {
                    numBrackets++;
                } else if ( c == ']' ) {
                    numBrackets--;
                } else if ( numBrackets == 0 && c == '>' ) {
                    return MNT_DocumentType;
                }
            }
            return MNT_None;
        }
    } else if ( pNode[1] == '/' ) {
        return MNT_None;
    } else {
        return MNT_Element;
    }

    const char *pEnd = strstr( pNode, pEndOfNode );
    if ( pEnd == nullptr ) {
        return MNT_None;
    }
    token.Next = static_cast<int32_t>( pEnd - pDoc ) +
                 static_cast<int32_t>( strlen( pEndOfNode ) );
    return type;
}
