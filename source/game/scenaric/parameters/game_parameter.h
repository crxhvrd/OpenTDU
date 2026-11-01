#pragma once

class GameParameterList;

// Parameter identifiers are big endian four character codes ("gmth", ...).
constexpr uint32_t MakeParameterID( const char ( &pID )[5] ) {
    return ( uint32_t( uint8_t( pID[0] ) ) << 24 ) |
           ( uint32_t( uint8_t( pID[1] ) ) << 16 ) |
           ( uint32_t( uint8_t( pID[2] ) ) << 8 ) |
           uint32_t( uint8_t( pID[3] ) );
}

// Flash frame label of a parameter widget (DAT_00f94300, 32 bytes per entry).
struct GameParameterTypeDesc {
    char pLabel[32];
};

enum eGameParameterType : int32_t {
    GPT_Text = 0x0,
    GPT_Time = 0x1,
    GPT_Level = 0x2,
    GPT_Weather = 0x8,
    GPT_Bool = 0xd,
    GPT_BoolOn = 0xe,
    GPT_Choice = 0xf,
    GPT_LevelFree = 0x10,
    GPT_Brand = 0x11,
    GPT_Speed = 0x12,
};

// Choice hashcodes driving the vehicle restriction parameters.
static constexpr uint64_t kGameParameterChoiceBrand = 0x583925;
static constexpr uint64_t kGameParameterChoiceModel = 0x59bfc5;
static constexpr uint64_t kGameParameterChoiceCategory = 0x5b4665;
static constexpr uint64_t kGameParameterChoiceRange = 0x5cf415;

extern bool gbGameParameterOwnedCarsOnly; // DAT_011ee7e4

extern const uint64_t kGameParameterEmptyOptions[1];        // DAT_00f94158
extern const uint64_t kGameParameterLevelOptions[7];        // DAT_00f94160
extern const uint64_t kGameParameterRestrictionOptions[5];  // DAT_00f94198
extern const uint64_t kGameParameterVehicleTypeOptions[3];  // DAT_00f941c0
extern const uint64_t kGameParameterCarCategoryOptions[7];  // DAT_00f941d8
extern const uint64_t kGameParameterBikeCategoryOptions[2]; // DAT_00f94210
extern const uint64_t kGameParameterDrivingAidOptions[4];   // DAT_00f942c8

class GameParameter {
public:
    inline uint32_t getID() const { return id; }
    inline int32_t getTypeIndex() const { return typeIndex; }
    inline int32_t getFormat() const { return format; }
    inline int32_t getMin() const { return min; }
    inline int32_t getMax() const { return max; }
    inline int32_t getCategory() const { return category; }
    inline uint32_t getGameTypeMask() const { return gameTypeMask; }
    inline uint32_t getField68() const { return field68; }
    inline bool isEnabled() const { return bEnabled; }
    inline bool isTrackingParent() const { return bTracksParent; }
    inline int32_t getFileFlags() const { return fileFlags; }
    inline uint64_t getDescriptionHashcode() const {
        return uint64_t( descriptionHashcode ) |
               ( uint64_t( uint32_t( field5c ) ) << 32 );
    }
    inline void setTracksParent( const bool bTracks ) {
        bTracksParent = bTracks;
    }
    inline GameParameter *getParent() const { return pParent; }
    inline GameParameter *getNext() const { return pNext; }
    inline GameParameter *getDependent( const int32_t index ) const {
        return pDependents[index];
    }

    inline void linkParent( GameParameter *pNewParent ) {
        // FUN_007869b0 (inlined)
        if ( pNewParent->numDependents >= kMaxNumDependents ) {
            return;
        }
        const bool bHadParent = pParent != nullptr;
        pParent = pNewParent;
        if ( !bHadParent ) {
            pNewParent->pDependents[pNewParent->numDependents++] = this;
        }
    }

public:
    GameParameter();

    const char *onClick( int32_t *pOutCheckBoxState );

