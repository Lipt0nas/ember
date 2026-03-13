#include "particle_editor.hpp"

#include "asset_exporter.hpp"
#include "imgui_internal.h"
#include "ui.hpp"

const std::unordered_map<NodeType, NodeDefinition> node_defs = {
    // Values
    {NodeType::VALUE, {"Value", IM_COL32(180, 180, 180, 255), ICON_FA_HASHTAG, ParticleOp::CONST, {}, false}},
    // Math
    {NodeType::ADD, {"Add", IM_COL32(70, 120, 180, 255), ICON_FA_PLUS, ParticleOp::ADD, {"A", "B"}, false}},
    {NodeType::SUB, {"Subtract", IM_COL32(70, 120, 180, 255), ICON_FA_MINUS, ParticleOp::SUB, {"A", "B"}, false}},
    {NodeType::MULT, {"Multiply", IM_COL32(70, 120, 180, 255), ICON_FA_CROSS, ParticleOp::MULT, {"A", "B"}, false}},
    {NodeType::DIV, {"Divide", IM_COL32(70, 120, 180, 255), ICON_FA_DIVIDE, ParticleOp::DIV, {"A", "B"}, false}},
    {NodeType::SIN, {"Sin", IM_COL32(140, 80, 180, 255), ICON_FA_WAVE_SQUARE, ParticleOp::SIN, {"Input"}, false}},
    {NodeType::COS, {"Cos", IM_COL32(140, 80, 180, 255), ICON_FA_WAVE_SQUARE, ParticleOp::COS, {"Input"}, false}},
    {NodeType::LERP,
     {"Interpolate",
      IM_COL32(60, 150, 100, 255),
      ICON_FA_LONG_ARROW_ALT_LEFT,
      ParticleOp::LERP,
      {"A", "B", "T"},
      false}},
    {NodeType::RAND, {"Random", IM_COL32(180, 140, 40, 255), ICON_FA_SHARE, ParticleOp::RAND, {"Min", "Max"}, false}},
    // Swizzle
    {NodeType::SWIZZLE_X,
     {"Swizzle X", IM_COL32(180, 80, 120, 255), ICON_FA_SLASH, ParticleOp::SWIZZLE_X, {"Vec"}, false}},
    {NodeType::SWIZZLE_Y,
     {"Swizzle Y", IM_COL32(180, 80, 120, 255), ICON_FA_SLASH, ParticleOp::SWIZZLE_Y, {"Vec"}, false}},
    {NodeType::SWIZZLE_Z,
     {"Swizzle Z", IM_COL32(180, 80, 120, 255), ICON_FA_SLASH, ParticleOp::SWIZZLE_Z, {"Vec"}, false}},
    {NodeType::SWIZZLE_W,
     {"Swizzle W", IM_COL32(180, 80, 120, 255), ICON_FA_SLASH, ParticleOp::SWIZZLE_W, {"Vec"}, false}},
    // Compose
    {NodeType::COMPOSE_VEC3,
     {"Compose Vec3",
      IM_COL32(100, 160, 100, 255),
      ICON_FA_OBJECT_GROUP,
      ParticleOp::COMPOSE_VEC3,
      {"X", "Y", "Z"},
      false}},
    {NodeType::COMPOSE_VEC4,
     {"Compose Vec4",
      IM_COL32(100, 160, 100, 255),
      ICON_FA_OBJECT_GROUP,
      ParticleOp::COMPOSE_VEC4,
      {"X", "Y", "Z", "W"},
      false}},
    // Read particle attributes
    {NodeType::READ_POSITION,
     {"Read Position", IM_COL32(180, 80, 60, 255), ICON_FA_DATABASE, ParticleOp::LOAD_ATTRIB, {}, false}},
    {NodeType::READ_VELOCITY,
     {"Read Velocity", IM_COL32(180, 80, 60, 255), ICON_FA_DATABASE, ParticleOp::LOAD_ATTRIB, {}, false}},
    {NodeType::READ_LIFETIME,
     {"Read Lifetime", IM_COL32(180, 80, 60, 255), ICON_FA_DATABASE, ParticleOp::LOAD_ATTRIB, {}, false}},
    {NodeType::READ_AGE,
     {"Read Age", IM_COL32(180, 80, 60, 255), ICON_FA_DATABASE, ParticleOp::LOAD_ATTRIB, {}, false}},
    {NodeType::READ_COLOR,
     {"Read Color", IM_COL32(180, 80, 60, 255), ICON_FA_DATABASE, ParticleOp::LOAD_ATTRIB, {}, false}},
    {NodeType::READ_ROTATION,
     {"Read Rotation", IM_COL32(180, 80, 60, 255), ICON_FA_DATABASE, ParticleOp::LOAD_ATTRIB, {}, false}},
    {NodeType::READ_SIZE,
     {"Read Size", IM_COL32(180, 80, 60, 255), ICON_FA_DATABASE, ParticleOp::LOAD_ATTRIB, {}, false}},
    // Read simulation params
    {NodeType::READ_DELTA_TIME,
     {"Read Delta Time", IM_COL32(60, 160, 180, 255), ICON_FA_CLOCK, ParticleOp::LOAD_PARAM, {}, false}},
    {NodeType::READ_TIME, {"Read Time", IM_COL32(60, 160, 180, 255), ICON_FA_CLOCK, ParticleOp::LOAD_PARAM, {}, false}},
    {NodeType::READ_PARTICLE_ID,
     {"Read Particle ID", IM_COL32(60, 160, 180, 255), ICON_FA_FINGERPRINT, ParticleOp::LOAD_PARAM, {}, false}},
    // Sinks
    {NodeType::SPAWN,
     {"Spawn Node",
      IM_COL32(220, 120, 40, 255),
      ICON_FA_CIRCLE_NOTCH,
      ParticleOp::STORE_ATTRIB,
      {"Position", "Velocity", "Lifetime", "Color", "Rotation", "Size"},
      true}},
    {NodeType::UPDATE,
     {"Update Node",
      IM_COL32(40, 180, 160, 255),
      ICON_FA_ROUTE,
      ParticleOp::STORE_ATTRIB,
      {"Position", "Velocity", "Lifetime", "Color", "Rotation", "Size"},
      true}},
};

