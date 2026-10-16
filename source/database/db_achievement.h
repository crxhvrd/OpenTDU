#pragma once

struct DBEntryAchievement {
    DBReference Kind;    
    int32_t NumItemsReq;
    int32_t NumPoints;
    DBReference Tile;
    DBReference Description;
    DBReference Text;       
    DBReference FailedExplanation; 
    DBReference Reward;     
    int32_t RewardData;     
    int32_t Flags;          
};
OTDU_SIZE_MUST_MATCH( DBEntryAchievement, 0x40 );
