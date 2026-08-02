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

struct PoliceStateJob {};
