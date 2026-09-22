#pragma once

struct ParticleInstance;
struct FlashMessage;

class CinematicEntity {
public:
    CinematicEntity();
    virtual ~CinematicEntity();

    virtual int32_t getEntityIndex() = 0;
    virtual bool onMessage( FlashMessage *pMessage );
    virtual void checkValidity() = 0;

    virtual void initializeForCinematic( void *param_2,
                                         void *param_3 ) { /* NOP */ }

    virtual void initializeForCinematicKeyframed( void *param_2,
                                                  void *param_3 ) { /* NOP */ }

    virtual bool isValid() const;

    inline bool wasVisibleBefore() const { return bVisibleBefore; }
    inline bool needRegisterEntity() const { return bNeedRegisterEntity; }
    inline void setNeedRegisterEntity( bool bEnabled ) {
        bNeedRegisterEntity = bEnabled;
    }

protected:
    std::string cutsceneName;
    uint32_t cutsceneIndex;
    ParticleInstance *pParticleInstance;

    uint8_t bVisibleBefore : 1;
    uint8_t bHighPriorityInit : 1;
    uint8_t bNeedRegisterEntity : 1;
};