static uint16_t attrib_index(NodeType t) {
    switch (t) {
    case NodeType::READ_POSITION:
        return Particle::POSITION;
    case NodeType::READ_VELOCITY:
        return Particle::VELOCITY;
    case NodeType::READ_LIFETIME:
        return Particle::LIFETIME;
    case NodeType::READ_AGE:
        return Particle::AGE;
    case NodeType::READ_COLOR:
        return Particle::COLOR;
    case NodeType::READ_ROTATION:
        return Particle::ROTATION;
    case NodeType::READ_SIZE:
        return Particle::SIZE;
    default:
        return 0;
    }
}

static uint16_t param_index(NodeType t) {
    switch (t) {
    case NodeType::READ_DELTA_TIME:
        return (uint16_t)SimParam::DELTA_TIME;
    case NodeType::READ_TIME:
        return (uint16_t)SimParam::TIME;
    case NodeType::READ_PARTICLE_ID:
        return (uint16_t)SimParam::PARTICLE_ID;
    default:
        return 0;
    }
}

static constexpr uint16_t sink_slots[] = {
    Particle::POSITION,
    Particle::VELOCITY,
    Particle::LIFETIME,
    Particle::COLOR,
    Particle::ROTATION,
    Particle::SIZE,
};

ParticleEditor::ParticleEditor() {
    config.SettingsFile = nullptr;
    config.UserPointer  = this;

    config.SaveSettings = [](const char* data, size_t size, ax::NodeEditor::SaveReasonFlags reason, void* ptr) -> bool {
        auto interesting = ax::NodeEditor::SaveReasonFlags::AddNode | ax::NodeEditor::SaveReasonFlags::RemoveNode |
                           ax::NodeEditor::SaveReasonFlags::Position;
        if ((int)(reason & interesting) == 0) {
            return false;
        }

        static_cast<ParticleEditor*>(ptr)->editor_state_blob = std::string(data, size);
        return true;
    };

    config.LoadSettings = [](char* data, void* ptr) -> size_t {
        auto& blob = static_cast<ParticleEditor*>(ptr)->editor_state_blob;
        if (blob.empty()) {
            return 0;
        }

        if (data) {
            memcpy(data, blob.data(), blob.size());
        }

        return blob.size();
    };

    context = ax::NodeEditor::CreateEditor(&config);

    emitters.push_back({"Emitter 1", {}});
    create_default_nodes(emitters[0]);
}

