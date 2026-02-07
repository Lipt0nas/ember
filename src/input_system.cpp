#include "input_system.hpp"

const std::unordered_map<int, Key> InputSystem::scancode_to_key = {
    {SDL_SCANCODE_A, Key::A},
    {SDL_SCANCODE_B, Key::B},
    {SDL_SCANCODE_C, Key::C},
    {SDL_SCANCODE_D, Key::D},
    {SDL_SCANCODE_E, Key::E},
    {SDL_SCANCODE_F, Key::F},
    {SDL_SCANCODE_G, Key::G},
    {SDL_SCANCODE_H, Key::H},
    {SDL_SCANCODE_I, Key::I},
    {SDL_SCANCODE_J, Key::J},
    {SDL_SCANCODE_K, Key::K},
    {SDL_SCANCODE_L, Key::L},
    {SDL_SCANCODE_M, Key::M},
    {SDL_SCANCODE_N, Key::N},
    {SDL_SCANCODE_O, Key::O},
    {SDL_SCANCODE_P, Key::P},
    {SDL_SCANCODE_Q, Key::Q},
    {SDL_SCANCODE_R, Key::R},
    {SDL_SCANCODE_S, Key::S},
    {SDL_SCANCODE_T, Key::T},
    {SDL_SCANCODE_U, Key::U},
    {SDL_SCANCODE_V, Key::V},
    {SDL_SCANCODE_W, Key::W},
    {SDL_SCANCODE_X, Key::X},
    {SDL_SCANCODE_Y, Key::Y},
    {SDL_SCANCODE_Z, Key::Z},

    {SDL_SCANCODE_0, Key::NUM_0},
    {SDL_SCANCODE_1, Key::NUM_1},
    {SDL_SCANCODE_2, Key::NUM_2},
    {SDL_SCANCODE_3, Key::NUM_3},
    {SDL_SCANCODE_4, Key::NUM_4},
    {SDL_SCANCODE_5, Key::NUM_5},
    {SDL_SCANCODE_6, Key::NUM_6},
    {SDL_SCANCODE_7, Key::NUM_7},
    {SDL_SCANCODE_8, Key::NUM_8},
    {SDL_SCANCODE_9, Key::NUM_9},

    {SDL_SCANCODE_RETURN, Key::RETURN},
    {SDL_SCANCODE_ESCAPE, Key::ESCAPE},
    {SDL_SCANCODE_BACKSPACE, Key::BACKSPACE},
    {SDL_SCANCODE_TAB, Key::TAB},
    {SDL_SCANCODE_SPACE, Key::SPACE},
    {SDL_SCANCODE_MINUS, Key::MINUS},
    {SDL_SCANCODE_EQUALS, Key::EQUALS},
    {SDL_SCANCODE_LEFTBRACKET, Key::LEFT_BRACKET},
    {SDL_SCANCODE_RIGHTBRACKET, Key::RIGHT_BRACKET},
    {SDL_SCANCODE_BACKSLASH, Key::BACKSLASH},
    {SDL_SCANCODE_SEMICOLON, Key::SEMICOLON},
    {SDL_SCANCODE_APOSTROPHE, Key::APOSTROPHE},
    {SDL_SCANCODE_GRAVE, Key::GRAVE},
    {SDL_SCANCODE_COMMA, Key::COMMA},
    {SDL_SCANCODE_PERIOD, Key::PERIOD},
    {SDL_SCANCODE_SLASH, Key::SLASH},
    {SDL_SCANCODE_CAPSLOCK, Key::CAPSLOCK},

    {SDL_SCANCODE_F1, Key::F1},
    {SDL_SCANCODE_F2, Key::F2},
    {SDL_SCANCODE_F3, Key::F3},
    {SDL_SCANCODE_F4, Key::F4},
    {SDL_SCANCODE_F5, Key::F5},
    {SDL_SCANCODE_F6, Key::F6},
    {SDL_SCANCODE_F7, Key::F7},
    {SDL_SCANCODE_F8, Key::F8},
    {SDL_SCANCODE_F9, Key::F9},
    {SDL_SCANCODE_F10, Key::F10},
    {SDL_SCANCODE_F11, Key::F11},
    {SDL_SCANCODE_F12, Key::F12},

    {SDL_SCANCODE_PRINTSCREEN, Key::PRINT_SCREEN},
    {SDL_SCANCODE_SCROLLLOCK, Key::SCROLL_LOCK},
    {SDL_SCANCODE_PAUSE, Key::PAUSE},
    {SDL_SCANCODE_INSERT, Key::INSERT},
    {SDL_SCANCODE_HOME, Key::HOME},
    {SDL_SCANCODE_PAGEUP, Key::PAGE_UP},
    {SDL_SCANCODE_DELETE, Key::DELETE},
    {SDL_SCANCODE_END, Key::END},
    {SDL_SCANCODE_PAGEDOWN, Key::PAGE_DOWN},
    {SDL_SCANCODE_RIGHT, Key::RIGHT},
    {SDL_SCANCODE_LEFT, Key::LEFT},
    {SDL_SCANCODE_UP, Key::UP},
    {SDL_SCANCODE_DOWN, Key::DOWN},

    {SDL_SCANCODE_KP_DIVIDE, Key::KEYPAD_DIVIDE},
    {SDL_SCANCODE_KP_MULTIPLY, Key::KEYPAD_MULTIPLY},
    {SDL_SCANCODE_KP_MINUS, Key::KEYPAD_MINUS},
    {SDL_SCANCODE_KP_PLUS, Key::KEYPAD_PLUS},
    {SDL_SCANCODE_KP_ENTER, Key::KEYPAD_ENTER},
    {SDL_SCANCODE_KP_0, Key::KEYPAD_0},
    {SDL_SCANCODE_KP_1, Key::KEYPAD_1},
    {SDL_SCANCODE_KP_2, Key::KEYPAD_2},
    {SDL_SCANCODE_KP_3, Key::KEYPAD_3},
    {SDL_SCANCODE_KP_4, Key::KEYPAD_4},
    {SDL_SCANCODE_KP_5, Key::KEYPAD_5},
    {SDL_SCANCODE_KP_6, Key::KEYPAD_6},
    {SDL_SCANCODE_KP_7, Key::KEYPAD_7},
    {SDL_SCANCODE_KP_8, Key::KEYPAD_8},
    {SDL_SCANCODE_KP_9, Key::KEYPAD_9},
    {SDL_SCANCODE_KP_PERIOD, Key::KEYPAD_PERIOD},
    {SDL_SCANCODE_KP_EQUALS, Key::KEYPAD_EQUALS},

    {SDL_SCANCODE_LCTRL, Key::LEFT_CTRL},
    {SDL_SCANCODE_LSHIFT, Key::LEFT_SHIFT},
    {SDL_SCANCODE_LALT, Key::LEFT_ALT},
    {SDL_SCANCODE_LGUI, Key::LEFT_SUPER},
    {SDL_SCANCODE_RCTRL, Key::RIGHT_CTRL},
    {SDL_SCANCODE_RSHIFT, Key::RIGHT_SHIFT},
    {SDL_SCANCODE_RALT, Key::RIGHT_ALT},
    {SDL_SCANCODE_RGUI, Key::RIGHT_SUPER},
};

