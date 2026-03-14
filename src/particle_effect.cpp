#include "particle_effect.hpp"

ParticleEmitter::InstructionSet::InstructionSet(
    std::vector<ParticleInstruction> instrs, const std::vector<glm::vec4>& constants
)
    : registers(constants.size()), instructions(std::move(instrs)) {
    for (auto& i : instructions) {
        if (i.op == ParticleOp::CONST) {
            registers[i.dst] = constants[i.imm];
        }
    }
    clean_register_state = registers;
}

ParticleEmitter::ParticleEmitter(const std::string& name, uint32_t count, InstructionSet spawn, InstructionSet update)
    : name(name), particles(count), spawn_instructions(std::move(spawn)), update_instructions(std::move(update)) {
    dead_count = count;
    live_count = 0;
}

void ParticleEmitter::execute_instruction(
    Particle& p, InstructionSet& set, ParticleInstruction& ins, const SimParams& params
) {
    auto& r = set.registers;
    switch (ins.op) {
    case ParticleOp::ADD: {
        r[ins.dst] = r[ins.srcs[0]] + r[ins.srcs[1]];
        break;
    }
    case ParticleOp::SUB: {
        r[ins.dst] = r[ins.srcs[0]] - r[ins.srcs[1]];
        break;
    }
    case ParticleOp::MULT: {
        r[ins.dst] = r[ins.srcs[0]] * r[ins.srcs[1]];
        break;
    }
    case ParticleOp::DIV: {
        r[ins.dst] = r[ins.srcs[0]] / r[ins.srcs[1]];
        break;
    }
    case ParticleOp::SIN: {
        r[ins.dst] = glm::sin(r[ins.srcs[0]]);
        break;
    }
    case ParticleOp::COS: {
        r[ins.dst] = glm::cos(r[ins.srcs[0]]);
        break;
    }
    case ParticleOp::LERP: {
        r[ins.dst] = glm::mix(r[ins.srcs[0]], r[ins.srcs[1]], r[ins.srcs[2]]);
        break;
    }
    case ParticleOp::RAND: {
        r[ins.dst] = glm::mix(r[ins.srcs[0]], r[ins.srcs[1]], glm::linearRand(glm::vec4(0.0f), glm::vec4(1.0f)));
        break;
    }
    case ParticleOp::SWIZZLE_X: {
        r[ins.dst] = glm::vec4(r[ins.srcs[0]].x);
        break;
    }
    case ParticleOp::SWIZZLE_Y: {
        r[ins.dst] = glm::vec4(r[ins.srcs[0]].y);
        break;
    }
    case ParticleOp::SWIZZLE_Z: {
        r[ins.dst] = glm::vec4(r[ins.srcs[0]].z);
        break;
    }
    case ParticleOp::SWIZZLE_W: {
        r[ins.dst] = glm::vec4(r[ins.srcs[0]].w);
        break;
    }
    case ParticleOp::COMPOSE_VEC3: {
        r[ins.dst] = glm::vec4(r[ins.srcs[0]].x, r[ins.srcs[1]].x, r[ins.srcs[2]].x, 0.0f);
        break;
    }
    case ParticleOp::COMPOSE_VEC4: {
        r[ins.dst] = glm::vec4(r[ins.srcs[0]].x, r[ins.srcs[1]].x, r[ins.srcs[2]].x, r[ins.srcs[3]].x);
        break;
    }
    case ParticleOp::LOAD_PARAM: {
        switch ((SimParam)ins.imm) {
        case SimParam::DELTA_TIME: {
            r[ins.dst] = glm::vec4(params.delta_time);
            break;
        }
        case SimParam::TIME: {
            r[ins.dst] = glm::vec4(params.time);
            break;
        }
        case SimParam::PARTICLE_ID: {
            r[ins.dst] = glm::vec4((float)params.particle_id);
            break;
        }
        default:
            break;
        }
        break;
    }
    case ParticleOp::STORE_ATTRIB: {
        glm::vec4 val = r[ins.imm];
        switch (ins.srcs[0]) {
        case Particle::POSITION: {
            p.position = val;
            break;
        }
        case Particle::AGE: {
            p.age = val.x;
            break;
        }
        case Particle::VELOCITY: {
            p.velocity = val;
            break;
        }
        case Particle::LIFETIME: {
            p.lifetime     = val.x;
            p.max_lifetime = val.x;
            break;
        }
        case Particle::COLOR: {
            p.color = val;
            break;
        }
        case Particle::ROTATION: {
            p.rotation = val;
            break;
        }
        case Particle::SIZE: {
            p.size = glm::vec3(val);
            break;
        }
        default:
            break;
        }
        break;
    }
    case ParticleOp::LOAD_ATTRIB: {
        glm::vec4 val{};
        switch (ins.srcs[0]) {
        case Particle::POSITION: {
            val = glm::vec4(p.position, 1.f);
            break;
        }
        case Particle::AGE: {
            val = glm::vec4(p.age);
            break;
        }
        case Particle::VELOCITY: {
            val = glm::vec4(p.velocity, 0.f);
            break;
        }
        case Particle::LIFETIME: {
            val = glm::vec4(p.lifetime);
            break;
        }
        case Particle::COLOR: {
            val = p.color;
            break;
        }
        case Particle::ROTATION: {
            val = p.rotation;
            break;
        }
        case Particle::SIZE: {
            val = glm::vec4(p.size, 0.f);
            break;
        }
        default:
            break;
        }
        r[ins.dst] = val;
        break;
    }
    default:
        break;
    }
}