ParticleEditor::~ParticleEditor() {
    ax::NodeEditor::DestroyEditor(context);
}

Node& ParticleEditor::create_node(EmitterGraph& emitter, NodeType type) {
    const auto& def = node_defs.at(type);

    Node node;
    node.id   = alloc_id();
    node.type = type;
    node.name = def.name;

    for (int i = 0; i < (int)def.input_names.size(); i++) {
        Pin p;
        p.id   = alloc_id();
        p.name = def.input_names[i];
        p.slot = def.is_sink ? (int)sink_slots[i] : i;

        if (type == NodeType::RAND && i == 1) {
            p.constant = glm::vec4(1.0f);
        }

        node.inputs.push_back(std::move(p));
    }

    if (!def.is_sink) {
        Pin p;
        p.id   = alloc_id();
        p.name = "Output";
        p.slot = 0;

        if (def.op == ParticleOp::LOAD_ATTRIB) {
            p.constant.x = (float)attrib_index(type);
        }

        if (def.op == ParticleOp::LOAD_PARAM) {
            p.constant.x = (float)param_index(type);
        }

        node.outputs.push_back(std::move(p));
    }

    int id = node.id;
    emitter.nodes.emplace(id, std::move(node));
    return emitter.nodes.at(id);
}

void ParticleEditor::remove_node(EmitterGraph& emitter, int node_id) {
    auto it = emitter.nodes.find(node_id);
    if (it == emitter.nodes.end()) {
        return;
    }

    if (!it->second.outputs.empty()) {
        int dead_output = it->second.outputs[0].id;
        for (auto& [id, n] : emitter.nodes) {
            for (auto& pin : n.inputs) {
                if (pin.connected_to == dead_output) {
                    pin.connected_to = -1;
                }
            }
        }
    }

    emitter.nodes.erase(it);
}

void ParticleEditor::create_default_nodes(EmitterGraph& emitter) {
    create_node(emitter, NodeType::SPAWN);
    create_node(emitter, NodeType::UPDATE);
}

Pin* ParticleEditor::find_pin(EmitterGraph& emitter, int pin_id) {
    for (auto& [id, node] : emitter.nodes) {
        for (auto& p : node.inputs) {
            if (p.id == pin_id) {
                return &p;
            }
        }

        for (auto& p : node.outputs) {
            if (p.id == pin_id) {
                return &p;
            }
        }
    }

    return nullptr;
}

Node* ParticleEditor::node_owning_pin(EmitterGraph& emitter, int pin_id) {
    for (auto& [id, node] : emitter.nodes) {
        for (auto& p : node.inputs) {
            if (p.id == pin_id) {
                return &node;
            }
        }

        for (auto& p : node.outputs) {
            if (p.id == pin_id) {
                return &node;
            }
        }
    }

    return nullptr;
}

bool ParticleEditor::pin_is_output(EmitterGraph& emitter, int pin_id) {
    for (auto& [id, node] : emitter.nodes) {
        for (auto& p : node.outputs) {
            if (p.id == pin_id) {
                return true;
            }
        }
    }

    return false;
}

ax::NodeEditor::LinkId ParticleEditor::make_link_id(int from_output, int to_input) {
    return (uintptr_t)(uint32_t)from_output << 32 | (uintptr_t)(uint32_t)to_input;
}

std::pair<int, int> ParticleEditor::split_link_id(ax::NodeEditor::LinkId id) {
    uintptr_t raw = (uintptr_t)id.Get();
    return {(int)(uint32_t)(raw >> 32), (int)(uint32_t)(raw & 0xFFFFFFFF)};
}