const std::vector<std::string> InputSystem::key_to_string_map = {
    "Unkown",
    "A",
    "B",
    "C",
    "D",
    "E",
    "F",
    "G",
    "H",
    "I",
    "J",
    "K",
    "L",
    "M",
    "N",
    "O",
    "P",
    "Q",
    "R",
    "S",
    "T",
    "U",
    "V",
    "W",
    "X",
    "Y",
    "Z",
    "Num0",
    "Num1",
    "Num2",
    "Num3",
    "Num4",
    "Num5",
    "Num6",
    "Num7",
    "Num8",
    "Num9",
    "Return",
    "Escape",
    "Backspace",
    "Tab",
    "Space",
    "Minus",
    "Equals",
    "LeftBracket",
    "RightBracket",
    "Backslash",
    "Semicolon",
    "Apostrophe",
    "Grave",
    "Comma",
    "Period",
    "Slash",
    "Capslock",
    "F1",
    "F2",
    "F3",
    "F4",
    "F5",
    "F6",
    "F7",
    "F8",
    "F9",
    "F10",
    "F11",
    "F12",
    "PrintScreen",
    "ScrollLock",
    "Pause",
    "Insert",
    "Home",
    "PageUp",
    "Delete",
    "End",
    "PageDown",
    "Right",
    "Left",
    "Up",
    "Down",
    "KeypadDivide",
    "KeypadMultiply",
    "KeypadMinus",
    "KeypadPlus",
    "KeypadEnter",
    "Keypad0",
    "Keypad1",
    "Keypad2",
    "Keypad3",
    "Keypad4",
    "Keypad5",
    "Keypad6",
    "Keypad7",
    "Keypad8",
    "Keypad9",
    "KeypadPeriod",
    "KeypadEquals",
    "LeftCtrl",
    "LeftShift",
    "LeftAlt",
    "LeftSuper",
    "RightCtrl",
    "RightShift",
    "RightAlt",
    "RightSuper"
};

