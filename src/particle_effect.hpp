#pragma once

#include "ember.hpp"

enum class ParticleOp : int8_t {
    LOAD_ATTRIB  = 0,
    STORE_ATTRIB = 1,
    SWIZZLE_X    = 2,
    SWIZZLE_Y    = 3,
    SWIZZLE_Z    = 4,
    SWIZZLE_W    = 5,
    COMPOSE_VEC3 = 7,
    COMPOSE_VEC4 = 8,
    CONST        = 16,
    LOAD_PARAM   = 17,
    ADD          = 32,
    SUB          = 33,
    MULT         = 34,
    DIV          = 35,
    SIN          = 36,
    COS          = 37,
    LERP         = 38,
    RAND         = 39,
};

struct ParticleInstruction {
    ParticleOp op;
    uint16_t   dst;
    uint16_t   srcs[5];
    uint16_t   imm;

    template <class Archive> void serialize(Archive& ar) {
        ar(op, dst, srcs, imm);
    }
};

struct ParticleEmitterConfig {
    uint32_t max_particles    = 100;
    float    emission_rate    = 10.0f;
    float    emitter_lifetime = -1.0f;
    bool     loop             = true;
    bool     additive         = true;
    bool     attached         = false;

    template <class Archive> void serialize(Archive& ar) {
        ar(max_particles, emission_rate, emitter_lifetime, loop, additive, attached);
    }
};

struct ParticleEffectHeader {
    uint32_t emmiter_count;

    template <class Archive> void serialize(Archive& ar) {
        ar(emmiter_count);
    }
};

struct ParticleEmitterHeader {
    uint32_t spawn_register_count;
    uint32_t update_register_count;
    uint64_t spawn_instruction_size;
    uint64_t update_instruction_size;

    template <class Archive> void serialize(Archive& ar) {
        ar(spawn_register_count, update_register_count, spawn_instruction_size, update_instruction_size);
    }
};

struct ParticleEmitterAsset {
    std::string name;

    std::vector<glm::vec4>           spawn_register_state;
    std::vector<ParticleInstruction> spawn_instructions;

    std::vector<glm::vec4>           update_register_state;
    std::vector<ParticleInstruction> update_instructions;

    template <class Archive> void serialize(Archive& ar) {
        ar(name, spawn_register_state, spawn_instructions, update_register_state, update_instructions);
    }
};

struct ParticleEffectAsset {
    std::vector<ParticleEmitterAsset> emitters;

    template <class Archive> void serialize(Archive& ar) {
        ar(emitters);
    }
};

struct Particle {
    glm::vec3 position     = {0.0f, 0.0f, 0.0f};
    float     age          = 0.0f;
    glm::vec3 velocity     = {0.0f, 0.0f, 0.0f};
    float     lifetime     = 3.0f;
    glm::vec4 color        = {1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec4 rotation     = {0.0f, 0.0f, 0.0f, 1.0f};
    glm::vec3 size         = {1.0f, 1.0f, 1.0f};
    float     max_lifetime = 3.0f;

    enum Attribute : uint16_t {
        POSITION = 0,
        AGE,
        VELOCITY,
        LIFETIME,
        COLOR,
        ROTATION,
        SIZE,
    };

    static const char* attrib_name(uint16_t a);
};

enum class SimParam : uint16_t {
    DELTA_TIME  = 0,
    TIME        = 1,
    PARTICLE_ID = 2,
};

struct SimParams {
    float    delta_time  = 0.0f;
    float    time        = 0.0f;
    uint32_t particle_id = 0;

    glm::vec3 emitter_pos = {};
};

struct ParticleEmitter {
    struct InstructionSet {
        std::vector<glm::vec4>           registers;
        std::vector<glm::vec4>           clean_register_state;
        std::vector<ParticleInstruction> instructions;

        InstructionSet() = default;

        InstructionSet(std::vector<ParticleInstruction> instrs, const std::vector<glm::vec4>& constants);
    };

    std::string           name;
    std::vector<Particle> particles;
    InstructionSet        spawn_instructions;
    InstructionSet        update_instructions;

    uint32_t live_count           = 0;
    uint32_t dead_count           = 0;
    float    emission_accumulator = 0.0f;
    float    emission_rate        = 1.0f;
    float    emitter_lifetime     = -1.0f;
    bool     loop                 = true;
    bool     additive             = true;
    bool     attached             = false;

    ParticleEmitter() = default;
    ParticleEmitter(const std::string& name, uint32_t count, InstructionSet spawn, InstructionSet update);

    void execute_instruction(Particle& p, InstructionSet& set, ParticleInstruction& ins, const SimParams& params);

    void simulate(SimParams params);
    void simulate_spawn(Particle& p, SimParams params);

    void resize(uint32_t new_max);
};

struct ParticleEffect {
    std::vector<ParticleEmitter> emitters;

    ParticleEffect() = default;

    ParticleEffect(ParticleEffectAsset& asset);
};