ParticleEditor::CompileResult ParticleEditor::compile(EmitterGraph& emitter, NodeType root_type) {
    CompileResult result;

    Node* root = nullptr;
    for (auto& [id, n] : emitter.nodes) {
        if (n.type == root_type) {
            root = &n;
            break;
        }
    }

    if (!root) {
        return result;
    }

    std::unordered_map<int, Node*> output_pin_to_node;
    for (auto& [id, n] : emitter.nodes) {
        for (auto& p : n.outputs) {
            output_pin_to_node[p.id] = &n;
        }
    }

    std::unordered_set<int> visited;
    std::vector<Node*>      order;

    std::function<void(Node*)> dfs = [&](Node* node) {
        if (!visited.insert(node->id).second) {
            return;
        }

        for (const auto& pin : node->inputs) {
            if (pin.connected_to < 0) {
                continue;
            }

            auto it = output_pin_to_node.find(pin.connected_to);
            if (it != output_pin_to_node.end()) {
                dfs(it->second);
            }
        }

        order.push_back(node);
    };

    for (const auto& pin : root->inputs) {
        if (pin.connected_to < 0) {
            continue;
        }

        auto it = output_pin_to_node.find(pin.connected_to);
        if (it != output_pin_to_node.end()) {
            dfs(it->second);
        }
    }

    std::unordered_map<int, uint16_t> reg;
    uint16_t                          next_reg = 0;

    auto assign = [&](int pin_id) -> uint16_t {
        auto [it, inserted] = reg.emplace(pin_id, next_reg);
        if (inserted) {
            ++next_reg;
        }

        return it->second;
    };

    for (Node* node : order) {
        for (auto& p : node->inputs) {
            assign(p.id);
        }
        for (auto& p : node->outputs) {
            assign(p.id);
        }
    }

    for (const auto& pin : root->inputs) {
        assign(pin.id);
        if (pin.connected_to >= 0) {
            assign(pin.connected_to);
        }
    }

    result.constants.assign(next_reg, glm::vec4(0.0f));

    for (Node* node : order) {
        if (node->type == NodeType::VALUE) {
            result.constants[reg.at(node->outputs[0].id)] = node->outputs[0].constant;
        }

        for (const auto& p : node->inputs) {
            if (p.connected_to < 0) {
                result.constants[reg.at(p.id)] = p.constant;
            }
        }
    }

    for (const auto& pin : root->inputs) {
        if (pin.connected_to < 0) {
            result.constants[reg.at(pin.id)] = pin.constant;
        }
    }

    auto src = [&](const Node* node, int i) -> uint16_t {
        const Pin& p = node->inputs[i];

        int  key = p.connected_to >= 0 ? p.connected_to : p.id;
        auto it  = reg.find(key);
        if (it == reg.end()) {
            result.errors.push_back({"pin not assigned a register", node->id});
            return 0;
        }
        return it->second;
    };

    for (Node* node : order) {
        const uint16_t   dst = node->outputs.empty() ? 0 : reg.at(node->outputs[0].id);
        const ParticleOp op  = node_defs.at(node->type).op;

        switch (node->type) {
        case NodeType::VALUE:
            result.instructions.push_back({.op = ParticleOp::CONST, .dst = dst, .imm = dst});
            break;

        case NodeType::READ_POSITION:
        case NodeType::READ_VELOCITY:
        case NodeType::READ_LIFETIME:
        case NodeType::READ_AGE:
        case NodeType::READ_COLOR:
        case NodeType::READ_ROTATION:
        case NodeType::READ_SIZE:
            result.instructions.push_back(
                {.op = ParticleOp::LOAD_ATTRIB, .dst = dst, .srcs = {(uint16_t)(int)node->outputs[0].constant.x}}
            );
            break;

        case NodeType::READ_DELTA_TIME:
        case NodeType::READ_TIME:
        case NodeType::READ_PARTICLE_ID:
            result.instructions.push_back(
                {.op = ParticleOp::LOAD_PARAM, .dst = dst, .imm = (uint16_t)(int)node->outputs[0].constant.x}
            );
            break;

        case NodeType::ADD:
        case NodeType::SUB:
        case NodeType::MULT:
        case NodeType::DIV:
        case NodeType::RAND:
            result.instructions.push_back({.op = op, .dst = dst, .srcs = {src(node, 0), src(node, 1)}});
            break;

        case NodeType::SIN:
        case NodeType::COS:
        case NodeType::SWIZZLE_X:
        case NodeType::SWIZZLE_Y:
        case NodeType::SWIZZLE_Z:
        case NodeType::SWIZZLE_W:
            result.instructions.push_back({.op = op, .dst = dst, .srcs = {src(node, 0)}});
            break;

        case NodeType::LERP:
        case NodeType::COMPOSE_VEC3:
            result.instructions.push_back({.op = op, .dst = dst, .srcs = {src(node, 0), src(node, 1), src(node, 2)}});
            break;

        case NodeType::COMPOSE_VEC4:
            result.instructions.push_back(
                {.op = op, .dst = dst, .srcs = {src(node, 0), src(node, 1), src(node, 2), src(node, 3)}}
            );
            break;

        default:
            break;
        }
    }

    for (const auto& pin : root->inputs) {
        if (pin.connected_to >= 0) {
            result.instructions.push_back(
                {.op = ParticleOp::STORE_ATTRIB, .srcs = {(uint16_t)pin.slot}, .imm = reg.at(pin.connected_to)}
            );
        }
    }

    return result;
}

