#pragma once

class PoliceState {
public:
    inline bool isArrested() const { return bArrested; }

public:
    PoliceState();
    ~PoliceState();

    void reset();

private:
    bool bArrested = false;
};

class PoliceStateJob {
public:
    virtual void check( class CarState *param_1, float param_2 ) = 0;
};
