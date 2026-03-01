#pragma once

#include "ember.hpp"
#include "resources.hpp"

#include <miniaudio.h>

struct SoundInstance {
    ma_sound sound;
    bool     in_use = false;
};

class SoundSystem {
public:
    constexpr inline static int INVALID_SOUND_INSTANCE = -1;

    bool initialize();
    void cleanup();

    void update();

    void preload(const Sound& sound);

    int  play_sound(const char* path, bool spatial);
    void stop_sound(int id);
    void stop_all_sounds();

    void set_listener(glm::vec3 position, glm::vec3 direction);

    void set_sound_volume(int id, float volume);
    void set_sound_pitch(int id, float pitch);
    void set_sound_min_distance(int id, float min_distance);
    void set_sound_max_distance(int id, float max_distance);
    void set_sound_rolloff(int id, float rolloff);
    void set_sound_looping(int id, bool looping);
    void set_sound_position(int id, glm::vec3 position);

    void set_sound_properties(
        int       id,
        float     volume,
        float     pitch,
        float     min_distance,
        float     max_distance,
        float     rolloff,
        bool      loop,
        glm::vec3 position
    );

private:
    constexpr inline static int MAX_INSTANCES = 256;

    ma_engine           engine;
    ma_resource_manager resource_manager;

    SoundInstance    instances[MAX_INSTANCES];
    std::vector<int> free_list;

    SoundInstance* get_instance(int id);
};
