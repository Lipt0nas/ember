#pragma once

#include "ember.hpp"

struct Joint {
    int parent_index;

    glm::vec3 bind_translation;
    glm::quat bind_rotation;
    glm::vec3 bind_scale;

    glm::mat4 inverse_bind_matrix;
};

struct Skeleton {
    std::vector<Joint> joints;
};

struct AnimationSampler {
    std::vector<float>     times;
    std::vector<glm::vec3> vec_values;
    std::vector<glm::quat> quat_values;
};

struct AnimationChannel {
    int              joint_index;
    std::string      path;
    AnimationSampler sampler;
};

struct Animation {
    std::string                   name;
    float                         duration;
    int                           skeleton_index;
    std::vector<AnimationChannel> channels;
};

struct JointPose {
    glm::vec3 translation = {0, 0, 0};
    glm::quat rotation    = {1, 0, 0, 0};
    glm::vec3 scale       = {1, 1, 1};
};

struct SkeletonPose {
    std::vector<JointPose> joints;
};

template <typename T> T sample_linear(const std::vector<float>& times, const std::vector<T>& values, float t) {
    assert(!times.empty() && times.size() == values.size());

    if (t <= times.front()) {
        return values.front();
    }

    if (t >= times.back()) {
        return values.back();
    }

    for (int i = 0; i < (int)times.size() - 1; i++) {
        if (t >= times[i] && t < times[i + 1]) {
            float alpha = (t - times[i]) / (times[i + 1] - times[i]);

            if constexpr (std::is_same_v<T, glm::quat>) {
                return glm::slerp(values[i], values[i + 1], alpha);
            } else {
                return glm::mix(values[i], values[i + 1], alpha);
            }
        }
    }

    return values.back();
}

static SkeletonPose sample_animation(const Animation& anim, const Skeleton& skeleton, float t) {
    SkeletonPose pose;
    pose.joints.resize(skeleton.joints.size());

    for (int i = 0; i < (int)skeleton.joints.size(); i++) {
        pose.joints[i].translation = skeleton.joints[i].bind_translation;
        pose.joints[i].rotation    = skeleton.joints[i].bind_rotation;
        pose.joints[i].scale       = skeleton.joints[i].bind_scale;
    }

    for (const auto& ch : anim.channels) {
        JointPose& jp = pose.joints[ch.joint_index];

        if (ch.path == "translation") {
            jp.translation = sample_linear(ch.sampler.times, ch.sampler.vec_values, t);
        } else if (ch.path == "rotation") {
            jp.rotation = sample_linear(ch.sampler.times, ch.sampler.quat_values, t);
        } else if (ch.path == "scale") {
            jp.scale = sample_linear(ch.sampler.times, ch.sampler.vec_values, t);
        }
    }

    return pose;
}

static void compute_global_transforms(
    const Skeleton& skeleton, const SkeletonPose& pose, std::vector<glm::mat4>& global_transforms
) {
    global_transforms.resize(skeleton.joints.size());

    for (int i = 0; i < (int)skeleton.joints.size(); i++) {
        const JointPose& jp = pose.joints[i];

        glm::mat4 local = glm::translate(glm::mat4(1), jp.translation) * glm::mat4_cast(jp.rotation) *
                          glm::scale(glm::mat4(1), jp.scale);

        int p                = skeleton.joints[i].parent_index;
        global_transforms[i] = (p < 0) ? local : global_transforms[p] * local;
    }
}
