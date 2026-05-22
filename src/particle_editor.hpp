#pragma once

#include "embedded.hpp"
#include "ember.hpp"
#include "particle_effect.hpp"
#include <cereal/cereal.hpp>

enum class NodeType {
    VALUE,
    MULT,
    DIV,
    ADD,
    SUB,
    SIN,
    COS,
    LERP,
    RAND,
    SWIZZLE_X,
    SWIZZLE_Y,
    SWIZZLE_Z,
    SWIZZLE_W,
    COMPOSE_VEC3,
    COMPOSE_VEC4,
    READ_POSITION,
    READ_VELOCITY,
    READ_LIFETIME,
    READ_AGE,
    READ_COLOR,
    READ_ROTATION,
    READ_SIZE,
    READ_DELTA_TIME,
    READ_TIME,
    READ_PARTICLE_ID,
    SPAWN,
    UPDATE,
};

struct NodeDefinition {
    const char*              name;
    ImU32                    color;
    const char*              icon;
    ParticleOp               op;
    std::vector<std::string> input_names;
    bool                     is_sink;
};

extern const std::unordered_map<NodeType, NodeDefinition> node_defs;

struct Pin {
    int         id = 0;
    std::string name;
    int         slot         = 0;
    glm::vec4   constant     = {}; // fallback value when unconnected
    int         connected_to = -1; // output pin id driving this input, -1 = none
};

struct Node {
    int              id   = 0;
    NodeType         type = NodeType::VALUE;
    std::string      name;
    ImVec2           position = {};
    std::vector<Pin> inputs;
    std::vector<Pin> outputs;
};

struct ParticleEffectSaveData {
    struct SavedPin {
        int         id;
        int         slot;
        std::string name;
        glm::vec4   constant;
        int         connected_to;

        template <class Archive> void serialize(Archive& ar, const uint32_t version) {
            ar(CEREAL_NVP(id), CEREAL_NVP(slot), CEREAL_NVP(name), CEREAL_NVP(constant), CEREAL_NVP(connected_to));
        }
    };

    struct SavedNode {
        int                   id;
        NodeType              type;
        ImVec2                position;
        std::vector<SavedPin> inputs;
        std::vector<SavedPin> outputs;

        template <class Archive> void serialize(Archive& ar, const uint32_t version) {
            ar(CEREAL_NVP(id), CEREAL_NVP(type), CEREAL_NVP(position), CEREAL_NVP(inputs), CEREAL_NVP(outputs));
        }
    };

    struct SavedEmitter {
        std::string            name;
        std::vector<SavedNode> nodes;

        template <class Archive> void serialize(Archive& ar, const uint32_t version) {
            ar(CEREAL_NVP(name), CEREAL_NVP(nodes));
        }
    };

    std::vector<SavedEmitter> emitters;
    std::string               editor_state;

    template <class Archive> void serialize(Archive& ar, const uint32_t version) {
        ar(CEREAL_NVP(emitters), CEREAL_NVP(editor_state));
    }
};

CEREAL_CLASS_VERSION(ParticleEffectSaveData::SavedPin, 0)
CEREAL_CLASS_VERSION(ParticleEffectSaveData::SavedNode, 0)
CEREAL_CLASS_VERSION(ParticleEffectSaveData::SavedEmitter, 0)
CEREAL_CLASS_VERSION(ParticleEffectSaveData, 0)

class ParticleEditor {
public:
    ParticleEditor();
    ~ParticleEditor();

    ParticleEditor(const ParticleEditor&)            = delete;
    ParticleEditor& operator=(const ParticleEditor&) = delete;

    void                   render(class AssetExporter* exporter);
    void                   load(const ParticleEffectSaveData& data, std::string asset_name);
    ParticleEffectSaveData save();

    static constexpr NodeType constructible[] = {
        NodeType::VALUE,
        NodeType::ADD,
        NodeType::SUB,
        NodeType::MULT,
        NodeType::DIV,
        NodeType::SIN,
        NodeType::COS,
        NodeType::LERP,
        NodeType::RAND,
        NodeType::SWIZZLE_X,
        NodeType::SWIZZLE_Y,
        NodeType::SWIZZLE_Z,
        NodeType::SWIZZLE_W,
        NodeType::COMPOSE_VEC3,
        NodeType::COMPOSE_VEC4,
        NodeType::READ_POSITION,
        NodeType::READ_VELOCITY,
        NodeType::READ_LIFETIME,
        NodeType::READ_AGE,
        NodeType::READ_COLOR,
        NodeType::READ_ROTATION,
        NodeType::READ_SIZE,
        NodeType::READ_DELTA_TIME,
        NodeType::READ_TIME,
        NodeType::READ_PARTICLE_ID,
    };

private:
    struct EmitterGraph {
        std::string                   name;
        std::unordered_map<int, Node> nodes;
    };

    std::vector<EmitterGraph> emitters;
    int                       active_emitter = 0;
    int                       next_id        = 1;

    ax::NodeEditor::Config         config;
    ax::NodeEditor::EditorContext* context = nullptr;

    std::string current_asset_name;
    std::string editor_state_blob;

    float splitter_area   = 0.0f;
    float left_pane_size  = 0.0f;
    float right_pane_size = 0.0f;

    bool                  open_new_node_popup = false;
    ax::NodeEditor::PinId pending_pin         = {};
    ImVec2                node_spawn_pos      = {};

    struct CompileError {
        std::string message;
        int         node_id = -1;
    };

    struct CompileResult {
        std::vector<ParticleInstruction> instructions;
        std::vector<glm::vec4>           constants;
        std::vector<CompileError>        errors;
    };

    int alloc_id() {
        return next_id++;
    }

    Node& create_node(EmitterGraph& emitter, NodeType type);
    void  remove_node(EmitterGraph& emitter, int node_id);
    void  create_default_nodes(EmitterGraph& emitter);

    Pin*  find_pin(EmitterGraph& emitter, int pin_id);
    Node* node_owning_pin(EmitterGraph& emitter, int pin_id);
    bool  pin_is_output(EmitterGraph& emitter, int pin_id);

    static ax::NodeEditor::LinkId make_link_id(int from_output, int to_input);
    static std::pair<int, int>    split_link_id(ax::NodeEditor::LinkId id);

    CompileResult compile(EmitterGraph& emitter, NodeType root_type);

    void render_left_pane(class AssetExporter* exporter);
    void render_graph();
    void render_new_node_popup(const char* popup_id, bool connect_pending);
    void render_node(const Node& node);
    void render_node_header(const Node& node, ImU32 color);
    void render_input_pins(const Node& node, ImU32 color);
    void render_output_pins(EmitterGraph& emitter, const Node& node, ImU32 color);
};
