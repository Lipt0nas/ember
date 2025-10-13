#pragma once

#include "ember.hpp"

struct FrustumPlanes {
    glm::vec4 left;
    glm::vec4 right;
    glm::vec4 bottom;
    glm::vec4 top;
    glm::vec4 near;
    glm::vec4 far;
};

FrustumPlanes extract_frustum_planes(const glm::mat4& view_proj);

struct Camera {
    float near_plane;

    float viewport_width;
    float viewport_height;

    float fov;

    glm::vec3 position;
    glm::vec3 direction;
    glm::vec3 up = {0, 1, 0};

    glm::mat4 projection_matrix;
    glm::mat4 view_matrix;
    glm::mat4 combined_matrix;

    FrustumPlanes planes;

    glm::quat orientation;
};

void update_camera(Camera& camera);
void move_camera(Camera& camera, glm::vec2 direction, float distance);
