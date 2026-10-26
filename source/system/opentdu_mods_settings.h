#pragma once

#include "options_widget_page.h"

// OTDU_ADDITION: the MODS page of the OPENTDU options movie
class OpenTDUModsSettings {
public:
    void open( OptionsWidgetPage &page );

private:
    std::vector<OptionsTab> buildTabs();
    void confirm();
    void cancel();

private:
    // Pack id and its state when the page opened
    std::vector<std::pair<std::string, bool>> openedStates;
};
