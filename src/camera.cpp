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
    camera.projection_matrix = glm::perspectiveFov(
        glm::radians(camera.fov), camera.viewport_width, camera.viewport_height, camera.near_plane, camera.far_plane
    );
    camera.projection_matrix[1][1] *= -1;
    camera.view_matrix = glm::lookAt(camera.position, camera.position + camera.direction, camera.up);

    camera.combined_matrix = camera.projection_matrix * camera.view_matrix;

    camera.planes = extract_frustum_planes(camera.combined_matrix);
}

void move_camera(Camera& camera, glm::vec3 direction, float distance) {
    glm::vec3 temp = direction;
    temp           = glm::normalize(temp);
    temp           = temp * distance;

    camera.position += temp;
}
