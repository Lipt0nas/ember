#include "camera.hpp"

void update_camera(Camera& camera) {
    glm::mat4 view = glm::mat4_cast(camera.orientation);
    view[3]        = glm::vec4(camera.position, 1.0f);
    view           = glm::inverse(view);
    view           = glm::scale(glm::identity<glm::mat4>(), glm::vec3(1, 1, -1)) * view;

    if (camera.type == CameraType::PERSPECTIVE) {
        float     f                 = 1.0f / tanf(glm::radians(camera.fov) / 2.0f);
        float     aspect            = camera.viewport_width / camera.viewport_height;
        glm::mat4 projection_matrix = glm::mat4(
            f / aspect,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            1.0f,
            0.0f,
            0.0f,
            camera.near_plane,
            0.0f
        );
        camera.projection_matrix = projection_matrix;
    } else {
        camera.projection_matrix = glm::ortho(
            -camera.viewport_width * 0.5f,
            camera.viewport_width * 0.5f,
            -camera.viewport_height * 0.5f,
            camera.viewport_height * 0.5f
        );
    }
    camera.view_matrix     = view;
    camera.combined_matrix = camera.projection_matrix * camera.view_matrix;
}

void move_camera(Camera& camera, glm::vec2 direction, float distance) {
    camera.position += float(direction.y * distance) * (camera.orientation * glm::vec3(1, 0, 0));
    camera.position += float(direction.x * distance) * (camera.orientation * glm::vec3(0, 0, -1));
}

glm::vec4 normalize_plane(glm::vec4 plane) {
    return plane / glm::length(glm::vec3(plane));
};
