#pragma once

class RandomGen;
struct VehicleBaseResources;

// Traffic vehicle templates
class AiPoolTraffic {
public:
    static constexpr int32_t kMaxTotalTemplates = 0x22;
    static constexpr int32_t kMaxLoadedTemplates = 8;
    static constexpr int32_t kMandatoryCarID = 0x7f0;

    struct Template {
        Template *pNext = nullptr;
        Template *pPrevious = nullptr;
        int32_t NumUsed = 0;
        int32_t NumTotalUsed = 0;
        bool bMandatory = false;
        bool bMarkedForChange = false;
        float Frequency = 0.0f;
        int32_t IDCar = -1;
        int32_t IDTire = 0;
        AiPoolTraffic *pParent = nullptr;
        VehicleBaseResources *pResource = nullptr;

        bool isLoadCandidate() const;
        bool hasLoadFailed() const;
        bool isLoaded() const;
    };

    struct AiTemplatesData {
        int32_t CarID = 0;
        bool bMandatory = false;
        bool bMarkedForChange = false;
        Template *pRelatedTemplate = nullptr;
        int32_t ActionToDo = 0;
    };

    enum eListState : int32_t {
        TLS_Idle = 0,
        TLS_ApplyPending = 1,
        TLS_Loading = 2,
        TLS_Deferred = 3,
    };

public:
    inline void setPaused( const bool bValue ) { bPaused = bValue; }
    inline Template *getFirstLoadedTemplate() const {
        return pFirstTemplateLoaded;
    }
    inline int32_t getNumTotalTemplates() const { return numTotalTemplates; }
    inline int32_t getNumLoadedTemplates() const { return numLoadedTemplates; }
    inline int32_t getNumLoadingTemplates() const {
        return numLoadingTemplates;
    }
    inline const Template &getTemplateAt( const int32_t index ) const {
        return templates[index];
    }

public:
    AiPoolTraffic( RandomGen *pRandomGen );

    void initialize();
    void terminate();
    void refreshFrequencies();
    void tick();
    void unloadAll();

    Template *findLoadedTemplate( const int32_t carID ) const;
    Template *pickTemplateForSpawn( const int32_t zoneIndex,
                                    const bool bSizeFilter );
    VehicleBaseResources *getTemplateResource( const int32_t carID );
    void forceUnloadTemplate( const int32_t carID );
    void markTemplateForChange( const bool bMark,
                                Template *pTemplate,
                                const float delay );

private:
    RandomGen *pRandom;
    float maxFrequency;
    float meanFrequency;
    bool bPaused;
    std::array<Template, kMaxTotalTemplates> templates;
    Template *pFirstTemplateLoaded;
    Template *pFirstTemplateLoading;
    int32_t numTotalTemplates;
    int32_t numLoadableTemplates;
    int32_t numMandatoryTemplates;
    int32_t maxLoadedTemplates;
    int32_t numLoadedTemplates;
    int32_t numLoadingTemplates;
    bool bCanAddTemplates;
    int32_t maxTemplatesToChange;
    int32_t numTemplatesToChange;
    eListState listState;
    std::vector<AiTemplatesData> templatesList;

private:
    Template &getTemplate( int32_t index );
    bool loadTemplate( Template *pTemplate, const bool bAsync );
    void unloadTemplate( Template *pTemplate );
    void updateLoadingTemplates();
    void updateListState();
    void processTemplateAction( const AiTemplatesData &data );
    void diffTemplateList();
    void processRemovals();
    Template *pickRandomTemplateToLoad();

    static void PushFront( Template *pTemplate, Template **ppHead );
    static void Unlink( Template *pTemplate, Template **ppHead );
};

int32_t RandomWeightedIndex( RandomGen &random,
                             const float *pWeights,
                             const int32_t count );
int32_t RandomRange( RandomGen &random, const int32_t min, const int32_t max );
