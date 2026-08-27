#include "shared.h"
#include "xml_reader.h"

static inline bool IsXMLSpace( const char c ) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static inline bool IsNameChar( const char c ) {
    return !IsXMLSpace( c ) && c != '>' && c != '/' && c != '=' && c != '<' &&
           c != '\0';
}

static char *SkipIgnorable( char *pIt ) {
    while ( *pIt != '\0' ) {
        while ( IsXMLSpace( *pIt ) ) {
            pIt++;
        }

        if ( strncmp( pIt, "<!--", 4 ) == 0 ) {
            const char *pEnd = strstr( pIt + 4, "-->" );
            if ( pEnd == nullptr ) {
                return pIt + strlen( pIt );
            }
            pIt = (char *)pEnd + 3;
            continue;
        }

        if ( strncmp( pIt, "<?", 2 ) == 0 ) {
            const char *pEnd = strstr( pIt + 2, "?>" );
            if ( pEnd == nullptr ) {
                return pIt + strlen( pIt );
            }
            pIt = (char *)pEnd + 2;
            continue;
        }

        if ( strncmp( pIt, "<!", 2 ) == 0 ) {
            const char *pEnd = strchr( pIt + 2, '>' );
            if ( pEnd == nullptr ) {
                return pIt + strlen( pIt );
            }
            pIt = (char *)pEnd + 1;
            continue;
        }

        break;
    }

    return pIt;
}

XMLReader::XMLReader() : currentIndex( kInvalidIndex ), parentIndex( 0 ) {}

XMLReader::~XMLReader() {}

const char *XMLReader::stringAt( const int32_t offset ) const {
    if ( offset == kInvalidIndex ) {
        return nullptr;
    }

    return strings.data() + offset;
}

const XMLReader::XMLNode *XMLReader::currentNode() const {
    if ( currentIndex == kInvalidIndex ||
         currentIndex >= (int32_t)nodes.size() ) {
        return nullptr;
    }

    return &nodes[currentIndex];
}

int32_t XMLReader::intern( const char *pBegin, const char *pEnd ) {
    const int32_t offset = (int32_t)strings.size();

    for ( const char *pRead = pBegin; pRead < pEnd; ) {
        if ( *pRead != '&' ) {
            strings.push_back( *pRead++ );
            continue;
        }

        const char *pSemicolon = pRead + 1;
        while ( pSemicolon < pEnd && *pSemicolon != ';' &&
                *pSemicolon != '&' ) {
            pSemicolon++;
        }

        if ( pSemicolon >= pEnd || *pSemicolon != ';' ) {
            strings.push_back( *pRead++ );
            continue;
        }

        const size_t nameLength = (size_t)( pSemicolon - pRead - 1 );
        const char *pName = pRead + 1;

        char decoded = '\0';
        if ( nameLength == 3 && strncmp( pName, "amp", 3 ) == 0 ) {
            decoded = '&';
        } else if ( nameLength == 2 && strncmp( pName, "lt", 2 ) == 0 ) {
            decoded = '<';
        } else if ( nameLength == 2 && strncmp( pName, "gt", 2 ) == 0 ) {
            decoded = '>';
        } else if ( nameLength == 4 && strncmp( pName, "quot", 4 ) == 0 ) {
            decoded = '"';
        } else if ( nameLength == 4 && strncmp( pName, "apos", 4 ) == 0 ) {
            decoded = '\'';
        } else if ( nameLength > 1 && pName[0] == '#' ) {
            const bool bHexadecimal = ( pName[1] == 'x' || pName[1] == 'X' );
            const int32_t codepoint =
                (int32_t)strtol( pName + ( bHexadecimal ? 2 : 1 ), nullptr,
                                 bHexadecimal ? 16 : 10 );
            decoded =
                ( codepoint > 0 && codepoint < 0x100 ) ? (char)codepoint : '?';
        }

        if ( decoded == '\0' ) {
            strings.push_back( *pRead++ );
            continue;
        }

        strings.push_back( decoded );
        pRead = pSemicolon + 1;
    }

    strings.push_back( '\0' );
    return offset;
}

