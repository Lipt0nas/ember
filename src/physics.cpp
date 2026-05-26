#include "physics.hpp"

#include "world.hpp"

PhysicsSystem::PhysicsSystem() {
    spdlog::info("Initializing physics system");
    JPH::RegisterDefaultAllocator();
    JPH::Factory::sInstance = new JPH::Factory();
    JPH::RegisterTypes();

    temp_allocator = new JPH::TempAllocatorImpl(10 * 1024 * 1024);
    job_system     = new JPH::JobSystemThreadPool(
        JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, std::thread::hardware_concurrency() - 1
    );

    const uint32_t physics_max_bodies             = UINT16_MAX;
    const uint32_t physics_num_body_mutexes       = 0;
    const uint32_t physica_max_body_pairs         = UINT16_MAX;
    const uint32_t physics_max_contact_contraints = 10240;

    system.Init(
        physics_max_bodies,
        physics_num_body_mutexes,
        physica_max_body_pairs,
        physics_max_contact_contraints,
        broad_phase_layer_interface,
        object_vs_broad_phase_layer_filter,
        object_vs_object_layer_filter
    );

    system.SetBodyActivationListener(&body_activation_listener);
    system.SetContactListener(&contact_listener);

    debug_renderer = new PhysicsDebugRenderer();
}

void PhysicsSystem::update() {
    system.Update(frame_time, 1, temp_allocator, job_system);
}

void PhysicsSystem::initialize(class World* world) {
    contact_listener.world = world;
    debug_renderer->world  = world;
}

JPH::ValidateResult PhysicsContactListener::OnContactValidate(
    const JPH::Body&               inBody1,
    const JPH::Body&               inBody2,
    JPH::RVec3Arg                  inBaseOffset,
    const JPH::CollideShapeResult& inCollisionResult
) {
    // Allows you to ignore a contact before it is created (using layers to not make objects collide is cheaper!)
    return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
}

void PhysicsContactListener::OnContactAdded(
    const JPH::Body&            inBody1,
    const JPH::Body&            inBody2,
    const JPH::ContactManifold& inManifold,
    JPH::ContactSettings&       ioSettings
) {
    const uint32_t id1 = static_cast<uint32_t>(inBody1.GetUserData());
    const uint32_t id2 = static_cast<uint32_t>(inBody2.GetUserData());
    const uint64_t key = pair_key(id1, id2);

    ContactAddedEvent ev{
        .body_1 = id1,
        .body_2 = id2,
        .normal =
            {
                inManifold.mWorldSpaceNormal.GetX(),
                inManifold.mWorldSpaceNormal.GetY(),
                inManifold.mWorldSpaceNormal.GetZ(),
            },
        .penetration_depth = inManifold.mPenetrationDepth,
        .impact_speed  = (inBody1.GetLinearVelocity() - inBody2.GetLinearVelocity()).Dot(inManifold.mWorldSpaceNormal),
        .contact_point = {
            inManifold.GetWorldSpaceContactPointOn1(0).GetX(),
            inManifold.GetWorldSpaceContactPointOn1(0).GetY(),
            inManifold.GetWorldSpaceContactPointOn1(0).GetZ(),
        },
    };

    const std::lock_guard<std::mutex> lock(pair_mutex);
    active_pairs.insert(key);
    pair_data.try_emplace(key, ev);
}

void PhysicsContactListener::OnContactPersisted(
    const JPH::Body&            inBody1,
    const JPH::Body&            inBody2,
    const JPH::ContactManifold& inManifold,
    JPH::ContactSettings&       ioSettings
) {
    const uint32_t id1 = static_cast<uint32_t>(inBody1.GetUserData());
    const uint32_t id2 = static_cast<uint32_t>(inBody2.GetUserData());

    const std::lock_guard<std::mutex> lock(pair_mutex);
    active_pairs.insert(pair_key(id1, id2));
}

void PhysicsContactListener::OnContactRemoved(const JPH::SubShapeIDPair& inSubShapePair) {
}