const std::unordered_map<int, Button> InputSystem::scancode_to_button = {
    {SDL_BUTTON_LEFT, Button::LEFT},
    {SDL_BUTTON_MIDDLE, Button::MIDDLE},
    {SDL_BUTTON_RIGHT, Button::RIGHT},
};

const std::vector<std::string> InputSystem::button_to_string_map = {
    "Unkown",
    "Left",
    "Middle",
    "Right",
};

void InputSystem::update_key_states() {
    for (int i = 0; i < pressed_keys.size(); i++) {
        released_keys[i] = !pressed_keys[i];
    }

    for (int i = 0; i < pressed_buttons.size(); i++) {
        released_buttons[i] = !pressed_buttons[i];
    }
}

glm::vec2 InputSystem::get_mouse_position() {
    return mouse_pos;
}

glm::vec2 InputSystem::get_mouse_delta() {
    return mouse_delta;
}

bool InputSystem::is_key_pressed(Key key) {
    return pressed_keys[static_cast<int>(key)];
}

bool InputSystem::is_button_pressed(Button button) {
    return pressed_buttons[static_cast<int>(button)];
}

bool InputSystem::is_key_just_pressed(Key key) {
    return pressed_keys[static_cast<int>(key)] && released_keys[static_cast<int>(key)];
}

bool InputSystem::is_button_just_pressed(Button button) {
    return pressed_buttons[static_cast<int>(button)] && released_buttons[static_cast<int>(button)];
}

std::string InputSystem::key_to_string(Key key) {
    return key_to_string_map[static_cast<int>(key)];
}

std::string InputSystem::button_to_string(Button button) {
    return button_to_string_map[static_cast<int>(button)];
}

Key InputSystem::string_to_key(const std::string& str) {
    for (int i = 0; i < key_to_string_map.size(); i++) {
        if (str == key_to_string_map[i]) {
            return static_cast<Key>(i);
        }
    }

    return Key::UNKNOWN;
}

Button InputSystem::string_to_button(const std::string& str) {
    for (int i = 0; i < button_to_string_map.size(); i++) {
        if (str == button_to_string_map[i]) {
            return static_cast<Button>(i);
        }
    }

    return Button::UNKNOWN;
}

void InputSystem::register_key_press(int scancode) {
    Key key = Key::UNKNOWN;

    auto entry = scancode_to_key.find(scancode);
    if (entry != scancode_to_key.end()) {
        key = entry->second;
    }

    pressed_keys[static_cast<int>(key)] = true;
}

void InputSystem::register_key_release(int scancode) {
    Key key = Key::UNKNOWN;

    auto entry = scancode_to_key.find(scancode);
    if (entry != scancode_to_key.end()) {
        key = entry->second;
    }

    pressed_keys[static_cast<int>(key)]  = false;
    released_keys[static_cast<int>(key)] = true;
}

void InputSystem::register_button_press(int scancode) {
    Button button = Button::UNKNOWN;

    auto entry = scancode_to_button.find(scancode);
    if (entry != scancode_to_button.end()) {
        button = entry->second;
    }

    pressed_buttons[static_cast<int>(button)] = true;
}

void InputSystem::register_button_release(int scancode) {
    Button button = Button::UNKNOWN;

    auto entry = scancode_to_button.find(scancode);
    if (entry != scancode_to_button.end()) {
        button = entry->second;
    }

    pressed_buttons[static_cast<int>(button)]  = false;
    released_buttons[static_cast<int>(button)] = true;
}

int InputSystem::get_key_count() {
    return key_to_string_map.size();
}

int InputSystem::get_button_count() {
    return button_to_string_map.size();
}
