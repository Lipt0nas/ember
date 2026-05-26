#pragma once

#include "ember.hpp"

#include <mutex>

#include <Jolt/Jolt.h>
#include <spdlog/spdlog.h>

#include <Jolt/Physics/Character/CharacterVirtual.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/CompoundShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/MutableCompoundShape.h>
#include <Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/ScaledShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Renderer/DebugRendererSimple.h>

struct ContactAddedEvent {
    uint32_t body_1;
    uint32_t body_2;

    glm::vec3 normal;
    float     penetration_depth;
    float     impact_speed;

    glm::vec3 contact_point;
};

struct ContactRemovedEvent {
    uint32_t body_1;
    uint32_t body_2;
};

struct CollisionStarted {
    uint32_t other;

    glm::vec3 normal;
    float     penetration_depth;
    float     impact_speed;

    glm::vec3 contact_point;
};

struct CollisionEnded {
    uint32_t other;
};

// Layer that objects can be in, determines which other objects it can collide with
// Typically you at least want to have 1 layer for moving bodies and 1 layer for static bodies, but you can have more
// layers if you want. E.g. you could have a layer for high detail collision (which is not used by the physics
// simulation but only if you do collision testing).
namespace Layers {
    static constexpr JPH::ObjectLayer NON_MOVING = 0;
    static constexpr JPH::ObjectLayer MOVING     = 1;
    static constexpr JPH::ObjectLayer NUM_LAYERS = 2;
}; // namespace Layers

/// Class that determines if two object layers can collide
class ObjectLayerPairFilterImpl : public JPH::ObjectLayerPairFilter {
public:
    virtual bool ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const override {
        switch (inObject1) {
        case Layers::NON_MOVING:
            return inObject2 == Layers::MOVING; // Non moving only collides with moving
        case Layers::MOVING:
            return true; // Moving collides with everything
        default:
            JPH_ASSERT(false);
            return false;
        }
    }
};

// Each broadphase layer results in a separate bounding volume tree in the broad phase. You at least want to have
// a layer for non-moving and moving objects to avoid having to update a tree full of static objects every frame.
// You can have a 1-on-1 mapping between object layers and broadphase layers (like in this case) but if you have
// many object layers you'll be creating many broad phase trees, which is not efficient. If you want to fine tune
// your broadphase layers define JPH_TRACK_BROADPHASE_STATS and look at the stats reported on the TTY.
namespace BroadPhaseLayers {
    static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
    static constexpr JPH::BroadPhaseLayer MOVING(1);
    static constexpr uint32_t             NUM_LAYERS(2);
}; // namespace BroadPhaseLayers

// BroadPhaseLayerInterface implementation
// This defines a mapping between object and broadphase layers.
class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface {
public:
    BPLayerInterfaceImpl() {
        // Create a mapping table from object to broad phase layer
        mObjectToBroadPhase[Layers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
        mObjectToBroadPhase[Layers::MOVING]     = BroadPhaseLayers::MOVING;
    }

    virtual uint32_t GetNumBroadPhaseLayers() const override {
        return BroadPhaseLayers::NUM_LAYERS;
    }

    virtual JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override {
        JPH_ASSERT(inLayer < Layers::NUM_LAYERS);
        return mObjectToBroadPhase[inLayer];
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    virtual const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override {
        switch ((JPH::BroadPhaseLayer::Type)inLayer) {
        case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::NON_MOVING:
            return "NON_MOVING";
        case (JPH::BroadPhaseLayer::Type)BroadPhaseLayers::MOVING:
            return "MOVING";
        default:
            JPH_ASSERT(false);
            return "INVALID";
        }
    }
#endif // JPH_EXTERNAL_PROFILE || JPH_PROFILE_ENABLED

private:
    JPH::BroadPhaseLayer mObjectToBroadPhase[Layers::NUM_LAYERS];
};

/// Class that determines if an object layer can collide with a broadphase layer
class ObjectVsBroadPhaseLayerFilterImpl : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    virtual bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override {
        switch (inLayer1) {
        case Layers::NON_MOVING:
            return inLayer2 == BroadPhaseLayers::MOVING;
        case Layers::MOVING:
            return true;
        default:
            JPH_ASSERT(false);
            return false;
        }
    }
};

class PhysicsContactListener : public JPH::ContactListener {
public:
    class World* world = nullptr;

    std::mutex                                      pair_mutex;
    std::unordered_set<uint64_t>                    active_pairs;
    std::unordered_set<uint64_t>                    prev_pairs;
    std::unordered_map<uint64_t, ContactAddedEvent> pair_data;

    virtual JPH::ValidateResult OnContactValidate(
        const JPH::Body&               inBody1,
        const JPH::Body&               inBody2,
        JPH::RVec3Arg                  inBaseOffset,
        const JPH::CollideShapeResult& inCollisionResult
    ) override;

    virtual void OnContactAdded(
        const JPH::Body&            inBody1,
        const JPH::Body&            inBody2,
        const JPH::ContactManifold& inManifold,
        JPH::ContactSettings&       ioSettings
    ) override;

    virtual void OnContactPersisted(
        const JPH::Body&            inBody1,
        const JPH::Body&            inBody2,
        const JPH::ContactManifold& inManifold,
        JPH::ContactSettings&       ioSettings
    ) override;

    virtual void OnContactRemoved(const JPH::SubShapeIDPair& inSubShapePair) override;

private:
    uint64_t pair_key(uint32_t a, uint32_t b);
};

// An example activation listener
class MyBodyActivationListener : public JPH::BodyActivationListener {
public:
    virtual void OnBodyActivated(const JPH::BodyID& inBodyID, uint64_t inBodyUserData) override {
        // cout << "A body got activated" << endl;
    }

    virtual void OnBodyDeactivated(const JPH::BodyID& inBodyID, uint64_t inBodyUserData) override {
        // cout << "A body went to sleep" << endl;
    }
};

class PhysicsDebugRenderer : public JPH::DebugRendererSimple {
public:
    class World* world = nullptr;

    virtual void DrawLine(JPH::RVec3Arg inFrom, JPH::RVec3Arg inTo, JPH::ColorArg inColor) override;

    virtual void DrawTriangle(
        JPH::RVec3Arg inV1, JPH::RVec3Arg inV2, JPH::RVec3Arg inV3, JPH::ColorArg inColor, ECastShadow inCastShadow
    ) override;

    virtual void DrawText3D(
        JPH::RVec3Arg inPosition, const std::string_view& inString, JPH::ColorArg inColor, float inHeight
    ) override;
};

class PhysicsSystem {
public:
    JPH::PhysicsSystem system;

    BPLayerInterfaceImpl              broad_phase_layer_interface;
    ObjectVsBroadPhaseLayerFilterImpl object_vs_broad_phase_layer_filter;
    ObjectLayerPairFilterImpl         object_vs_object_layer_filter;

    MyBodyActivationListener body_activation_listener;
    PhysicsContactListener   contact_listener;

    PhysicsDebugRenderer* debug_renderer;

    JPH::TempAllocatorImpl*   temp_allocator;
    JPH::JobSystemThreadPool* job_system;

    const float frame_time = 1.0f / 60.0f;

    PhysicsSystem();

    void update();
    void flush_pending_contacts();

    void initialize(class World* world);

private:
};
