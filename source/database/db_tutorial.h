#pragma once

struct DBEntryTutorial {
    DBReference TextID;    // 'back', 'dpad', 'map ', ...
    DBReference TextIndex; 
    DBReference Libelle;   
    DBReference File;      
    int32_t TimeCent;      
    int32_t __PADDING__;
};
OTDU_SIZE_MUST_MATCH( DBEntryTutorial, 0x28 );
