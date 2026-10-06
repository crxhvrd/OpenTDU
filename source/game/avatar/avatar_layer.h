#pragma once

class Animation;
class EntityAvatar;
struct RenderAnm;

// Animation layer attached to an avatar
class AvatarLayer {
public:
    inline AvatarLayer *getNext() const { return pNext; }
    inline void setNext( AvatarLayer *pLayer ) { pNext = pLayer; }
    inline bool isEnabled() const { return bEnabled; }

public:
    AvatarLayer();
    virtual ~AvatarLayer() {}

    virtual void attach( EntityAvatar *pTargetAvatar );
    virtual void detach();
    virtual void onResourceChange() {}
    virtual void onSkeletonChange() {}
    virtual void onPoseComputed() {}
    virtual void enable();
    virtual void disable();
    virtual void tick( float deltaTime, float totalTime ) {}

protected:
    Animation *getAnimation() const;

protected:
    EntityAvatar *pAvatar;
    AvatarLayer *pNext;
    bool bEnabled;
};

// Body idle pose
class AvatarPoseLayer : public AvatarLayer {
public:
    enum ePose : int32_t {
        AP_Male = 0,
        AP_Female = 1,
        AP_Bike = 2,
        AP_HeadMale = 3,
        AP_HeadFemale = 4,
        AP_Count = 5
    };

    enum eMode : int32_t {
        APM_Body = 0,
        APM_Head = 1,
    };

public:
    inline void setPoseAnim( const ePose pose, RenderAnm *pAnim ) {
        pPoseAnims[pose] = pAnim;
    }
    inline void setMode( const eMode newMode ) { mode = newMode; }

public:
    AvatarPoseLayer();

    void attach( EntityAvatar *pTargetAvatar ) override;
    void detach() override;
    void onResourceChange() override;
    void onSkeletonChange() override;
    void enable() override;
    void disable() override;

    void refresh();

private:
    void play( int32_t pose );

private:
    eMode mode;
    RenderAnm *pPoseAnims[AP_Count];
    int32_t currentPose;
};

// Head/torso turn following the mouse
class AvatarLookAtLayer : public AvatarLayer {
public:
    inline void setAnims( RenderAnm *pHorizontal, RenderAnm *pVertical ) {
        pHorizontalAnim = pHorizontal;
        pVerticalAnim = pVertical;
    }

public:
    AvatarLookAtLayer();

    void attach( EntityAvatar *pTargetAvatar ) override;
    void detach() override;
    void onResourceChange() override;
    void enable() override;
    void disable() override;
    void tick( float deltaTime, float totalTime ) override;

private:
    void apply();

private:
    float blendRatios[6];
    RenderAnm *pHorizontalAnim;
    RenderAnm *pVerticalAnim;
};

// High heels pose
class AvatarHeelLayer : public AvatarLayer {
public:
    inline void setHeelAnim( RenderAnm *pAnim ) { pHeelAnim = pAnim; }

public:
    AvatarHeelLayer();

    void attach( EntityAvatar *pTargetAvatar ) override;
    void detach() override;
    void onResourceChange() override;
    void enable() override;
    void disable() override;

    void refresh();

private:
    RenderAnm *pHeelAnim;
};

// Shows the bike helmet
class AvatarHelmetLayer : public AvatarLayer {
public:
    void detach() override;
    void onPoseComputed() override;
    void tick( float deltaTime, float totalTime ) override;
};
