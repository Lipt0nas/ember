#pragma once

#include "ember.hpp"

struct Material {
    uint32_t albedo_index;
    uint32_t normals_index;
    uint32_t material_index;
    uint32_t emissive_index;

    glm::vec4 albedo_factor;
    glm::vec3 emissive_factor;

    float roughness_factor;
    float metallic_factor;
    float normal_scale;
};

struct MeshLOD {
    uint32_t index_offset;
    uint32_t index_count;

    uint32_t meshlet_offset;
    uint32_t meshlet_count;

    float error;
};

struct alignas(16) Mesh {
    glm::vec3 center = {};
    float     radius = 0.0f;

    glm::vec4 bounds_min;
    glm::vec4 bounds_max;

    uint32_t vertex_offset;
    uint32_t vertex_count;

    uint32_t lod_count;
    MeshLOD  lods[8];
};

struct MeshInstance {
    int mesh_id     = 0;
    int material_id = 0;

    glm::vec3 position = {};
    float     scale    = 1.0f;

    glm::quat rotation = {0.0f, 0.0f, 0.0f, 1.0f};

    glm::vec3 last_position = {};
    float     last_scale    = 1.0f;

    glm::quat last_rotation = {0.0f, 0.0f, 0.0f, 1.0f};
};

struct Vertex {
    uint16_t px;
    uint16_t py;
    uint16_t pz;

    uint16_t ux;
    uint16_t uy;

    uint16_t tn;

    // normal + tangent sign
    uint32_t norm;
};

struct MeshletBounds {
    glm::vec3 center;
    float     radius;

    glm::vec3 cone_axis;
    float     cone_cutoff;

    glm::vec3 cone_apex;
    float     _pad;
};

struct DebugVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
};

uint16_t pack_tangent(glm::vec3 tangent);

void generate_tangents(std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);

class IcosphereGenerator {
private:
    std::vector<DebugVertex>                          vertices;
    std::vector<uint32_t>                             indices;
    std::map<std::pair<uint32_t, uint32_t>, uint32_t> midpoint_cache;

    uint32_t  get_middle_point(uint32_t p1, uint32_t p2);
    glm::vec2 calculate_tex_coord(const glm::vec3& pos);

public:
    void generate(float radius, int subdivisions);

    const std::vector<DebugVertex>& get_vertices() const;
    const std::vector<uint32_t>&    get_indices() const;
};
