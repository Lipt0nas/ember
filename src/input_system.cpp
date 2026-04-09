#include "input_system.hpp"

#include "world.hpp"

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

const std::unordered_map<int, GamepadButton> InputSystem::scancode_to_gamepad_button = {
    {SDL_GAMEPAD_BUTTON_SOUTH, GamepadButton::SOUTH},
    {SDL_GAMEPAD_BUTTON_EAST, GamepadButton::EAST},
    {SDL_GAMEPAD_BUTTON_WEST, GamepadButton::WEST},
    {SDL_GAMEPAD_BUTTON_NORTH, GamepadButton::NORTH},
    {SDL_GAMEPAD_BUTTON_BACK, GamepadButton::BACK},
    {SDL_GAMEPAD_BUTTON_GUIDE, GamepadButton::GUIDE},
    {SDL_GAMEPAD_BUTTON_START, GamepadButton::START},
    {SDL_GAMEPAD_BUTTON_LEFT_STICK, GamepadButton::LEFT_STICK},
    {SDL_GAMEPAD_BUTTON_RIGHT_STICK, GamepadButton::RIGHT_STICK},
    {SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, GamepadButton::LEFT_SHOULDER},
    {SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, GamepadButton::RIGHT_SHOULDER},
    {SDL_GAMEPAD_BUTTON_DPAD_UP, GamepadButton::DPAD_UP},
    {SDL_GAMEPAD_BUTTON_DPAD_DOWN, GamepadButton::DPAD_DOWN},
    {SDL_GAMEPAD_BUTTON_DPAD_LEFT, GamepadButton::DPAD_LEFT},
    {SDL_GAMEPAD_BUTTON_DPAD_RIGHT, GamepadButton::DPAD_RIGHT},
};

const std::vector<std::string> InputSystem::gamepad_button_to_string_map = {
    "Unkown",
    "South",
    "East",
    "West",
    "North",
    "Back",
    "Guide",
    "Start",
    "LeftStick",
    "RightStick",
    "LeftShoulder",
    "RightShoulder",
    "DpadUp",
    "DpadDown",
    "DpadLeft",
    "DpadRight",
};

void InputSystem::initialize(class World* world) {
    this->world = world;
}