    virtual const char *getDisplayText();
    virtual const char *increment( int32_t param_2 );
    virtual const char *decrement( int32_t param_2 );
    virtual const GameParameterTypeDesc *getTypeDesc();
    virtual uint32_t getSize();
    virtual bool serialize( void *pOutData );
    virtual bool deserialize( const void *pData );
    virtual void setValue( const int32_t newValue, const bool bNotify );
    virtual void setRange( const int32_t newMin,
                           const int32_t newMax,
                           const int32_t newValue );
    virtual void setMinMax( const int32_t newMin, const int32_t newMax );
    virtual void setup( const int32_t newMin,
                        const int32_t newMax,
                        const int32_t newValue,
                        const int32_t newStep,
                        const bool bNewClamp );
    virtual intptr_t getValue();
    virtual bool refresh();
    virtual bool onChildChanged( GameParameter *pChild );
    virtual bool registerTo( GameParameterList *pList );

protected:
    static constexpr int32_t kMaxNumDependents = 6;

protected:
    inline void refreshDependents() {
        for ( int32_t i = 0; i < numDependents; i++ ) {
            if ( pDependents[i] != nullptr ) {
                pDependents[i]->refresh();
            }
        }
    }

    inline int32_t getValueInt() { return int32_t( getValue() ); }

protected:
    friend class GameParameterList;

    int32_t value;
    int32_t min;
    int32_t max;
    bool bClamp;
    int32_t step;
    int32_t typeIndex;
    bool bEnabled;
    bool bTracksParent;
    int32_t fileFlags;
    GameParameter *pParent;
    GameParameter *pDependents[kMaxNumDependents];
    int32_t numDependents;
    uint32_t dependencyFlags;
    int32_t category;
    uint32_t id;
    uint32_t descriptionHashcode;
    int32_t field5c;
    uint32_t gameTypeMask;
    int32_t format;
    uint32_t field68;
    GameParameter *pNext;
};

class GameParameterList {
public:
    inline GameParameter *getHead() const { return pHead; }
    inline int32_t getCount() const { return count; }

    inline void add( GameParameter *pParameter,
                     const uint32_t parameterID,
                     const int32_t format,
                     const int32_t category,
                     const uint32_t descriptionHashcode,
                     const int32_t field5c,
                     const uint32_t gameTypeMask,
                     const uint32_t field68 ) {
        // FUN_007869b0 (inlined)
        if ( pHead == nullptr ) {
            pHead = pParameter;
        } else {
            pTail->pNext = pParameter;
        }
        pTail = pParameter;
        pParameter->id = parameterID;
        pParameter->format = format;
        pParameter->category = category;
        pParameter->descriptionHashcode = descriptionHashcode;
        pParameter->field5c = field5c;
        pParameter->gameTypeMask = gameTypeMask;
        pParameter->field68 = field68;
        pParameter->registerTo( this );
        count++;
    }

public:
    GameParameterList();

    void deserialize( const uint8_t **ppCursor, uint32_t *pVersion );
    int32_t serialize( uint8_t **ppCursor );

private:
    GameParameter *pHead;
    GameParameter *pTail;
    int32_t count;
};

// DAT_012147b0
extern int32_t gIGEFileVersion;

// The IGE file version.
class GameParameterVersion : public GameParameter {
public:
    bool serialize( void *pOutData ) override;
    bool deserialize( const void *pData ) override;
};

class GameParameterString : public GameParameter {
public:
    inline const char *getString() const { return pText; }
    inline char *getStringBuffer() { return pText; }

public:
    static constexpr int32_t kMaxLength = 0x40;

public:
    GameParameterString();

    uint32_t getSize() override;
    bool serialize( void *pOutData ) override;
    bool deserialize( const void *pData ) override;
    intptr_t getValue() override;

private:
    char pText[kMaxLength + 1];
};

class GameParameterNumber : public GameParameter {
public:
    inline void setMoney( const bool bMoney ) { bIsMoney = bMoney; }

public:
    GameParameterNumber();

    const char *getDisplayText() override;

protected:
    char pText[0x20];
    bool bIsMoney;
};

// Number of teams.
class GameParameterTeamCount : public GameParameterNumber {
public:
    const char *increment( int32_t param_2 ) override;
    const char *decrement( int32_t param_2 ) override;
};

class GameParameterBool : public GameParameter {
public:
    GameParameterBool();

    const GameParameterTypeDesc *getTypeDesc() override;
};

class GameParameterLevel : public GameParameter {
public:
    GameParameterLevel();

    const GameParameterTypeDesc *getTypeDesc() override;
};

class GameParameterVector : public GameParameter {
public:
    inline const Eigen::Vector4f &getVector() const { return vector; }

public:
    GameParameterVector();

    uint32_t getSize() override;
    bool serialize( void *pOutData ) override;
    bool deserialize( const void *pData ) override;

private:
    Eigen::Vector4f vector;
};

// Duration in milliseconds.
class GameParameterTime : public GameParameter {
public:
    GameParameterTime();

    const char *getDisplayText() override;
    bool refresh() override;
    bool onChildChanged( GameParameter *pChild ) override;

private:
    char pText[0x20];
};

