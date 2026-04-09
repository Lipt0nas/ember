#pragma once

#include "ember.hpp"

enum class Key {
    UNKNOWN = 0,
    A,
    B,
    C,
    D,
    E,
    F,
    G,
    H,
    I,
    J,
    K,
    L,
    M,
    N,
    O,
    P,
    Q,
    R,
    S,
    T,
    U,
    V,
    W,
    X,
    Y,
    Z,
    NUM_0,
    NUM_1,
    NUM_2,
    NUM_3,
    NUM_4,
    NUM_5,
    NUM_6,
    NUM_7,
    NUM_8,
    NUM_9,
    RETURN,
    ESCAPE,
    BACKSPACE,
    TAB,
    SPACE,
    MINUS,
    EQUALS,
    LEFT_BRACKET,
    RIGHT_BRACKET,
    BACKSLASH,
    SEMICOLON,
    APOSTROPHE,
    GRAVE,
    COMMA,
    PERIOD,
    SLASH,
    CAPSLOCK,
    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,
    PRINT_SCREEN,
    SCROLL_LOCK,
    PAUSE,
    INSERT,
    HOME,
    PAGE_UP,
    DELETE,
    END,
    PAGE_DOWN,
    RIGHT,
    LEFT,
    UP,
    DOWN,
    KEYPAD_DIVIDE,
    KEYPAD_MULTIPLY,
    KEYPAD_MINUS,
    KEYPAD_PLUS,
    KEYPAD_ENTER,
    KEYPAD_0,
    KEYPAD_1,
    KEYPAD_2,
    KEYPAD_3,
    KEYPAD_4,
    KEYPAD_5,
    KEYPAD_6,
    KEYPAD_7,
    KEYPAD_8,
    KEYPAD_9,
    KEYPAD_PERIOD,
    KEYPAD_EQUALS,
    LEFT_CTRL,
    LEFT_SHIFT,
    LEFT_ALT,
    LEFT_SUPER,
    RIGHT_CTRL,
    RIGHT_SHIFT,
    RIGHT_ALT,
    RIGHT_SUPER,
};

enum class Button {
    UNKNOWN = 0,
    LEFT,
    MIDDLE,
    RIGHT
};

enum class GamepadButton {
    UNKNOWN,
    SOUTH,
    EAST,
    WEST,
    NORTH,
    BACK,
    GUIDE,
    START,
    LEFT_STICK,
    RIGHT_STICK,
    LEFT_SHOULDER,
    RIGHT_SHOULDER,
    DPAD_UP,
    DPAD_DOWN,
    DPAD_LEFT,
    DPAD_RIGHT,
};

struct Gamepad {
    glm::vec2 left_axis  = {0.0f, 0.0f};
    glm::vec2 right_axis = {0.0f, 0.0f};

    float left_trigger  = 0.0f;
    float right_trigger = 0.0f;

    std::array<bool, 24> pressed_buttons  = {0};
    std::array<bool, 24> released_buttons = {1};
};

struct KeyUpEvent {
    int key;
};

struct KeyDownEvent {
    int  key;
    bool repeating;
};

class InputSystem {
public:
    void initialize(class World* world);

    void update_key_states();

    glm::vec2 get_mouse_position();
    glm::vec2 get_mouse_delta();

    bool is_key_pressed(Key key);
    bool is_button_pressed(Button button);

    bool is_key_just_pressed(Key key);
    bool is_button_just_pressed(Button button);

    std::string key_to_string(Key key);
    std::string button_to_string(Button button);

    Key    string_to_key(const std::string& str);
    Button string_to_button(const std::string& str);

    void register_key_press(int scancode, bool repeating);
    void register_key_release(int scancode);

    void register_button_press(int scancode);
    void register_button_release(int scancode);

    int get_key_count();
    int get_button_count();

    glm::vec2 mouse_pos;
    glm::vec2 mouse_delta;
    glm::vec2 mouse_delta_accumulator;

    void add_gamepad(uint32_t id);
    void remove_gamepad(uint32_t id);

    Gamepad* get_gamepad_by_id(uint32_t id);
    Gamepad* get_gamepad_by_index(uint32_t index);

    glm::vec2 get_gamepad_left_axis(uint32_t index);
    glm::vec2 get_gamepad_right_axis(uint32_t index);

    float get_gamepad_left_trigger(uint32_t index);
    float get_gamepad_right_trigger(uint32_t index);

    bool is_gamepad_button_pressed(uint32_t index, GamepadButton button);
    bool is_gamepad_button_just_pressed(uint32_t index, GamepadButton button);

    int           get_gamepad_button_count();
    std::string   gamepad_button_to_string(GamepadButton button);
    GamepadButton string_to_gamepad_button(const std::string& str);

    void register_gamepad_button_press(uint32_t id, int scancode);
    void register_gamepad_button_release(uint32_t id, int scancode);

    void register_gamepad_axis(uint32_t id, int axis, float amount);

private:
    std::array<bool, 512> pressed_keys    = {0};
    std::array<bool, 12>  pressed_buttons = {0};

    std::array<bool, 512> released_keys    = {1};
    std::array<bool, 12>  released_buttons = {1};

    std::map<uint32_t, Gamepad> gamepads;

    static const std::unordered_map<int, Key> scancode_to_key;
    static const std::vector<std::string>     key_to_string_map;

    static const std::unordered_map<int, Button> scancode_to_button;
    static const std::vector<std::string>        button_to_string_map;

    static const std::unordered_map<int, GamepadButton> scancode_to_gamepad_button;
    static const std::vector<std::string>               gamepad_button_to_string_map;

    class World* world = nullptr;
};
