#include "input_system.hpp"

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

bool InputSystem::is_key_pressed(int key) {
    if (key < 0 || key >= pressed_keys.size()) {
        return false;
    }

    return pressed_keys[key];
}

bool InputSystem::is_button_pressed(int button) {
    if (button < 0 || button >= pressed_buttons.size()) {
        return false;
    }

    return pressed_buttons[button];
}

bool InputSystem::is_key_just_pressed(int key) {
    if (key < 0 || key >= pressed_keys.size()) {
        return false;
    }

    return pressed_keys[key] && released_keys[key];
}

bool InputSystem::is_button_just_pressed(int button) {
    if (button < 0 || button >= pressed_buttons.size()) {
        return false;
    }

    return pressed_buttons[button] && released_buttons[button];
}
