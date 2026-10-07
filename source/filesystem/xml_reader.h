#pragma once

class XMLReader {
public:
    XMLReader();
    ~XMLReader();

    bool parse( const char *pContent, const size_t contentLength );
    void resetPos();
    bool findElem( const char *pName );
    bool findNextElem();
    bool intoElem();
    bool outOfElem();
    const char *getElemName() const;
    const char *getAttrib( const char *pName ) const;
    const char *getData() const;

private:
    static constexpr int32_t kInvalidIndex = -1;

    struct XMLAttribute {
        int32_t NameOffset = kInvalidIndex;
        int32_t ValueOffset = kInvalidIndex;
    };

    struct XMLNode {
        int32_t NameOffset = kInvalidIndex;
        int32_t DataOffset = kInvalidIndex;
        int32_t FirstAttribute = kInvalidIndex;
        int32_t NumAttributes = 0;
        int32_t Parent = kInvalidIndex;
        int32_t FirstChild = kInvalidIndex;
        int32_t LastChild = kInvalidIndex;
        int32_t NextSibling = kInvalidIndex;
    };

    std::vector<char> document;
    std::vector<char> strings;
    std::vector<XMLNode> nodes;
    std::vector<XMLAttribute> attributes;

    int32_t currentIndex;
    int32_t parentIndex;

private:
    const char *stringAt( const int32_t offset ) const;
    const XMLNode *currentNode() const;
    int32_t intern( const char *pBegin, const char *pEnd );
};
