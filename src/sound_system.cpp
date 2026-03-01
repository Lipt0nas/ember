#include "sound_system.hpp"

void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    ma_data_source_read_pcm_frames((ma_data_source*)pDevice->pUserData, pOutput, frameCount, NULL);

    (void)pInput;
}

bool SoundSystem::initialize() {
    free_list.reserve(MAX_INSTANCES);
    for (int i = MAX_INSTANCES - 1; i >= 0; i--) {
        free_list.push_back(i);
    }

    ma_resource_manager_config rm_config = ma_resource_manager_config_init();

    ma_result result = ma_resource_manager_init(&rm_config, &resource_manager);
    if (result != MA_SUCCESS) {
        spdlog::critical("Failed to initialize audio resource manager");
        return false;
    }

    ma_engine_config engine_config = ma_engine_config_init();
    engine_config.pResourceManager = &resource_manager;

    result = ma_engine_init(&engine_config, &engine);
    if (result != MA_SUCCESS) {
        ma_resource_manager_uninit(&resource_manager);
        spdlog::critical("Failed to initialize audio engine");
        return false;
    }

    return true;
}

void SoundSystem::cleanup() {
    for (size_t i = 0; i < MAX_INSTANCES; i++) {
        if (instances[i].in_use) {
            ma_sound_uninit(&instances[i].sound);
            instances[i].in_use = false;
        }
    }

    ma_engine_uninit(&engine);
    ma_resource_manager_uninit(&resource_manager);
}

void SoundSystem::update() {
    for (size_t i = 0; i < MAX_INSTANCES; i++) {
        if (instances[i].in_use && ma_sound_at_end(&instances[i].sound)) {
            ma_sound_uninit(&instances[i].sound);
            instances[i].in_use = false;
            free_list.push_back(i);
        }
    }
}

int SoundSystem::play_sound(const char* path, bool spatial) {
    if (free_list.empty()) {
        spdlog::warn("Sound instance pool exhausted");
        return INVALID_SOUND_INSTANCE;
    }

    int id = free_list.back();
    free_list.pop_back();

    ma_uint32 flags = 0;
    if (!spatial) {
        flags |= MA_SOUND_FLAG_NO_SPATIALIZATION;
    }

    ma_result result = ma_sound_init_from_file(&engine, path, flags, nullptr, nullptr, &instances[id].sound);
    if (result != MA_SUCCESS) {
        spdlog::error("Failed to load sound: {}", path);
        free_list.push_back(id);
        return INVALID_SOUND_INSTANCE;
    }

    ma_sound_start(&instances[id].sound);
    instances[id].in_use = true;

    return id;
}

void SoundSystem::stop_sound(int id) {
    SoundInstance* inst = get_instance(id);
    if (!inst) {
        return;
    }

    ma_sound_stop(&inst->sound);
    ma_sound_uninit(&inst->sound);
    inst->in_use = false;
    free_list.push_back(id);
}

void SoundSystem::stop_all_sounds() {
    for (size_t i = 0; i < MAX_INSTANCES; i++) {
        if (instances[i].in_use) {
            ma_sound_stop(&instances[i].sound);
            ma_sound_uninit(&instances[i].sound);
            instances[i].in_use = false;
            free_list.push_back(i);
        }
    }
}

void SoundSystem::set_listener(glm::vec3 position, glm::vec3 direction) {
    ma_engine_listener_set_position(&engine, 0, position.x, position.y, position.z);
    ma_engine_listener_set_direction(&engine, 0, direction.x, direction.y, direction.z);
    ma_engine_listener_set_world_up(&engine, 0, 0, 1, 0);
}

SoundInstance* SoundSystem::get_instance(int id) {
    if (id < 0 || id >= MAX_INSTANCES || !instances[id].in_use) {
        return nullptr;
    }

    return &instances[id];
}

void SoundSystem::preload(const Sound& sound) {
    if (sound.stream) {
        return;
    }

    ma_uint32 flags = MA_RESOURCE_MANAGER_DATA_SOURCE_FLAG_DECODE;
    ma_resource_manager_register_file(&resource_manager, sound.path.c_str(), flags);
}

void SoundSystem::set_sound_properties(
    int       id,
    float     volume,
    float     pitch,
    float     min_distance,
    float     max_distance,
    float     rolloff,
    bool      loop,
    glm::vec3 position
) {
    auto instance = get_instance(id);
    if (!instance) {
        return;
    }

    ma_sound_set_position(&instance->sound, position.x, position.y, position.z);
    ma_sound_set_volume(&instance->sound, volume);
    ma_sound_set_pitch(&instance->sound, pitch);
    ma_sound_set_min_distance(&instance->sound, min_distance);
    ma_sound_set_max_distance(&instance->sound, max_distance);
    ma_sound_set_rolloff(&instance->sound, rolloff);
    ma_sound_set_looping(&instance->sound, loop ? MA_TRUE : MA_FALSE);
}

void SoundSystem::set_sound_volume(int id, float volume) {
    auto instance = get_instance(id);
    if (!instance) {
        return;
    }

    ma_sound_set_volume(&instance->sound, volume);
}

void SoundSystem::set_sound_pitch(int id, float pitch) {
    auto instance = get_instance(id);
    if (!instance) {
        return;
    }

    ma_sound_set_pitch(&instance->sound, pitch);
}

void SoundSystem::set_sound_min_distance(int id, float min_distance) {
    auto instance = get_instance(id);
    if (!instance) {
        return;
    }

    ma_sound_set_min_distance(&instance->sound, min_distance);
}

void SoundSystem::set_sound_max_distance(int id, float max_distance) {
    auto instance = get_instance(id);
    if (!instance) {
        return;
    }

    ma_sound_set_max_distance(&instance->sound, max_distance);
}

void SoundSystem::set_sound_rolloff(int id, float rolloff) {
    auto instance = get_instance(id);
    if (!instance) {
        return;
    }

    ma_sound_set_rolloff(&instance->sound, rolloff);
}

void SoundSystem::set_sound_looping(int id, bool looping) {
    auto instance = get_instance(id);
    if (!instance) {
        return;
    }

    ma_sound_set_looping(&instance->sound, looping ? MA_TRUE : MA_FALSE);
}

void SoundSystem::set_sound_position(int id, glm::vec3 position) {
    auto instance = get_instance(id);
    if (!instance) {
        return;
    }

    ma_sound_set_position(&instance->sound, position.x, position.y, position.z);
}
