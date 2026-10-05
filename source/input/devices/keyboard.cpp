
#include "shared.h"
#include "keyboard.h"

#include <SDL3/SDL.h>

// DAT_00fa1988
static uint8_t DAT_00fa1988[3][256] = {
    { // Default
        0x00, 0x1B, '&', 0xE9,
        0x00,'\'', '(', '-',
        0xE8, 0x00, 0xE7, 0xE0,
        ')', '=','\b','\t',
        'a', 'z', 'e', 'r',
        't', 'y', 'u', 'i',
        'o', 'p', 0x00, '$',
        '\r', 0x00, 'q', 's',
        'd', 'f', 'g', 'h',
        'j', 'k', 'l', 'm',
        0xF9, 0xB2, 0x00, '*',
        'w', 'x', 'c', 'v',
        'b', 'n', ',', ';',
        ':', '!', 0x00, '*',
        0x00, ' ', 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, '-', 0x00,
        0x00, 0x00, '+', 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, '<', 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        '\t', 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
    },
    { // Shift Mod
        0x00, 0x1B, '1', '2',
        '3', '4', '5', '6',
        '7', '8', '9', '0',
        0xB0, '+','\b','\t',
        'A', 'Z', 'E', 'R',
        'T', 'Y', 'U', 'I',
        'O', 'P', 0x00, 0xA3,
        '\r', 0x00, 'Q', 'S',
        'D', 'F', 'G', 'H',
        'J', 'K', 'L', 'M',
        '%', 0x00, 0x00, 0xB5,
        'W', 'X', 'C', 'V',
        'B', 'N', '?', '.',
        '/', 0xA7, 0x00, '*',
        0x00, ' ', 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, '-', 0x00,
        0x00, 0x00, '+', 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, '>', 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        '\t', 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
    },
    { // Alt Mod?
        0x00, 0x00, 0x00, 0x00,
        '#', '{', '[', '|',
        0x00,'\\', '^', '@',
        ']', '}', 0x00, 0x00,
        0x00, 0x00, 0x80, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0xA4,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
    }
};

static void InitVirtualKeysLUT()
{
    // FUN_00437010
    for (uint32_t uCode = 0; uCode < 0x100; uCode++) {
        SDL_Scancode scanCode = (SDL_Scancode)uCode;

        uint32_t uVirtKey = SDL_GetKeyFromScancode(scanCode, SDL_KMOD_NONE, false);
        uint32_t uVirtKeyShift = SDL_GetKeyFromScancode(scanCode, SDL_KMOD_SHIFT, false);
        uint32_t uVirtKeyAlt = SDL_GetKeyFromScancode(scanCode, SDL_KMOD_ALT, false);

        DAT_00fa1988[0][uCode] = uVirtKey < 0x100u ? (char)uVirtKey : 0;
        DAT_00fa1988[1][uCode] = uVirtKeyShift < 0x100u ? (char)uVirtKeyShift : 0;
        DAT_00fa1988[2][uCode] = uVirtKeyAlt < 0x100u ? (char)uVirtKeyAlt : 0;
    }
}