void ParticleEmitter::simulate(SimParams params) {
    if (emission_rate > 0.0f && !finished) {
        emission_accumulator += params.delta_time;
        const float interval = 1.0f / emission_rate;

        while (emission_accumulator >= interval) {
            emission_accumulator -= interval;

            if (live_count < particles.size()) {
                Particle& p = particles[live_count];
                p           = {};

                simulate_spawn(p, params);
                p.position += params.emitter_pos;

                ++live_count;
                --dead_count;
            }
        }
    }

    for (uint32_t i = 0; i < live_count;) {
        Particle& p = particles[i];

        p.lifetime -= params.delta_time;

        if (p.lifetime <= 0.0f) {
            particles[i] = particles[--live_count];
            ++dead_count;

            continue;
        }

        p.age = 1.0 - (p.lifetime / p.max_lifetime);

        update_instructions.registers = update_instructions.clean_register_state;
        for (auto& ins : update_instructions.instructions) {
            execute_instruction(p, update_instructions, ins, params);
        }

        p.position += p.velocity * params.delta_time;

        i++;
    }

    if (current_lifetime >= 0.0f && emitter_lifetime != -1.0f) {
        current_lifetime -= params.delta_time;

        if (current_lifetime <= 0.0f) {
            if (loop) {
                current_lifetime     = emitter_lifetime;
                emission_accumulator = 0.0f;
            } else {
                finished = true;
            }
        }
    }
}

void ParticleEmitter::simulate_spawn(Particle& p, SimParams params) {
    auto& set     = spawn_instructions;
    set.registers = set.clean_register_state;
    for (auto& ins : set.instructions) {
        execute_instruction(p, set, ins, params);
    }
}

void ParticleEmitter::resize(uint32_t new_max) {
    particles.resize(new_max);

    live_count = std::min(live_count, new_max);
    dead_count = new_max - live_count;
}

const char* Particle::attrib_name(uint16_t a) {
    switch (a) {
    case POSITION:
        return "Position";
    case AGE:
        return "Age";
    case VELOCITY:
        return "Velocity";
    case LIFETIME:
        return "Lifetime";
    case COLOR:
        return "Color";
    case ROTATION:
        return "Rotation";
    case SIZE:
        return "Size";
    default:
        return "Unknown";
    }
}

ParticleEffect::ParticleEffect(ParticleEffectAsset& asset) {
    for (auto& emitter : asset.emitters) {
        emitters.push_back(ParticleEmitter(
            emitter.name,
            10,
            ParticleEmitter::InstructionSet(emitter.spawn_instructions, emitter.spawn_register_state),
            ParticleEmitter::InstructionSet(emitter.update_instructions, emitter.update_register_state)
        ));
    }
}