void PhysicsSystem::flush_pending_contacts() {
    std::unordered_set<uint64_t>                    current_pairs;
    std::unordered_map<uint64_t, ContactAddedEvent> data_snapshot;
    {
        const std::lock_guard<std::mutex> lock(contact_listener.pair_mutex);
        std::swap(current_pairs, contact_listener.active_pairs);
        std::swap(data_snapshot, contact_listener.pair_data);
    }

    for (const uint64_t key : current_pairs) {
        if (contact_listener.prev_pairs.contains(key)) {
            continue;
        }

        const auto it = data_snapshot.find(key);
        if (it == data_snapshot.end()) {
            continue;
        }

        const ContactAddedEvent& ev = it->second;

        contact_listener.world->script.issue_event(ev);

        if (auto* s = contact_listener.world->scene.get_component<components::Script>((entt::entity)ev.body_1)) {
            contact_listener.world->script.call_on_collision_started(
                (Entity)ev.body_1,
                *s,
                CollisionStarted{
                    .other             = ev.body_2,
                    .normal            = ev.normal,
                    .penetration_depth = ev.penetration_depth,
                    .impact_speed      = ev.impact_speed,
                    .contact_point     = ev.contact_point,
                }
            );
        }

        if (auto* s = contact_listener.world->scene.get_component<components::Script>((entt::entity)ev.body_2)) {
            contact_listener.world->script.call_on_collision_started(
                (Entity)ev.body_2,
                *s,
                CollisionStarted{
                    .other             = ev.body_1,
                    .normal            = ev.normal,
                    .penetration_depth = ev.penetration_depth,
                    .impact_speed      = ev.impact_speed,
                    .contact_point     = ev.contact_point,
                }
            );
        }
    }

    auto& body_interface = contact_listener.world->physics.system.GetBodyInterface();

    auto get_body_id = [&](uint32_t entity_id) -> std::optional<JPH::BodyID> {
        auto* phys = contact_listener.world->scene.get_component<components::Physics>((entt::entity)entity_id);
        if (!phys) {
            return std::nullopt;
        }

        return phys->body_id;
    };

    auto is_sleeping_static = [&](JPH::BodyID bid) {
        return body_interface.GetMotionType(bid) == JPH::EMotionType::Dynamic && !body_interface.IsActive(bid);
    };

    for (const uint64_t key : contact_listener.prev_pairs) {
        if (current_pairs.contains(key)) {
            continue;
        }

        const uint32_t id1 = static_cast<uint32_t>(key >> 32);
        const uint32_t id2 = static_cast<uint32_t>(key & 0xFFFFFFFF);

        const auto body_id1 = get_body_id(id1);
        const auto body_id2 = get_body_id(id2);

        if (!body_id1 || !body_id2) {
            continue;
        }

        if (is_sleeping_static(*body_id1) || is_sleeping_static(*body_id2)) {
            current_pairs.insert(key);
            continue;
        }

        const ContactRemovedEvent ev{.body_1 = id1, .body_2 = id2};
        contact_listener.world->script.issue_event(ev);

        if (auto* s = contact_listener.world->scene.get_component<components::Script>((entt::entity)id1)) {
            contact_listener.world->script.call_on_collision_ended((Entity)id1, *s, CollisionEnded{.other = id2});
        }

        if (auto* s = contact_listener.world->scene.get_component<components::Script>((entt::entity)id2)) {
            contact_listener.world->script.call_on_collision_ended((Entity)id2, *s, CollisionEnded{.other = id1});
        }
    }

    contact_listener.prev_pairs = std::move(current_pairs);
}

uint64_t PhysicsContactListener::pair_key(uint32_t a, uint32_t b) {
    if (a > b) {
        std::swap(a, b);
    }

    return (uint64_t(a) << 32) | b;
}

void PhysicsDebugRenderer::DrawLine(JPH::RVec3Arg inFrom, JPH::RVec3Arg inTo, JPH::ColorArg inColor) {
    world->renderer.debug_renderer_draw_line(
        world->renderer.debug_renderer,
        {inFrom.GetX(), inFrom.GetY(), inFrom.GetZ()},
        {inTo.GetX(), inTo.GetY(), inTo.GetZ()},
        {inColor.r, inColor.g, inColor.b}
    );
}

void PhysicsDebugRenderer::DrawTriangle(
    JPH::RVec3Arg inV1, JPH::RVec3Arg inV2, JPH::RVec3Arg inV3, JPH::ColorArg inColor, ECastShadow inCastShadow
) {
    world->renderer.debug_renderer_draw_line(
        world->renderer.debug_renderer,
        {inV1.GetX(), inV1.GetY(), inV1.GetZ()},
        {inV2.GetX(), inV2.GetY(), inV2.GetZ()},
        {inColor.r, inColor.g, inColor.b}
    );

    world->renderer.debug_renderer_draw_line(
        world->renderer.debug_renderer,
        {inV2.GetX(), inV2.GetY(), inV2.GetZ()},
        {inV3.GetX(), inV3.GetY(), inV3.GetZ()},
        {inColor.r, inColor.g, inColor.b}
    );

    world->renderer.debug_renderer_draw_line(
        world->renderer.debug_renderer,
        {inV3.GetX(), inV3.GetY(), inV3.GetZ()},
        {inV1.GetX(), inV1.GetY(), inV1.GetZ()},
        {inColor.r, inColor.g, inColor.b}
    );
}

void PhysicsDebugRenderer::DrawText3D(
    JPH::RVec3Arg inPosition, const std::string_view& inString, JPH::ColorArg inColor, float inHeight
) {
}