void InputSystem::update_key_states() {
    for (int i = 0; i < pressed_keys.size(); i++) {
        released_keys[i] = !pressed_keys[i];
    }

    for (int i = 0; i < pressed_buttons.size(); i++) {
        released_buttons[i] = !pressed_buttons[i];
    }

    for (auto& [_, gamepad] : gamepads) {
        for (int i = 0; i < gamepad.pressed_buttons.size(); i++) {
            gamepad.released_buttons[i] = !gamepad.pressed_buttons[i];
        }
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

void InputSystem::register_key_press(int scancode, bool repeating) {
    Key key = Key::UNKNOWN;

    auto entry = scancode_to_key.find(scancode);
    if (entry != scancode_to_key.end()) {
        key = entry->second;
    }

    world->script.issue_event(
        KeyDownEvent{
            .key       = static_cast<int>(key),
            .repeating = repeating,
        }
    );

    pressed_keys[static_cast<int>(key)] = true;
}

void InputSystem::register_key_release(int scancode) {
    Key key = Key::UNKNOWN;

    auto entry = scancode_to_key.find(scancode);
    if (entry != scancode_to_key.end()) {
        key = entry->second;
    }

    world->script.issue_event(
        KeyUpEvent{
            .key = static_cast<int>(key),
        }
    );

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

void InputSystem::add_gamepad(uint32_t id) {
    gamepads.insert({id, {}});
}

void InputSystem::remove_gamepad(uint32_t id) {
    gamepads.erase(id);
}

Gamepad* InputSystem::get_gamepad_by_id(uint32_t id) {
    auto it = gamepads.find(id);

    return it == gamepads.end() ? nullptr : &it->second;
}

Gamepad* InputSystem::get_gamepad_by_index(uint32_t index) {
    if (gamepads.empty() || index >= gamepads.size()) {
        return nullptr;
    }

    uint32_t idx = 0;
    for (auto& [_, gamepad] : gamepads) {
        if (idx++ == index) {
            return &gamepad;
        }
    }

    return nullptr;
}

glm::vec2 InputSystem::get_gamepad_left_axis(uint32_t index) {
    auto gamepad = get_gamepad_by_index(index);

    return gamepad ? gamepad->left_axis : glm::vec2(0.0f, 0.0f);
}

glm::vec2 InputSystem::get_gamepad_right_axis(uint32_t index) {
    auto gamepad = get_gamepad_by_index(index);

    return gamepad ? gamepad->right_axis : glm::vec2(0.0f, 0.0f);
}

float InputSystem::get_gamepad_left_trigger(uint32_t index) {
    auto gamepad = get_gamepad_by_index(index);

    return gamepad ? gamepad->left_trigger : 0.0f;
}

float InputSystem::get_gamepad_right_trigger(uint32_t index) {
    auto gamepad = get_gamepad_by_index(index);

    return gamepad ? gamepad->right_trigger : 0.0f;
}

int InputSystem::get_gamepad_button_count() {
    return gamepad_button_to_string_map.size();
}

std::string InputSystem::gamepad_button_to_string(GamepadButton button) {
    return gamepad_button_to_string_map[static_cast<int>(button)];
}

GamepadButton InputSystem::string_to_gamepad_button(const std::string& str) {
    for (int i = 0; i < gamepad_button_to_string_map.size(); i++) {
        if (str == gamepad_button_to_string_map[i]) {
            return static_cast<GamepadButton>(i);
        }
    }

    return GamepadButton::UNKNOWN;
}

bool InputSystem::is_gamepad_button_pressed(uint32_t index, GamepadButton button) {
    auto gamepad = get_gamepad_by_index(index);

    return gamepad ? gamepad->pressed_buttons[static_cast<int>(button)] : false;
}

bool InputSystem::is_gamepad_button_just_pressed(uint32_t index, GamepadButton button) {
    auto gamepad = get_gamepad_by_index(index);

    return gamepad ? gamepad->pressed_buttons[static_cast<int>(button)] &&
                         gamepad->released_buttons[static_cast<int>(button)]
                   : false;
}

void InputSystem::register_gamepad_button_press(uint32_t id, int scancode) {
    auto gamepad = get_gamepad_by_id(id);
    if (!gamepad) {
        return;
    }

    GamepadButton button = GamepadButton::UNKNOWN;

    auto entry = scancode_to_gamepad_button.find(scancode);
    if (entry != scancode_to_gamepad_button.end()) {
        button = entry->second;
    }

    gamepad->pressed_buttons[static_cast<int>(button)] = true;
}

void InputSystem::register_gamepad_button_release(uint32_t id, int scancode) {
    auto gamepad = get_gamepad_by_id(id);
    if (!gamepad) {
        return;
    }

    GamepadButton button = GamepadButton::UNKNOWN;

    auto entry = scancode_to_gamepad_button.find(scancode);
    if (entry != scancode_to_gamepad_button.end()) {
        button = entry->second;
    }

    gamepad->pressed_buttons[static_cast<int>(button)]  = false;
    gamepad->released_buttons[static_cast<int>(button)] = true;
}

void InputSystem::register_gamepad_axis(uint32_t id, int axis, float amount) {
    auto gamepad = get_gamepad_by_id(id);
    if (!gamepad) {
        return;
    }

    switch (axis) {
    case SDL_GAMEPAD_AXIS_LEFTX:
        gamepad->left_axis.x = amount;
        break;
    case SDL_GAMEPAD_AXIS_LEFTY:
        gamepad->left_axis.y = amount;
        break;
    case SDL_GAMEPAD_AXIS_RIGHTX:
        gamepad->right_axis.x = amount;
        break;
    case SDL_GAMEPAD_AXIS_RIGHTY:
        gamepad->right_axis.y = amount;
        break;
    case SDL_GAMEPAD_AXIS_LEFT_TRIGGER:
        gamepad->left_trigger = amount;
        break;
    case SDL_GAMEPAD_AXIS_RIGHT_TRIGGER:
        gamepad->right_trigger = amount;
        break;
    default:
        break;
    }
}
