#include "camera.hpp"

FrustumPlanes extract_frustum_planes(const glm::mat4& view_proj) {
    FrustumPlanes planes;

    planes.left = glm::vec4(
        view_proj[0][3] + view_proj[0][0],
        view_proj[1][3] + view_proj[1][0],
        view_proj[2][3] + view_proj[2][0],
        view_proj[3][3] + view_proj[3][0]
    );

    planes.right = glm::vec4(
        view_proj[0][3] - view_proj[0][0],
        view_proj[1][3] - view_proj[1][0],
        view_proj[2][3] - view_proj[2][0],
        view_proj[3][3] - view_proj[3][0]
    );

    planes.bottom = glm::vec4(
        view_proj[0][3] + view_proj[0][1],
        view_proj[1][3] + view_proj[1][1],
        view_proj[2][3] + view_proj[2][1],
        view_proj[3][3] + view_proj[3][1]
    );

    planes.top = glm::vec4(
        view_proj[0][3] - view_proj[0][1],
        view_proj[1][3] - view_proj[1][1],
        view_proj[2][3] - view_proj[2][1],
        view_proj[3][3] - view_proj[3][1]
    );

    planes.near = glm::vec4(
        view_proj[0][3] + view_proj[0][2],
        view_proj[1][3] + view_proj[1][2],
        view_proj[2][3] + view_proj[2][2],
        view_proj[3][3] + view_proj[3][2]
    );

    planes.far = glm::vec4(
        view_proj[0][3] - view_proj[0][2],
        view_proj[1][3] - view_proj[1][2],
        view_proj[2][3] - view_proj[2][2],
        view_proj[3][3] - view_proj[3][2]
    );

    planes.left /= glm::length(glm::vec3(planes.left));
    planes.right /= glm::length(glm::vec3(planes.right));
    planes.bottom /= glm::length(glm::vec3(planes.bottom));
    planes.top /= glm::length(glm::vec3(planes.top));
    planes.near /= glm::length(glm::vec3(planes.near));
    planes.far /= glm::length(glm::vec3(planes.far));

    return planes;
}

void update_camera(Camera& camera) {
    float     f                 = 1.0f / tanf(glm::radians(camera.fov) / 2.0f);
    float     aspect            = camera.viewport_width / camera.viewport_height;
    glm::mat4 projection_matrix = glm::mat4(
        f / aspect, 0.0f, 0.0f, 0.0f, 0.0f, f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, camera.near_plane, 0.0f
    );

    glm::mat4 view = glm::mat4_cast(camera.orientation);
    view[3]        = glm::vec4(camera.position, 1.0f);
    view           = glm::inverse(view);
    view           = glm::scale(glm::identity<glm::mat4>(), glm::vec3(1, 1, -1)) * view;

    camera.projection_matrix = projection_matrix;
    camera.view_matrix       = view;
    camera.combined_matrix   = projection_matrix * view;
    camera.planes            = extract_frustum_planes(camera.combined_matrix);
}

void move_camera(Camera& camera, glm::vec2 direction, float distance) {
    camera.position += float(direction.y * distance) * (camera.orientation * glm::vec3(1, 0, 0));
    camera.position += float(direction.x * distance) * (camera.orientation * glm::vec3(0, 0, -1));
}
