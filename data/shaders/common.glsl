layout(set = 1, binding = 0) uniform SceneUBO {
    mat4 view_proj;
    vec4 planes[6];
    vec4 camera_position;

    mat4 frozen_view_proj;
    vec4 frozen_planes[6];
    vec4 frozen_camera_position;

    uint debug_frustum;
    uint disable_culling;
} scene;