// DAT_00fa1c88 (indexed by DirectInput scancode)
static constexpr const char *kDIKChannelNames[256] = {
    "Unamed", "ESC", "1", "2",
    "3", "4", "5", "6",
    "7", "8", "9", "0",
    "CLOSE_BRACKET", "EQUAL", "BACKSPACE", "TAB",
    "A", "Z", "E", "R",
    "T", "Y", "U", "I",
    "O", "P", "TREMA", "DOLLAR",
    "ENTER", "LCTRL", "Q", "S",
    "D", "F", "G", "H",
    "J", "K", "L", "M",
    "PERCENT", "SQUARE", "LSHIFT", "STAR",
    "W", "X", "C", "V",
    "B", "N", "COMMA", "SEMI_COLON",
    "COLON", "EXCLAMATION", "RSHIFT", "PAD_MUL",
    "LALT", "SPACE", "CAPS_LOCK", "F1",
    "F2", "F3", "F4", "F5",
    "F6", "F7", "F8", "F9",
    "F10", "LOCK_NUM", "SCROLL_LOCK", "PAD_7",
    "PAD_8", "PAD_9", "PAD_MINUS", "PAD_4",
    "PAD_5", "PAD_6", "PAD_PLUS", "PAD_1",
    "PAD_2", "PAD_3", "PAD_0", "PAD_DOT",
    "unamed", "unamed", "LOWER", "F11",
    "F12", "unamed", "unamed", "unamed",
    "unamed", "unamed", "unamed", "unamed",
    "unamed", "unamed", "unamed", "unamed",
    "unamed", "unamed", "unamed", "unamed",
    "unamed", "unamed", "unamed", "unamed",
    "unamed", "unamed", "unamed", "unamed",
    "unamed", "unamed", "unamed", "unamed",
    "unamed", "unamed", "unamed", "unamed",
    "unamed", "unamed", "unamed", "unamed",
    "unamed", "unamed", "unamed", "unamed",
    "unamed", "unamed", "unamed", "unamed",
    "unamed", "unamed", "unamed", "unamed",
    "unamed", "unamed", "unamed", "unamed",
    "unamed", "unamed", "unamed", "unamed",
    "PREV_TRACK", "unamed", "unamed", "unamed",
    "unamed", "unamed", "unamed", "unamed",
    "unamed", "NEXT_TRACK", "unamed", "unamed",
    "PAD_ENTER", "RCTRL", "unamed", "unamed",
    "MUTE", "CALC", "PLAY_PAUSE", "unamed",
    "MEDIA_STOP", "unamed", "unamed", "unamed",
    "unamed", "unamed", "unamed", "unamed",
    "unamed", "unamed", "VOL_MINUS", "unamed",
    "VOL_PLUS", "unamed", "WEB_HOME", "unamed",
    "unamed", "PAD_DIV", "unamed", "PRINT_SCRN",
    "RALT", "unamed", "unamed", "unamed",
    "unamed", "unamed", "unamed", "unamed",
    "unamed", "unamed", "unamed", "unamed",
    "unamed", "PAUSE", "unamed", "HOME",
    "UP", "PAGE_UP", "unamed", "LEFT",
    "unamed", "RIGHT", "unamed", "END",
    "DOWN", "PAGE_DOWN", "INS", "SUP",
    "unamed", "unamed", "unamed", "unamed",
    "unamed", "unamed", "unamed", "ROS",
    "ROS", "APPLICATION", "POWER", "SLEEP",
    "unamed", "unamed", "unamed", "WAKE",
    "unamed", "SEARCH", "FAVORITES", "REFRESH",
    "WEB_STOP", "FORWARD", "BACK", "COMPUTER",
    "MAIL", "MEDIA", "unamed", "unamed",
    "unamed", "unamed", "unamed", "unamed",
    "unamed", "unamed", "unamed", "unamed",
    "unamed", "unamed", "unamed", "unamed",
    "unamed", "unamed", "unamed", "unamed",
};

struct ScancodeToDIK {
    SDL_Scancode Scancode;
    uint8_t DIK;
};

