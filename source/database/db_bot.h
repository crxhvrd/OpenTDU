#pragma once

struct DBEntryBot {
    DBReference FirstName;
    DBReference Behaviour;
    DBReference PNJ; // DBEntryPNJ
    float Difficulty;
    int32_t ZoneIndex;
    DBReference BotCar; // DBEntryCarPhysicsData
    int32_t ColorIndex;
    float Rarety;
};
OTDU_SIZE_MUST_MATCH( DBEntryBot, 0x30 );