// Speed in hundredths of km/h.
class GameParameterSpeed : public GameParameter {
public:
    GameParameterSpeed();

    const char *getDisplayText() override;
    bool refresh() override;
    bool onChildChanged( GameParameter *pChild ) override;

private:
    char pText[0x20];
};

class GameParameterLaps : public GameParameter {
public:
    GameParameterLaps();

    const char *getDisplayText() override;
    virtual const char *decrementLaps();

private:
    char pText[0x20];
};

// A value picked among a table of database hashcodes.
class GameParameterChoice : public GameParameter {
public:
    inline uint64_t getChoiceHashcode() const { return choiceHashcode; }
    inline void setChoiceHashcode( const uint64_t hashcode ) {
        choiceHashcode = hashcode;
    }
    inline const uint64_t *getOptions() const { return pOptions; }
    inline void setDatabaseIndex( const int32_t index ) {
        databaseIndex = index;
    }

    inline uint64_t getSelectedOption() const {
        // FUN_00785340 (inlined)
        if ( pOptions == nullptr || value < min || value > max ) {
            return 0ull;
        }
        return pOptions[value];
    }

public:
    GameParameterChoice();

    const char *getDisplayText() override;
    bool refresh() override;
    bool onChildChanged( GameParameter *pChild ) override;
    virtual bool setupOptions( const int32_t newMin,
                               const int32_t newMax,
                               const int32_t newValue,
                               const uint64_t *pNewOptions );

protected:
    int32_t getParentVehicleType() const;

protected:
    // Option of the parent choice that enables this one.
    uint64_t choiceHashcode;
    const uint64_t *pOptions;
    int32_t databaseIndex;
};

// A string of a database table, saved by hashcode.
class GameParameterDBString : public GameParameter {
public:
    GameParameterDBString();

    const char *getDisplayText() override;
    uint32_t getSize() override;
    bool serialize( void *pOutData ) override;
    bool deserialize( const void *pData ) override;
    virtual const char *incrementBy( const int32_t amount );
    virtual const char *decrementBy( const int32_t amount );
    virtual void setupDatabase( const int32_t newMin,
                                const int32_t newMax,
                                const int32_t newValue,
                                const int32_t newDatabaseIndex );

private:
    uint64_t hashcode;
    int32_t databaseIndex;
};

class GameParameterBrand : public GameParameterChoice {
public:
    inline uint64_t getBrandHashcode() const { return brandHashcode; }

public:
    GameParameterBrand();

    const char *getDisplayText() override;
    const char *increment( int32_t param_2 ) override;
    const char *decrement( int32_t param_2 ) override;
    uint32_t getSize() override;
    bool serialize( void *pOutData ) override;
    bool deserialize( const void *pData ) override;
    bool refresh() override;

private:
    const char *pBrandName;
    uint64_t brandHashcode;
};

class GameParameterModel : public GameParameterChoice {
public:
    GameParameterModel();

    const char *getDisplayText() override;
    const char *increment( int32_t param_2 ) override;
    const char *decrement( int32_t param_2 ) override;
    bool deserialize( const void *pData ) override;
    bool refresh() override;

private:
    char pModelName[0x80];
    uint64_t brandHashcode;
    int32_t carIndex;
    int32_t brandCarIndex;
};

// Vehicle restriction (none, brand, model, category or category range) and the
// parameters of each restriction kind.
class GameParameterVehicleRestriction : public GameParameterChoice {
public:
    inline void setFixedChildIDs( const bool bFixed ) {
        bFixedChildIDs = bFixed;
    }
    inline GameParameterBrand &getBrand() { return brand; }
    inline GameParameterBrand &getModelBrand() { return modelBrand; }
    inline GameParameterModel &getModel() { return model; }
    inline GameParameterChoice &getCategory() { return categoryChoice; }
    inline GameParameterChoice &getCategoryFrom() { return categoryFrom; }
    inline GameParameterChoice &getCategoryTo() { return categoryTo; }

public:
    GameParameterVehicleRestriction();

    bool registerTo( GameParameterList *pList ) override;
    bool setupOptions( const int32_t newMin,
                       const int32_t newMax,
                       const int32_t newValue,
                       const uint64_t *pNewOptions ) override;

private:
    GameParameterBrand brand;
    GameParameterBrand modelBrand;
    GameParameterModel model;
    GameParameterChoice categoryChoice;
    GameParameterChoice categoryFrom;
    GameParameterChoice categoryTo;
    bool bFixedChildIDs;
};
