#pragma once

class FlashPlayer;
struct MapSpotRecord;

// Free-roam cursor of the challenge map (GMLobby, GMClub).
class MapCursor {
public:
    MapCursor();

    void update( FlashPlayer *pPlayer, const float deltaTime );
    void zoomIn( FlashPlayer *pPlayer );
    void zoomOut( FlashPlayer *pPlayer );

private:
    void enterFreeMode();
    void leaveFreeMode( MapSpotRecord *pRecord );

private:
    float cursorX;
    float cursorY;
    float zoomCursorX;
    float zoomCursorY;
    Eigen::Vector4f zoomOutCursorPosition;
};
