#pragma once

class AchievementsData;
class Driver;

class IPlayerData {
public:
    virtual ~IPlayerData() {}

    virtual void initialize() = 0;
    virtual void terminate() = 0;
    virtual void tick( const float deltaTime ) = 0;
    virtual void newGame( const bool bResetOnly ) = 0;
    virtual Driver *getDriver() = 0;
    virtual AchievementsData *getAchievements() = 0;
    virtual void clear() = 0;
    virtual bool isReady() const = 0;
};
