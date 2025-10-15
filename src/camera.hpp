#pragma once

#include "ember.hpp"
struct Camera {
    float near_plane;

    float viewport_width;
    float viewport_height;

    float fov;

    glm::vec3 position;
    glm::quat orientation;

    glm::mat4 projection_matrix;
    glm::mat4 view_matrix;
    glm::mat4 combined_matrix;
};

void update_camera(Camera& camera);
void move_camera(Camera& camera, glm::vec2 direction, float distance);

glm::vec4 normalize_plane(glm::vec4 plane);