bool XMLReader::parse( const char *pContent, const size_t contentLength ) {
    nodes.clear();
    attributes.clear();
    document.clear();
    strings.clear();

    currentIndex = kInvalidIndex;
    parentIndex = 0;

    if ( pContent == nullptr || contentLength == 0 ) {
        return false;
    }

    document.assign( pContent, pContent + contentLength );
    document.push_back( '\0' );

    nodes.push_back( XMLNode() );

    int32_t openIndex = 0;
    char *pIt = document.data();

    while ( true ) {
        char *pTextBegin = pIt;
        while ( *pIt != '\0' && *pIt != '<' ) {
            pIt++;
        }

        if ( openIndex != 0 && nodes[openIndex].DataOffset == kInvalidIndex ) {
            char *pTextEnd = pIt;
            while ( pTextBegin < pTextEnd && IsXMLSpace( *pTextBegin ) ) {
                pTextBegin++;
            }
            while ( pTextEnd > pTextBegin && IsXMLSpace( pTextEnd[-1] ) ) {
                pTextEnd--;
            }

            if ( pTextEnd > pTextBegin ) {
                nodes[openIndex].DataOffset = intern( pTextBegin, pTextEnd );
            }
        }

        pIt = SkipIgnorable( pIt );
        if ( *pIt != '<' ) {
            break;
        }
        pIt++;

        if ( *pIt == '/' ) {
            pIt++;
            while ( *pIt != '\0' && *pIt != '>' ) {
                pIt++;
            }
            if ( *pIt == '>' ) {
                pIt++;
            }

            if ( openIndex == 0 ) {
                OTDU_LOG_WARN( "Malformed XML: unbalanced closing tag\n" );
                return false;
            }
            openIndex = nodes[openIndex].Parent;
            continue;
        }

        // Opening tag.
        char *pNameBegin = pIt;
        while ( IsNameChar( *pIt ) ) {
            pIt++;
        }
        if ( pIt == pNameBegin ) {
            OTDU_LOG_WARN( "Malformed XML: empty element name\n" );
            return false;
        }

        char *pNameEnd = pIt;

        XMLNode node;
        node.Parent = openIndex;
        node.FirstAttribute = (int32_t)attributes.size();

        // Attributes.
        bool bSelfClosing = false;
        while ( *pIt != '\0' ) {
            while ( IsXMLSpace( *pIt ) ) {
                pIt++;
            }

            if ( *pIt == '/' ) {
                bSelfClosing = true;
                pIt++;
                continue;
            }

            if ( *pIt == '>' || *pIt == '\0' ) {
                break;
            }

            char *pAttrBegin = pIt;
            while ( IsNameChar( *pIt ) ) {
                pIt++;
            }
            if ( pIt == pAttrBegin ) {
                OTDU_LOG_WARN( "Malformed XML: bad attribute near '%.16s'\n",
                               pAttrBegin );
                return false;
            }
            char *pAttrEnd = pIt;

            while ( IsXMLSpace( *pIt ) ) {
                pIt++;
            }
            if ( *pIt != '=' ) {
                OTDU_LOG_WARN(
                    "Malformed XML: attribute '%.16s' has no value\n",
                    pAttrBegin );
                return false;
            }
            pIt++;
            while ( IsXMLSpace( *pIt ) ) {
                pIt++;
            }

            const char quote = *pIt;
            if ( quote != '"' && quote != '\'' ) {
                OTDU_LOG_WARN( "Malformed XML: unquoted attribute value\n" );
                return false;
            }
            pIt++;

            char *pValueBegin = pIt;
            while ( *pIt != '\0' && *pIt != quote ) {
                pIt++;
            }
            if ( *pIt != quote ) {
                OTDU_LOG_WARN(
                    "Malformed XML: unterminated attribute value\n" );
                return false;
            }
            char *pValueEnd = pIt;
            pIt++;

            XMLAttribute attribute;
            attribute.NameOffset = intern( pAttrBegin, pAttrEnd );
            attribute.ValueOffset = intern( pValueBegin, pValueEnd );
            attributes.push_back( attribute );
            node.NumAttributes++;
        }

        if ( *pIt != '>' ) {
            OTDU_LOG_WARN( "Malformed XML: unterminated tag\n" );
            return false;
        }
        pIt++;

        node.NameOffset = intern( pNameBegin, pNameEnd );

        const int32_t nodeIndex = (int32_t)nodes.size();
        nodes.push_back( node );

        XMLNode &parent = nodes[openIndex];
        if ( parent.FirstChild == kInvalidIndex ) {
            parent.FirstChild = nodeIndex;
        } else {
            nodes[parent.LastChild].NextSibling = nodeIndex;
        }
        parent.LastChild = nodeIndex;

        if ( !bSelfClosing ) {
            openIndex = nodeIndex;
        }
    }

    if ( openIndex != 0 ) {
        OTDU_LOG_WARN( "Malformed XML: '%s' is never closed\n",
                       stringAt( nodes[openIndex].NameOffset ) );
        return false;
    }

    return true;
}

void XMLReader::resetPos() {
    currentIndex = kInvalidIndex;
    parentIndex = 0;
}

bool XMLReader::findElem( const char *pName ) {
    if ( nodes.empty() || pName == nullptr ) {
        return false;
    }

    int32_t candidate = ( currentIndex == kInvalidIndex )
                            ? nodes[parentIndex].FirstChild
                            : nodes[currentIndex].NextSibling;

    while ( candidate != kInvalidIndex ) {
        const char *pCandidateName = stringAt( nodes[candidate].NameOffset );
        if ( pCandidateName != nullptr &&
             StringCmpI( pCandidateName, pName ) == 0 ) {
            currentIndex = candidate;
            return true;
        }
        candidate = nodes[candidate].NextSibling;
    }

    return false;
}

bool XMLReader::intoElem() {
    if ( currentNode() == nullptr ) {
        return false;
    }

    parentIndex = currentIndex;
    currentIndex = kInvalidIndex;
    return true;
}

bool XMLReader::outOfElem() {
    if ( parentIndex == 0 ) {
        return false;
    }

    currentIndex = parentIndex;
    parentIndex = nodes[parentIndex].Parent;
    return true;
}

const char *XMLReader::getElemName() const {
    const XMLNode *pNode = currentNode();
    return ( pNode != nullptr ) ? stringAt( pNode->NameOffset ) : nullptr;
}

const char *XMLReader::getAttrib( const char *pName ) const {
    const XMLNode *pNode = currentNode();
    if ( pNode == nullptr || pName == nullptr ) {
        return nullptr;
    }

    for ( int32_t i = 0; i < pNode->NumAttributes; i++ ) {
        const XMLAttribute &attribute = attributes[pNode->FirstAttribute + i];
        const char *pAttributeName = stringAt( attribute.NameOffset );
        if ( pAttributeName != nullptr &&
             StringCmpI( pAttributeName, pName ) == 0 ) {
            return stringAt( attribute.ValueOffset );
        }
    }

    return nullptr;
}

const char *XMLReader::getData() const {
    const XMLNode *pNode = currentNode();
    if ( pNode == nullptr ) {
        return nullptr;
    }

    const char *pData = stringAt( pNode->DataOffset );
    return ( pData != nullptr ) ? pData : "";
}