// Windows set-1 scancodes (third_party/SDL/src/events/scancodes_windows.h);
// extended keys are 0x80 | code, PAUSE is DIK_PAUSE (0xc5).
static constexpr ScancodeToDIK kScancodeToDIK[] = {
    { SDL_SCANCODE_ESCAPE, 0x01 },
    { SDL_SCANCODE_1, 0x02 },
    { SDL_SCANCODE_2, 0x03 },
    { SDL_SCANCODE_3, 0x04 },
    { SDL_SCANCODE_4, 0x05 },
    { SDL_SCANCODE_5, 0x06 },
    { SDL_SCANCODE_6, 0x07 },
    { SDL_SCANCODE_7, 0x08 },
    { SDL_SCANCODE_8, 0x09 },
    { SDL_SCANCODE_9, 0x0a },
    { SDL_SCANCODE_0, 0x0b },
    { SDL_SCANCODE_MINUS, 0x0c },
    { SDL_SCANCODE_EQUALS, 0x0d },
    { SDL_SCANCODE_BACKSPACE, 0x0e },
    { SDL_SCANCODE_TAB, 0x0f },
    { SDL_SCANCODE_Q, 0x10 },
    { SDL_SCANCODE_W, 0x11 },
    { SDL_SCANCODE_E, 0x12 },
    { SDL_SCANCODE_R, 0x13 },
    { SDL_SCANCODE_T, 0x14 },
    { SDL_SCANCODE_Y, 0x15 },
    { SDL_SCANCODE_U, 0x16 },
    { SDL_SCANCODE_I, 0x17 },
    { SDL_SCANCODE_O, 0x18 },
    { SDL_SCANCODE_P, 0x19 },
    { SDL_SCANCODE_LEFTBRACKET, 0x1a },
    { SDL_SCANCODE_RIGHTBRACKET, 0x1b },
    { SDL_SCANCODE_RETURN, 0x1c },
    { SDL_SCANCODE_LCTRL, 0x1d },
    { SDL_SCANCODE_A, 0x1e },
    { SDL_SCANCODE_S, 0x1f },
    { SDL_SCANCODE_D, 0x20 },
    { SDL_SCANCODE_F, 0x21 },
    { SDL_SCANCODE_G, 0x22 },
    { SDL_SCANCODE_H, 0x23 },
    { SDL_SCANCODE_J, 0x24 },
    { SDL_SCANCODE_K, 0x25 },
    { SDL_SCANCODE_L, 0x26 },
    { SDL_SCANCODE_SEMICOLON, 0x27 },
    { SDL_SCANCODE_APOSTROPHE, 0x28 },
    { SDL_SCANCODE_GRAVE, 0x29 },
    { SDL_SCANCODE_LSHIFT, 0x2a },
    { SDL_SCANCODE_BACKSLASH, 0x2b },
    { SDL_SCANCODE_Z, 0x2c },
    { SDL_SCANCODE_X, 0x2d },
    { SDL_SCANCODE_C, 0x2e },
    { SDL_SCANCODE_V, 0x2f },
    { SDL_SCANCODE_B, 0x30 },
    { SDL_SCANCODE_N, 0x31 },
    { SDL_SCANCODE_M, 0x32 },
    { SDL_SCANCODE_COMMA, 0x33 },
    { SDL_SCANCODE_PERIOD, 0x34 },
    { SDL_SCANCODE_SLASH, 0x35 },
    { SDL_SCANCODE_RSHIFT, 0x36 },
    { SDL_SCANCODE_KP_MULTIPLY, 0x37 },
    { SDL_SCANCODE_LALT, 0x38 },
    { SDL_SCANCODE_SPACE, 0x39 },
    { SDL_SCANCODE_CAPSLOCK, 0x3a },
    { SDL_SCANCODE_F1, 0x3b },
    { SDL_SCANCODE_F2, 0x3c },
    { SDL_SCANCODE_F3, 0x3d },
    { SDL_SCANCODE_F4, 0x3e },
    { SDL_SCANCODE_F5, 0x3f },
    { SDL_SCANCODE_F6, 0x40 },
    { SDL_SCANCODE_F7, 0x41 },
    { SDL_SCANCODE_F8, 0x42 },
    { SDL_SCANCODE_F9, 0x43 },
    { SDL_SCANCODE_F10, 0x44 },
    { SDL_SCANCODE_NUMLOCKCLEAR, 0x45 },
    { SDL_SCANCODE_SCROLLLOCK, 0x46 },
    { SDL_SCANCODE_KP_7, 0x47 },
    { SDL_SCANCODE_KP_8, 0x48 },
    { SDL_SCANCODE_KP_9, 0x49 },
    { SDL_SCANCODE_KP_MINUS, 0x4a },
    { SDL_SCANCODE_KP_4, 0x4b },
    { SDL_SCANCODE_KP_5, 0x4c },
    { SDL_SCANCODE_KP_6, 0x4d },
    { SDL_SCANCODE_KP_PLUS, 0x4e },
    { SDL_SCANCODE_KP_1, 0x4f },
    { SDL_SCANCODE_KP_2, 0x50 },
    { SDL_SCANCODE_KP_3, 0x51 },
    { SDL_SCANCODE_KP_0, 0x52 },
    { SDL_SCANCODE_KP_PERIOD, 0x53 },
    { SDL_SCANCODE_NONUSBACKSLASH, 0x56 },
    { SDL_SCANCODE_F11, 0x57 },
    { SDL_SCANCODE_F12, 0x58 },
    { SDL_SCANCODE_KP_EQUALS, 0x59 },
    { SDL_SCANCODE_INTERNATIONAL6, 0x5c },
    { SDL_SCANCODE_F13, 0x64 },
    { SDL_SCANCODE_F14, 0x65 },
    { SDL_SCANCODE_F15, 0x66 },
    { SDL_SCANCODE_F16, 0x67 },
    { SDL_SCANCODE_F17, 0x68 },
    { SDL_SCANCODE_F18, 0x69 },
    { SDL_SCANCODE_F19, 0x6a },
    { SDL_SCANCODE_F20, 0x6b },
    { SDL_SCANCODE_F21, 0x6c },
    { SDL_SCANCODE_F22, 0x6d },
    { SDL_SCANCODE_F23, 0x6e },
    { SDL_SCANCODE_INTERNATIONAL2, 0x70 },
    { SDL_SCANCODE_LANG2, 0x71 },
    { SDL_SCANCODE_LANG1, 0x72 },
    { SDL_SCANCODE_INTERNATIONAL1, 0x73 },
    { SDL_SCANCODE_F24, 0x76 },
    { SDL_SCANCODE_LANG4, 0x77 },
    { SDL_SCANCODE_LANG3, 0x78 },
    { SDL_SCANCODE_INTERNATIONAL4, 0x79 },
    { SDL_SCANCODE_INTERNATIONAL5, 0x7b },
    { SDL_SCANCODE_INTERNATIONAL3, 0x7d },
    { SDL_SCANCODE_KP_COMMA, 0x7e },
    { SDL_SCANCODE_PASTE, 0x8a },
    { SDL_SCANCODE_MEDIA_PREVIOUS_TRACK, 0x90 },
    { SDL_SCANCODE_CUT, 0x97 },
    { SDL_SCANCODE_COPY, 0x98 },
    { SDL_SCANCODE_MEDIA_NEXT_TRACK, 0x99 },
    { SDL_SCANCODE_KP_ENTER, 0x9c },
    { SDL_SCANCODE_RCTRL, 0x9d },
    { SDL_SCANCODE_MUTE, 0xa0 },
    { SDL_SCANCODE_MEDIA_PLAY_PAUSE, 0xa2 },
    { SDL_SCANCODE_MEDIA_STOP, 0xa4 },
    { SDL_SCANCODE_MEDIA_EJECT, 0xac },
    { SDL_SCANCODE_VOLUMEDOWN, 0xae },
    { SDL_SCANCODE_VOLUMEUP, 0xb0 },
    { SDL_SCANCODE_AC_HOME, 0xb2 },
    { SDL_SCANCODE_KP_DIVIDE, 0xb5 },
    { SDL_SCANCODE_PRINTSCREEN, 0xb7 },
    { SDL_SCANCODE_RALT, 0xb8 },
    { SDL_SCANCODE_HELP, 0xbb },
    { SDL_SCANCODE_PAUSE, 0xc5 },
    { SDL_SCANCODE_HOME, 0xc7 },
    { SDL_SCANCODE_UP, 0xc8 },
    { SDL_SCANCODE_PAGEUP, 0xc9 },
    { SDL_SCANCODE_LEFT, 0xcb },
    { SDL_SCANCODE_RIGHT, 0xcd },
    { SDL_SCANCODE_END, 0xcf },
    { SDL_SCANCODE_DOWN, 0xd0 },
    { SDL_SCANCODE_PAGEDOWN, 0xd1 },
    { SDL_SCANCODE_INSERT, 0xd2 },
    { SDL_SCANCODE_DELETE, 0xd3 },
    { SDL_SCANCODE_LGUI, 0xdb },
    { SDL_SCANCODE_RGUI, 0xdc },
    { SDL_SCANCODE_APPLICATION, 0xdd },
    { SDL_SCANCODE_POWER, 0xde },
    { SDL_SCANCODE_SLEEP, 0xdf },
    { SDL_SCANCODE_AC_SEARCH, 0xe5 },
    { SDL_SCANCODE_AC_BOOKMARKS, 0xe6 },
    { SDL_SCANCODE_AC_REFRESH, 0xe7 },
    { SDL_SCANCODE_AC_STOP, 0xe8 },
    { SDL_SCANCODE_AC_FORWARD, 0xe9 },
    { SDL_SCANCODE_AC_BACK, 0xea },
    { SDL_SCANCODE_MEDIA_SELECT, 0xed },
};

