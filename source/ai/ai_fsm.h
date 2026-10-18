#pragma once

class AiFsm {
public:
    enum eEvent : int32_t {
        FE_Update = 0,
        FE_Enter = 1,
        FE_Exit = 2,
    };

    static constexpr int32_t kMaxStackSize = 4;
    static constexpr int32_t kMaxChainedTransitions = 10;

public:
    inline int32_t getCurrentState() const { return currentState; }

public:
    AiFsm();
    virtual ~AiFsm() = default;

    void process( const int32_t event );

protected:
    int32_t currentState;
    int32_t nextState;
    bool bTransitionPending;
    int32_t stateStack[kMaxStackSize];
    int32_t stackSize;

protected:
    virtual bool handle( const int32_t event, const int32_t state ) = 0;

    inline void goTo( const int32_t state ) {
        nextState = state;
        bTransitionPending = true;
    }

    inline void push( const int32_t state ) {
        stateStack[stackSize++] = currentState;
        goTo( state );
    }

    inline void pop() {
        nextState = stateStack[--stackSize];
        bTransitionPending = true;
    }
};
