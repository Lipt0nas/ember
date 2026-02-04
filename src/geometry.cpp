#include "geometry.hpp"

uint16_t pack_tangent(glm::vec3 tangent) {
    float sum = glm::abs(tangent.x) + glm::abs(tangent.y) + glm::abs(tangent.z);
    float tu  = tangent.z >= 0 ? tangent.x / sum : (1 - glm::abs(tangent.y / sum)) * (tangent.x >= 0 ? 1 : -1);
    float tv  = tangent.z >= 0 ? tangent.y / sum : (1 - glm::abs(tangent.x / sum)) * (tangent.y >= 0 ? 1 : -1);

    return (meshopt_quantizeSnorm(tu, 8) + 127) | (meshopt_quantizeSnorm(tv, 8) + 127) << 8;
}

void generate_tangents(std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) {
    std::vector<glm::vec3> tangents(vertices.size(), glm::vec3(0));
    std::vector<glm::vec3> bitangents(vertices.size(), glm::vec3(0));

    for (size_t i = 0; i < indices.size(); i += 3) {
        uint32_t i0 = indices[i + 0];
        uint32_t i1 = indices[i + 1];
        uint32_t i2 = indices[i + 2];

        const Vertex& v0 = vertices[i0];
        const Vertex& v1 = vertices[i1];
        const Vertex& v2 = vertices[i2];

        const glm::vec3 v0_pos =
            glm::vec3(meshopt_dequantizeHalf(v0.px), meshopt_dequantizeHalf(v0.py), meshopt_dequantizeHalf(v0.pz));
        const glm::vec3 v1_pos =
            glm::vec3(meshopt_dequantizeHalf(v1.px), meshopt_dequantizeHalf(v1.py), meshopt_dequantizeHalf(v1.pz));
        const glm::vec3 v2_pos =
            glm::vec3(meshopt_dequantizeHalf(v2.px), meshopt_dequantizeHalf(v2.py), meshopt_dequantizeHalf(v2.pz));

        const glm::vec2 uv0 = glm::vec2(meshopt_dequantizeHalf(v0.ux), meshopt_dequantizeHalf(v0.uy));
        const glm::vec2 uv1 = glm::vec2(meshopt_dequantizeHalf(v1.ux), meshopt_dequantizeHalf(v1.uy));
        const glm::vec2 uv2 = glm::vec2(meshopt_dequantizeHalf(v2.ux), meshopt_dequantizeHalf(v2.uy));

        glm::vec3 edge1     = v1_pos - v0_pos;
        glm::vec3 edge2     = v2_pos - v0_pos;
        glm::vec2 delta_uv1 = uv1 - uv0;
        glm::vec2 delta_uv2 = uv2 - uv0;

        float f = 1.0f / (delta_uv1.x * delta_uv2.y - delta_uv2.x * delta_uv1.y);

        glm::vec3 tangent;
        tangent.x = f * (delta_uv2.y * edge1.x - delta_uv1.y * edge2.x);
        tangent.y = f * (delta_uv2.y * edge1.y - delta_uv1.y * edge2.y);
        tangent.z = f * (delta_uv2.y * edge1.z - delta_uv1.y * edge2.z);

        glm::vec3 bitangent;
        bitangent.x = f * (-delta_uv2.x * edge1.x + delta_uv1.x * edge2.x);
        bitangent.y = f * (-delta_uv2.x * edge1.y + delta_uv1.x * edge2.y);
        bitangent.z = f * (-delta_uv2.x * edge1.z + delta_uv1.x * edge2.z);

        tangents[i0] += tangent;
        tangents[i1] += tangent;
        tangents[i2] += tangent;

        bitangents[i0] += bitangent;
        bitangents[i1] += bitangent;
        bitangents[i2] += bitangent;
    }

    for (size_t i = 0; i < vertices.size(); ++i) {
        const glm::vec3& n = glm::vec3(
            (vertices[i].norm & 1023) / 511.0f - 1.0f,
            ((vertices[i].norm >> 10) & 1023) / 511.0f - 1.0f,
            ((vertices[i].norm >> 20) & 1023) / 511.0f - 1.0f
        );
        const glm::vec3& t = tangents[i];
        const glm::vec3& b = bitangents[i];

        glm::vec3 tangent = glm::normalize(t - n * glm::dot(n, t));

        float handedness = (glm::dot(glm::cross(n, tangent), b) < 0.0f) ? -1.0f : 1.0f;

        vertices[i].norm |= (handedness >= 0 ? 0 : 1) << 30;
        vertices[i].tn = pack_tangent(tangent);
    }
}

uint32_t IcosphereGenerator::get_middle_point(uint32_t p1, uint32_t p2) {
    bool                          first_is_smaller = p1 < p2;
    std::pair<uint32_t, uint32_t> key              = first_is_smaller ? std::make_pair(p1, p2) : std::make_pair(p2, p1);

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

glm::vec2 IcosphereGenerator::calculate_tex_coord(const glm::vec3& pos) {
    float u = 0.5f + atan2f(pos.z, pos.x) / (2.0f * glm::pi<float>());
    float v = 0.5f - asinf(pos.y) / glm::pi<float>();

    return glm::vec2(u, v);
}

void IcosphereGenerator::generate(float radius, int subdivisions) {
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

const std::vector<DebugVertex>& IcosphereGenerator::get_vertices() const {
    return vertices;
}

const std::vector<uint32_t>& IcosphereGenerator::get_indices() const {
    return indices;
}

glm::vec3 rotate_quat(glm::vec3 v, glm::quat q) {
    return v + 2.0f * glm::cross(glm::vec3(q.x, q.y, q.z), glm::cross(glm::vec3(q.x, q.y, q.z), v) + q.w * v);
}