static const char *GetChannelName( const SDL_Scancode scancode )
{
    // Channels are named after the DirectInput key at the same physical
    // position, so DevicesPC.ini and savegame bindings resolve as in the
    // original (whose names follow an AZERTY layout).
    for ( const ScancodeToDIK &entry : kScancodeToDIK ) {
        if ( entry.Scancode == scancode ) {
            return kDIKChannelNames[entry.DIK];
        }
    }
    return kDIKChannelNames[0x54]; // "unamed"
}

DeviceKeyboard::DeviceKeyboard()
    : Device()
{

}

DeviceKeyboard::~DeviceKeyboard()
{

}

bool DeviceKeyboard::initialize() {
    // FUN_004371a0
    if (!SDL_HasKeyboard()) {
        return false;
    }

    InitVirtualKeysLUT();

    numChannelButtons = SDL_Scancode::SDL_SCANCODE_COUNT;
    numChannelAxis = 0;
    unknown = 0;
    channelCount = numChannelButtons + numChannelAxis + unknown;

    InputChannel* iVar1 = allocateChannels(channelCount);
    if (iVar1 == nullptr) {
        return false;
    }

    deviceType = eDeviceType::DT_Keyboard;
    specializedType = eDeviceSpecialization::DS_None;
    setName("Keyboard"); // DAT_00f3ea80
    
    flags = (flags ^ (flags ^ 0x01)) & 1;

    InputChannel* peVar4 = &pChannels[channelCount + -2];
    peVar4->Unknown = 2;
    peVar4->field13_0x54 = 3;

    pScancodeChannel = peVar4;

    for (uint32_t i = 0; i < channelCount; i++) {
        InputChannel& peVar3 = pChannels[i];
        strncpy(peVar3.pName, GetChannelName(SDL_Scancode(i)), sizeof(peVar3.pName) - 1);
        peVar3.pName[sizeof(peVar3.pName) - 1] = '\0';

        if (peVar3.Unknown == 0) {
            peVar3.Unknown4 = 10.0f;
            peVar3.Unknown5 = 10.0f;
        } else {
            peVar3.Unknown4 = 1e+08f;
            peVar3.Unknown5 = 1e+09f;
        }
    }

    flags |= 2;
    setEnabled();

    return true;
}