void ParticleEditor::render(AssetExporter* exporter) {
    constexpr float splitter_size = 6.0f;
    const auto      available     = ImGui::GetContentRegionAvail();

    if (splitter_area != available.x) {
        if (splitter_area == 0.0f) {
            left_pane_size = ImFloor(available.x * 0.25f);
        } else {
            left_pane_size = left_pane_size * (available.x / splitter_area);
        }

        splitter_area   = available.x;
        right_pane_size = available.x - left_pane_size - splitter_size;
    }

    imgui_splitter(true, splitter_size, &left_pane_size, &right_pane_size, 100.0f, 100.0f);

    ImGui::BeginChild("##left", ImVec2(left_pane_size, -1), false);
    render_left_pane(exporter);
    ImGui::EndChild();

    if (current_asset_name.empty()) {
        return;
    }

    ImGui::SameLine(0.0f, splitter_size);
    ax::NodeEditor::SetCurrentEditor(context);
    ax::NodeEditor::Begin("Particle Editor", ImVec2(right_pane_size, 0));
    render_graph();
    ax::NodeEditor::End();
}

void ParticleEditor::render_left_pane(AssetExporter* exporter) {
    ImGui::Text("Particle Effect: %s", current_asset_name.c_str());
    ImGui::Separator();

    ImGui::Text("Emitters");
    ImGui::Spacing();

    for (int i = 0; i < (int)emitters.size(); i++) {
        ImGui::PushID(i);

        bool selected = (i == active_emitter);
        if (ImGui::Selectable(emitters[i].name.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick)) {
            active_emitter = i;
        }

        if (selected && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            ImGui::OpenPopup("RenameEmitter");
        }

        if (ImGui::BeginPopup("RenameEmitter")) {
            static char rename_buf[128] = {};
            if (ImGui::IsWindowAppearing()) {
                strncpy(rename_buf, emitters[i].name.c_str(), sizeof(rename_buf) - 1);
            }

            ImGui::SetNextItemWidth(180.0f);
            if (ImGui::InputText("##rename", rename_buf, sizeof(rename_buf), ImGuiInputTextFlags_EnterReturnsTrue)) {
                emitters[i].name = rename_buf;
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        ImGui::PopID();
    }

    ImGui::Spacing();

    if (ImGui::Button("Add Emitter")) {
        emitters.push_back({"Emitter " + std::to_string(emitters.size() + 1), {}});
        active_emitter = (int)emitters.size() - 1;
        create_default_nodes(emitters[active_emitter]);
    }

    if (emitters.size() > 1) {
        ImGui::SameLine();

        if (ImGui::Button("Remove Emitter")) {
            emitters.erase(emitters.begin() + active_emitter);
            active_emitter = std::max(0, active_emitter - 1);
        }
    }

    ImGui::Separator();

    if (ImGui::Button("Save Current Effect")) {
        ParticleEffectAsset asset;

        for (auto& emitter : emitters) {
            auto spawn_result  = compile(emitter, NodeType::SPAWN);
            auto update_result = compile(emitter, NodeType::UPDATE);

            for (auto& e : spawn_result.errors) {
                spdlog::warn("[{}] Spawn compile error (node {}): {}", emitter.name, e.node_id, e.message);
            }

            for (auto& e : update_result.errors) {
                spdlog::warn("[{}] Update compile error (node {}): {}", emitter.name, e.node_id, e.message);
            }

            spdlog::info(
                "[{}] {} spawn registers / {} instructions,  {} update registers / {} instructions",
                emitter.name,
                spawn_result.constants.size(),
                spawn_result.instructions.size(),
                update_result.constants.size(),
                update_result.instructions.size()
            );

            asset.emitters.push_back(
                ParticleEmitterAsset{
                    .name                  = emitter.name,
                    .spawn_register_state  = spawn_result.constants,
                    .spawn_instructions    = spawn_result.instructions,
                    .update_register_state = update_result.constants,
                    .update_instructions   = update_result.instructions,
                }
            );
        }

        exporter->export_particle_effect(save(), asset, current_asset_name);
    }

    ImGui::Separator();
}

void ParticleEditor::render_graph() {
    if (emitters.empty()) {
        return;
    }

    EmitterGraph& current = emitters[active_emitter];

    for (const auto& [id, node] : current.nodes) {
        render_node(node);
    }

    for (const auto& [id, node] : current.nodes) {
        for (const auto& pin : node.inputs) {
            if (pin.connected_to >= 0) {
                ax::NodeEditor::Link(make_link_id(pin.connected_to, pin.id), pin.connected_to, pin.id);
            }
        }
    }

    if (ax::NodeEditor::BeginCreate()) {
        ax::NodeEditor::PinId a;
        ax::NodeEditor::PinId b;
        if (ax::NodeEditor::QueryNewLink(&a, &b)) {
            int  aid      = (int)a.Get();
            int  bid      = (int)b.Get();
            bool a_is_out = pin_is_output(current, aid);
            bool b_is_out = pin_is_output(current, bid);

            if (a_is_out != b_is_out) {
                int   out_id    = a_is_out ? aid : bid;
                int   in_id     = a_is_out ? bid : aid;
                Pin*  in_pin    = find_pin(current, in_id);
                Node* from_node = node_owning_pin(current, out_id);
                Node* to_node   = node_owning_pin(current, in_id);

                bool blocked = !in_pin || !from_node || !to_node || from_node == to_node || in_pin->connected_to >= 0;

                if (!blocked) {
                    if (ax::NodeEditor::AcceptNewItem()) {
                        in_pin->connected_to = out_id;
                    }
                } else {
                    ax::NodeEditor::RejectNewItem(ImColor(255, 0, 0), 2.0f);
                }
            }
        }

        ax::NodeEditor::PinId dragged;
        if (ax::NodeEditor::QueryNewNode(&dragged)) {
            if (ax::NodeEditor::AcceptNewItem()) {
                pending_pin         = dragged;
                node_spawn_pos      = ImGui::GetMousePos();
                open_new_node_popup = true;
            }
        }
    }
    ax::NodeEditor::EndCreate();

    if (ax::NodeEditor::BeginDelete()) {
        ax::NodeEditor::NodeId del_node;
        while (ax::NodeEditor::QueryDeletedNode(&del_node)) {
            if (ax::NodeEditor::AcceptDeletedItem()) {
                remove_node(current, (int)del_node.Get());
            }
        }

        ax::NodeEditor::LinkId del_link;
        while (ax::NodeEditor::QueryDeletedLink(&del_link)) {
            if (ax::NodeEditor::AcceptDeletedItem()) {
                auto [from, to] = split_link_id(del_link);
                if (Pin* p = find_pin(current, to)) {
                    p->connected_to = -1;
                }
            }
        }
    }
    ax::NodeEditor::EndDelete();

    ax::NodeEditor::Suspend();

    if (open_new_node_popup) {
        ImGui::OpenPopup("NewNodeFromPin");
        open_new_node_popup = false;
    }
    render_new_node_popup("NewNodeFromPin", true);

    if (ax::NodeEditor::ShowBackgroundContextMenu()) {
        ImGui::OpenPopup("NewNode");
        node_spawn_pos = ImGui::GetMousePos();
    }
    render_new_node_popup("NewNode", false);

    ax::NodeEditor::Resume();
}

void ParticleEditor::render_new_node_popup(const char* popup_id, bool connect_pending) {
    if (!ImGui::BeginPopup(popup_id)) {
        return;
    }

    EmitterGraph& current = emitters[active_emitter];

    auto section = [&](const char* label, std::initializer_list<NodeType> types) {
        if (ImGui::TreeNode(label)) {
            ImGui::Indent(10.0f);
            for (NodeType type : types) {
                if (ImGui::MenuItem(node_defs.at(type).name)) {
                    Node& new_node = create_node(current, type);
                    ax::NodeEditor::SetNodePosition(new_node.id, ax::NodeEditor::ScreenToCanvas(node_spawn_pos));

                    if (connect_pending && pending_pin) {
                        int  pid     = (int)pending_pin.Get();
                        bool pid_out = pin_is_output(current, pid);

                        if (pid_out && !new_node.inputs.empty()) {
                            new_node.inputs[0].connected_to = pid;
                        } else if (!pid_out && !new_node.outputs.empty()) {
                            if (Pin* target = find_pin(current, pid)) {
                                target->connected_to = new_node.outputs[0].id;
                            }
                        }
                        pending_pin = {};
                    }
                }
            }
            ImGui::Unindent(10.0f);
            ImGui::TreePop();
        }
        ImGui::Separator();
    };

    section("Values", {NodeType::VALUE});
    section(
        "Math",
        {NodeType::ADD,
         NodeType::SUB,
         NodeType::MULT,
         NodeType::DIV,
         NodeType::SIN,
         NodeType::COS,
         NodeType::LERP,
         NodeType::RAND}
    );
    section("Swizzle", {NodeType::SWIZZLE_X, NodeType::SWIZZLE_Y, NodeType::SWIZZLE_Z, NodeType::SWIZZLE_W});
    section("Compose", {NodeType::COMPOSE_VEC3, NodeType::COMPOSE_VEC4});
    section(
        "Read Particle",
        {NodeType::READ_POSITION,
         NodeType::READ_VELOCITY,
         NodeType::READ_LIFETIME,
         NodeType::READ_AGE,
         NodeType::READ_COLOR,
         NodeType::READ_ROTATION,
         NodeType::READ_SIZE}
    );
    section("Read Simulation", {NodeType::READ_DELTA_TIME, NodeType::READ_TIME, NodeType::READ_PARTICLE_ID});

    ImGui::EndPopup();
}

void ParticleEditor::render_node(const Node& node) {
    const ImU32 color = node_defs.at(node.type).color;

    ax::NodeEditor::PushStyleColor(ax::NodeEditor::StyleColor_NodeBg, ImColor(IM_COL32(30, 30, 30, 240)));
    ax::NodeEditor::PushStyleColor(ax::NodeEditor::StyleColor_NodeBorder, ImColor(color));
    ax::NodeEditor::PushStyleColor(ax::NodeEditor::StyleColor_HovNodeBorder, ImColor(color));
    ax::NodeEditor::PushStyleVar(ax::NodeEditor::StyleVar_NodeBorderWidth, 2.0f);
    ax::NodeEditor::PushStyleVar(ax::NodeEditor::StyleVar_NodeRounding, 6.0f);
    ax::NodeEditor::PushStyleVar(ax::NodeEditor::StyleVar_NodePadding, ImVec4(12, 8, 12, 8));

    ax::NodeEditor::BeginNode(node.id);
    ImGui::PushID(node.id);

    render_node_header(node, color);

    if (node.type == NodeType::VALUE) {
        ImGui::BeginGroup();
        ImGui::SetNextItemWidth(140.0f);
        Pin& out = const_cast<Node&>(node).outputs[0];
        ImGui::DragFloat4("##value", &out.constant.x, 0.01f, 0.0f, 0.0f, "%.2f");
        ImGui::EndGroup();
        ImGui::SameLine();
        ImGui::Dummy(ImVec2(8.0f, 0.0f));
        ImGui::SameLine();
        ImGui::BeginGroup();
        render_output_pins(emitters[active_emitter], node, color);
        ImGui::EndGroup();
    } else {
        render_input_pins(node, color);
        if (!node.outputs.empty()) {
            ImGui::SameLine();
            ImGui::Dummy(ImVec2(40.0f, 0.0f));
            ImGui::SameLine();
            ImGui::BeginGroup();
            render_output_pins(emitters[active_emitter], node, color);
            ImGui::EndGroup();
        }
    }

    ImGui::PopID();
    ax::NodeEditor::EndNode();
    ax::NodeEditor::PopStyleVar(3);
    ax::NodeEditor::PopStyleColor(3);
}

void ParticleEditor::render_node_header(const Node& node, ImU32 color) {
    const auto& def = node_defs.at(node.type);
    ImGui::Dummy(ImVec2(160.0f, 0.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImColor(color).Value);
    ImGui::Text("%s", def.icon);
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextUnformatted(node.name.c_str());
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRectFilled(pos, ImVec2(pos.x + 160.0f, pos.y + 1.0f), color);
    ImGui::Dummy(ImVec2(160.0f, 1.0f));
    ImGui::Spacing();
    ImGui::Spacing();
}

void ParticleEditor::render_input_pins(const Node& node, ImU32 color) {
    ImGui::BeginGroup();
    if (node.inputs.empty()) {
        ImGui::Dummy(ImVec2(30.0f, 0.0f));
    } else {
        for (const auto& pin : node.inputs) {
            ImGui::PushID(pin.id);
            ax::NodeEditor::BeginPin(pin.id, ax::NodeEditor::PinKind::Input);
            const ImU32 pc = (pin.connected_to >= 0) ? color : IM_COL32(80, 80, 80, 255);
            ImGui::PushStyleColor(ImGuiCol_Text, ImColor(pc).Value);
            ImGui::Text(ICON_FA_CIRCLE);
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::TextUnformatted(pin.name.c_str());
            ax::NodeEditor::EndPin();
            ImGui::PopID();
        }
    }
    ImGui::EndGroup();
}

void ParticleEditor::render_output_pins(EmitterGraph& emitter, const Node& node, ImU32 color) {
    for (const auto& pin : node.outputs) {
        ImGui::PushID(pin.id);
        ax::NodeEditor::BeginPin(pin.id, ax::NodeEditor::PinKind::Output);

        bool connected = false;
        for (const auto& [oid, other] : emitter.nodes) {
            for (const auto& ip : other.inputs) {
                if (ip.connected_to == pin.id) {
                    connected = true;
                    break;
                }
            }

            if (connected) {
                break;
            }
        }

        const ImU32 pc = connected ? color : IM_COL32(80, 80, 80, 255);
        ImGui::TextUnformatted(pin.name.c_str());
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, ImColor(pc).Value);
        ImGui::Text(ICON_FA_CIRCLE);
        ImGui::PopStyleColor();

        ax::NodeEditor::EndPin();
        ImGui::PopID();
    }
}

ParticleEffectSaveData ParticleEditor::save() {
    ax::NodeEditor::SetCurrentEditor(context);

    ParticleEffectSaveData data;
    data.editor_state = editor_state_blob;

    for (const auto& emitter : emitters) {
        ParticleEffectSaveData::SavedEmitter se;
        se.name = emitter.name;

        for (const auto& [nid, node] : emitter.nodes) {
            ParticleEffectSaveData::SavedNode sn;
            sn.id       = node.id;
            sn.type     = node.type;
            sn.position = ax::NodeEditor::GetNodePosition(node.id);

            for (const auto& p : node.inputs) {
                sn.inputs.push_back({p.id, p.slot, p.name, p.constant, p.connected_to});
            }

            for (const auto& p : node.outputs) {
                sn.outputs.push_back({p.id, p.slot, p.name, p.constant, p.connected_to});
            }

            se.nodes.push_back(std::move(sn));
        }

        data.emitters.push_back(std::move(se));
    }

    return data;
}

void ParticleEditor::load(const ParticleEffectSaveData& data, std::string asset_name) {
    emitters.clear();
    active_emitter     = 0;
    current_asset_name = std::move(asset_name);

    editor_state_blob = data.editor_state;
    ax::NodeEditor::DestroyEditor(context);
    context = ax::NodeEditor::CreateEditor(&config);
    ax::NodeEditor::SetCurrentEditor(context);

    if (data.emitters.empty()) {
        emitters.push_back({"Emitter 1", {}});
        create_default_nodes(emitters[0]);
        return;
    }

    for (const auto& se : data.emitters) {
        EmitterGraph emitter;
        emitter.name = se.name;

        for (const auto& sn : se.nodes) {
            Node node;
            node.id   = sn.id;
            node.type = sn.type;
            node.name = node_defs.at(sn.type).name;

            for (const auto& sp : sn.inputs) {
                node.inputs.push_back({sp.id, sp.name, sp.slot, sp.constant, sp.connected_to});
            }

            for (const auto& sp : sn.outputs) {
                node.outputs.push_back({sp.id, sp.name, sp.slot, sp.constant, sp.connected_to});
            }

            next_id = std::max(next_id, node.id + 1);
            for (auto& p : node.inputs) {
                next_id = std::max(next_id, p.id + 1);
            }

            for (auto& p : node.outputs) {
                next_id = std::max(next_id, p.id + 1);
            }

            ax::NodeEditor::SetNodePosition(node.id, sn.position);
            emitter.nodes.emplace(node.id, std::move(node));
        }

        emitters.push_back(std::move(emitter));
    }
}
