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

struct Mesh {
    uint32_t meshlet_offset;
    uint32_t meshlet_count;

    uint32_t vertex_count;
    uint32_t vertex_offset;

    uint32_t index_count;
    uint32_t first_index;

    glm::vec3 center     = {};
    float     radius     = 0.0f;
    glm::vec3 bounds_min = {};
    glm::vec3 bounds_max = {};
};

struct MeshInstance {
    int mesh_id;
    int material_id;

    glm::vec3 position = {};
    float     scale    = 1.0f;

    glm::quat rotation = {0.0f, 0.0f, 0.0f, 1.0f};
};

struct Vertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;

    bool operator==(const Vertex& other) const {
        return position == other.position && normal == other.normal && uv == other.uv;
    }
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
    glm::vec4 color;
};

class IcosphereGenerator {
private:
    std::vector<Vertex>                               vertices;
    std::vector<uint32_t>                             indices;
    std::map<std::pair<uint32_t, uint32_t>, uint32_t> midpoint_cache;

    uint32_t get_middle_point(uint32_t p1, uint32_t p2) {
        bool                          first_is_smaller = p1 < p2;
        std::pair<uint32_t, uint32_t> key = first_is_smaller ? std::make_pair(p1, p2) : std::make_pair(p2, p1);

        auto it = midpoint_cache.find(key);
        if (it != midpoint_cache.end()) {
            return it->second;
        }

        glm::vec3 point1 = vertices[p1].position;
        glm::vec3 point2 = vertices[p2].position;
        glm::vec3 middle = glm::normalize((point1 + point2) * 0.5f);

        uint32_t index = static_cast<uint32_t>(vertices.size());
        vertices.push_back({middle, middle, calculate_tex_coord(middle)});

        midpoint_cache[key] = index;

        return index;
    }

    glm::vec2 calculate_tex_coord(const glm::vec3& pos) {
        float u = 0.5f + atan2f(pos.z, pos.x) / (2.0f * M_PI);
        float v = 0.5f - asinf(pos.y) / M_PI;

        return glm::vec2(u, v);
    }

public:
    void generate(float radius, int subdivisions) {
        vertices.clear();
        indices.clear();
        midpoint_cache.clear();

        const float t = (1.0f + sqrtf(5.0f)) / 2.0f;

        std::vector<glm::vec3> base_vertices = {
            glm::normalize(glm::vec3(-1, t, 0)),
            glm::normalize(glm::vec3(1, t, 0)),
            glm::normalize(glm::vec3(-1, -t, 0)),
            glm::normalize(glm::vec3(1, -t, 0)),

            glm::normalize(glm::vec3(0, -1, t)),
            glm::normalize(glm::vec3(0, 1, t)),
            glm::normalize(glm::vec3(0, -1, -t)),
            glm::normalize(glm::vec3(0, 1, -t)),

            glm::normalize(glm::vec3(t, 0, -1)),
            glm::normalize(glm::vec3(t, 0, 1)),
            glm::normalize(glm::vec3(-t, 0, -1)),
            glm::normalize(glm::vec3(-t, 0, 1))
        };

        for (const auto& pos : base_vertices) {
            vertices.push_back({pos, pos, calculate_tex_coord(pos)});
        }

        std::vector<uint32_t> base_indices = {// 5 faces around point 0
                                              0, 11, 5, 0, 5,  1,  0,  1,  7,  0,  7, 10, 0, 10, 11,

                                              1, 5,  9, 5, 11, 4,  11, 10, 2,  10, 7, 6,  7, 1,  8,

                                              3, 9,  4, 3, 4,  2,  3,  2,  6,  3,  6, 8,  3, 8,  9,

                                              4, 9,  5, 2, 4,  11, 6,  2,  10, 8,  6, 7,  9, 8,  1
        };

        indices = base_indices;

        for (int i = 0; i < subdivisions; i++) {
            std::vector<uint32_t> new_indices;

            for (size_t j = 0; j < indices.size(); j += 3) {
                uint32_t v1 = indices[j];
                uint32_t v2 = indices[j + 1];
                uint32_t v3 = indices[j + 2];

                uint32_t a = get_middle_point(v1, v2);
                uint32_t b = get_middle_point(v2, v3);
                uint32_t c = get_middle_point(v3, v1);

                new_indices.push_back(v1);
                new_indices.push_back(a);
                new_indices.push_back(c);
                new_indices.push_back(v2);
                new_indices.push_back(b);
                new_indices.push_back(a);
                new_indices.push_back(v3);
                new_indices.push_back(c);
                new_indices.push_back(b);
                new_indices.push_back(a);
                new_indices.push_back(b);
                new_indices.push_back(c);
            }

            indices = new_indices;
        }

        if (radius != 1.0f) {
            for (auto& vertex : vertices) {
                vertex.position *= radius;
            }
        }
    }

    const std::vector<Vertex>& get_vertices() const {
        return vertices;
    }

    const std::vector<uint32_t>& get_indices() const {
        return indices;
    }
};