int32_t DeviceKeyboard::getChannelIndexFromAscii(uint8_t param_1)
{
    // FUN_00437460
    for (int32_t i = 0; i < 3; i++) {
        for (int32_t iVar2 = 0; iVar2 < 256; iVar2++) {
            if (DAT_00fa1988[i][iVar2] == param_1) {
                // NOTE: the original returns iVar2 - 1 (DirectInput channel
                // layout); channels are indexed by SDL scancode here.
                return iVar2;
            }
        }
    }
    return -1;
}

void DeviceKeyboard::setEnabled()
{
    // FUN_00435100
    if ((flags & 2) != 0) {
        flags = flags & 0xfd;
    }
}

void DeviceKeyboard::setDisabled()
{
    // FUN_004350e0
    if ((flags & 2) == 0) {
        flags |= 2;
    }
}

bool DeviceKeyboard::FUN_0042df00()
{
    // FUN_0042df00
    return true;
}

bool DeviceKeyboard::tick()
{
    // FUN_00437290
    if ((flags & 2) != 0) {
        return true;
    }

    int32_t keyCount = 0;
    const bool *pKeyStates = SDL_GetKeyboardState( &keyCount );
    for ( int32_t i = 0; i < keyCount; i++ ) {
        if ( pKeyStates[i] ) {
            pChannels[i].Flags |= 0x10;
        } else {
            // Clear only the pressed bit; bits 5-7 are edge-detection state
            // managed by sampleData and must be preserved across tick().
            pChannels[i].Flags &= ~0x10u;
        }
    }

    return true;
}

edGUID DeviceKeyboard::getInstanceGUID() const {
    // FUN_00435a30
    return kGUIDSysKeyboard;
}

void DeviceKeyboard::getChannelDisplayName( const InputChannel &channel,
                                            char *pOutName,
                                            const size_t outNameLength ) const {
    // FUN_004374d0
    const SDL_Scancode scancode = SDL_Scancode( &channel - pChannels );
    const SDL_Keycode keycode =
        SDL_GetKeyFromScancode( scancode, SDL_KMOD_NONE, false );
    const char *pKeyName = SDL_GetKeyName( keycode );
    if ( pKeyName == nullptr || pKeyName[0] == '\0' ) {
        Device::getChannelDisplayName( channel, pOutName, outNameLength );
        return;
    }

    strncpy( pOutName, pKeyName, outNameLength - 1 );
    pOutName[outNameLength - 1] = '\0';
}
