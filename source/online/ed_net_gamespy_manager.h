#pragma once

class edNetGamespyManager {
public:
    edNetGamespyManager();

    void denyFriendRequest();
    void acceptFriendRequest();
};

extern edNetGamespyManager *gpEdNetGamespyManager;
