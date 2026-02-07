#pragma once

#include "ember.hpp"

enum class CameraType {
    PERSPECTIVE  = 0,
    ORTHOGRAPHIC = 1
};

struct Camera {
    float near_plane;
    float far_plane;

    float viewport_width;
    float viewport_height;

    float fov;
    float ortho_size;

    CameraType type = CameraType::PERSPECTIVE;

    glm::vec3 position;
    glm::quat orientation;

    glm::mat4 projection_matrix;
    glm::mat4 view_matrix;
    glm::mat4 combined_matrix;
};

void update_camera(Camera& camera);
void move_camera(Camera& camera, glm::vec2 direction, float distance);

glm::vec4 normalize_plane(glm::vec4 plane);
