#include "cvars.hpp"

#include <shared_mutex>

#include <cereal/archives/json.hpp>
#include <cereal/cereal.hpp>
#include <imgui_stdlib.h>

enum class CVarType : char {
    INT,
    FLOAT,
    BOOL,
    STRING
};

class CVarParam {
public:
    friend class CVarSystemImpl;

    int32_t   array_index;
    CVarType  type;
    CVarFlags flags;

    std::string name;
    std::string description;
};

template <typename T> struct CVarStorage {
    T initial;
    T current;

    CVarParam* parameter;
};

template <typename T> struct CVarArray {
    CVarStorage<T>* storage  = nullptr;
    int32_t         last_var = 0;

    CVarArray(size_t size) {
        storage = new CVarStorage<T>[size]();
    }

    CVarStorage<T>* get_storage(int32_t index) {
        return &storage[index];
    }

    T* get_ptr(int32_t index) {
        return &storage[index].current;
    }

    T get(int32_t index) {
        return storage[index].current;
    }

    void set(const T& val, int32_t index) {
        storage[index].current = val;
    }

    int add(const T& initial_value, const T& current_value, CVarParam* param) {
        int index = last_var;

        storage[index].current   = current_value;
        storage[index].initial   = initial_value;
        storage[index].parameter = param;

        param->array_index = index;
        last_var++;

        return index;
    }

    int add(const T& val, CVarParam* param) {
        return add(val, val, param);
    }
};

class CVarSystemImpl : public CVars {
public:
    CVarParam*   get_var(const std::string& id) override;
    int32_t*     get_int_var(const std::string& id) override;
    float*       get_float_var(const std::string& id) override;
    bool*        get_bool_var(const std::string& id) override;
    std::string* get_string_var(const std::string& id) override;

    void set_int_var(const std::string& id, int32_t value) override;
    void set_float_var(const std::string& id, float value) override;
    void set_bool_var(const std::string& id, bool value) override;
    void set_string_var(const std::string& id, const std::string& value) override;

    CVarParam* create_int_var(
        const std::string& id, const std::string& description, int32_t default_value, int32_t initial_value
    ) override;
    CVarParam* create_float_var(
        const std::string& id, const std::string& description, float default_value, float initial_value
    ) override;
    CVarParam* create_bool_var(
        const std::string& id, const std::string& description, bool default_value, bool initial_value
    ) override;
    CVarParam* create_string_var(
        const std::string& id,
        const std::string& description,
        const std::string& default_value,
        const std::string& initial_value
    ) override;

    void load_from_file(const std::filesystem::path& path) override;
    void save_to_file(const std::filesystem::path& path) override;

    void render_ui() override;

    constexpr static int32_t MAX_INT_CVARS = 1000;
    CVarArray<int32_t>       int_vars      = {MAX_INT_CVARS};

    constexpr static int32_t MAX_FLOAT_CVARS = 1000;
    CVarArray<float>         float_vars      = {MAX_FLOAT_CVARS};

    constexpr static int32_t MAX_BOOL_CVARS = 200;
    CVarArray<bool>          bool_vars      = {MAX_BOOL_CVARS};

    constexpr static int32_t MAX_STRING_CVARS = 200;
    CVarArray<std::string>   string_vars      = {MAX_STRING_CVARS};

    template <typename T> CVarArray<T>* get_var_array();

    template <typename T> T*   get_current_var(const std::string& id);
    template <typename T> void set_current_var(const std::string& id, const T& value);

    static CVarSystemImpl* get() {
        return static_cast<CVarSystemImpl*>(CVars::get());
    }

private:
    std::shared_mutex mutex;

    CVarParam* init_var(const std::string& name, const std::string& description) {
        saved_vars[name] = CVarParam{};

        CVarParam& new_param  = saved_vars[name];
        new_param.name        = name;
        new_param.description = description;

        return &new_param;
    }

    std::unordered_map<std::string, CVarParam> saved_vars;
};

template <> CVarArray<int32_t>* CVarSystemImpl::get_var_array() {
    return &int_vars;
}

template <> CVarArray<float>* CVarSystemImpl::get_var_array() {
    return &float_vars;
}

template <> CVarArray<bool>* CVarSystemImpl::get_var_array() {
    return &bool_vars;
}

template <> CVarArray<std::string>* CVarSystemImpl::get_var_array() {
    return &string_vars;
}

CVarParam* CVarSystemImpl::get_var(const std::string& id) {
    std::shared_lock lock(mutex);

    auto it = saved_vars.find(id);
    if (it != saved_vars.end()) {
        return &(*it).second;
    }

    return nullptr;
}

int32_t* CVarSystemImpl::get_int_var(const std::string& id) {
    return get_current_var<int32_t>(id);
}

float* CVarSystemImpl::get_float_var(const std::string& id) {
    return get_current_var<float>(id);
}

bool* CVarSystemImpl::get_bool_var(const std::string& id) {
    return get_current_var<bool>(id);
}

std::string* CVarSystemImpl::get_string_var(const std::string& id) {
    return get_current_var<std::string>(id);
}

void CVarSystemImpl::set_int_var(const std::string& id, int32_t value) {
    set_current_var<int32_t>(id, value);
}

void CVarSystemImpl::set_float_var(const std::string& id, float value) {
    set_current_var<float>(id, value);
}

void CVarSystemImpl::set_bool_var(const std::string& id, bool value) {
    set_current_var<bool>(id, value);
}

void CVarSystemImpl::set_string_var(const std::string& id, const std::string& value) {
    set_current_var<std::string>(id, value);
}

CVarParam* CVarSystemImpl::create_int_var(
    const std::string& id, const std::string& description, int32_t default_value, int32_t initial_value
) {
    std::unique_lock lock(mutex);

    CVarParam* param = init_var(id, description);
    if (!param) {
        return nullptr;
    }

    param->type = CVarType::INT;

    get_var_array<int32_t>()->add(default_value, initial_value, param);

    return param;
}

CVarParam* CVarSystemImpl::create_float_var(
    const std::string& id, const std::string& description, float default_value, float initial_value
) {
    std::unique_lock lock(mutex);

    CVarParam* param = init_var(id, description);
    if (!param) {
        return nullptr;
    }

    param->type = CVarType::FLOAT;

    get_var_array<float>()->add(default_value, initial_value, param);

    return param;
}

CVarParam* CVarSystemImpl::create_bool_var(
    const std::string& id, const std::string& description, bool default_value, bool initial_value
) {
    std::unique_lock lock(mutex);

    CVarParam* param = init_var(id, description);
    if (!param) {
        return nullptr;
    }

    param->type = CVarType::BOOL;

    get_var_array<bool>()->add(default_value, initial_value, param);

    return param;
}

CVarParam* CVarSystemImpl::create_string_var(
    const std::string& id,
    const std::string& description,
    const std::string& default_value,
    const std::string& initial_value
) {
    std::unique_lock lock(mutex);

    CVarParam* param = init_var(id, description);
    if (!param) {
        return nullptr;
    }

    param->type = CVarType::STRING;

    get_var_array<std::string>()->add(default_value, initial_value, param);

    return param;
}

void CVarSystemImpl::load_from_file(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return;
    }

    cereal::JSONInputArchive archive(file);

    auto load_var = [&](CVarParam* param) {
        try {
            switch (param->type) {
            case CVarType::INT: {
                int32_t val;
                archive(cereal::make_nvp(param->name, val));
                set_int_var(param->name, val);
                break;
            }
            case CVarType::FLOAT: {
                float val;
                archive(cereal::make_nvp(param->name, val));
                set_float_var(param->name, val);
                break;
            }
            case CVarType::BOOL: {
                bool val;
                archive(cereal::make_nvp(param->name, val));
                set_bool_var(param->name, val);
                break;
            }
            case CVarType::STRING: {
                std::string val;
                archive(cereal::make_nvp(param->name, val));
                set_string_var(param->name, val);
                break;
            }
            default:
                spdlog::warn("Unknown CVar type for key \"{}\"", param->name);
                break;
            }
        } catch (const cereal::Exception&) {
        }
    };

    for (int32_t i = 0; i < get_var_array<int32_t>()->last_var; i++) {
        load_var(get_var_array<int32_t>()->storage[i].parameter);
    }

    for (int32_t i = 0; i < get_var_array<float>()->last_var; i++) {
        load_var(get_var_array<float>()->storage[i].parameter);
    }

    for (int32_t i = 0; i < get_var_array<bool>()->last_var; i++) {
        load_var(get_var_array<bool>()->storage[i].parameter);
    }

    for (int32_t i = 0; i < get_var_array<std::string>()->last_var; i++) {
        load_var(get_var_array<std::string>()->storage[i].parameter);
    }
}

void CVarSystemImpl::save_to_file(const std::filesystem::path& path) {
    std::ofstream             file(path);
    cereal::JSONOutputArchive archive(file);

    auto save_vars = [&]<typename T>() {
        auto* arr = get_var_array<T>();
        for (int32_t i = 0; i < arr->last_var; i++) {
            auto&      storage = arr->storage[i];
            const bool persist =
                !(static_cast<int32_t>(storage.parameter->flags) & static_cast<int32_t>(CVarFlags::DO_NOT_PERSIST));
            if (persist) {
                archive(cereal::make_nvp(storage.parameter->name, storage.current));
            }
        }
    };

    save_vars.operator()<int32_t>();
    save_vars.operator()<float>();
    save_vars.operator()<bool>();
    save_vars.operator()<std::string>();
}

void CVarSystemImpl::render_ui() {
    static std::string filter = "";

    ImGui::InputText("Filter", &filter);
    ImGui::Separator();

    std::vector<CVarParam*> filtered_params;
    auto                    add_parameter = [&](CVarParam* param) {
        bool hidden = ((uint32_t)param->flags & (uint32_t)CVarFlags::HIDDEN);

        if (!hidden) {
            if (param->name.find(filter) != std::string::npos) {
                filtered_params.push_back(param);
            }
        }
    };

    for (int i = 0; i < get_var_array<int32_t>()->last_var; i++) {
        add_parameter(get_var_array<int32_t>()->storage[i].parameter);
    }

    for (int i = 0; i < get_var_array<bool>()->last_var; i++) {
        add_parameter(get_var_array<bool>()->storage[i].parameter);
    }

    for (int i = 0; i < get_var_array<float>()->last_var; i++) {
        add_parameter(get_var_array<float>()->storage[i].parameter);
    }

    for (int i = 0; i < get_var_array<std::string>()->last_var; i++) {
        add_parameter(get_var_array<std::string>()->storage[i].parameter);
    }

    std::ranges::sort(filtered_params, [](CVarParam* a, CVarParam* b) {
        return a->name < b->name;
    });

    auto edit_parameter = [&](CVarParam* param) {
        bool editable = !((uint32_t)param->flags & (uint32_t)CVarFlags::READONLY);

        switch (param->type) {
        case CVarType::INT:
            ImGui::InputInt(
                param->name.c_str(),
                get_var_array<int32_t>()->get_ptr(param->array_index),
                1,
                100,
                editable ? 0 : ImGuiInputTextFlags_ReadOnly
            );
            break;
        case CVarType::BOOL: {
            auto  store = get_var_array<bool>()->get_storage(param->array_index);
            bool  val   = store->current;
            bool* edit  = editable ? &store->current : &val;

            ImGui::Checkbox(param->name.c_str(), edit);
        } break;
        case CVarType::FLOAT:
            ImGui::InputFloat(
                param->name.c_str(),
                get_var_array<float>()->get_ptr(param->array_index),
                0.0f,
                0.0f,
                "%.3f",
                editable ? 0 : ImGuiInputTextFlags_ReadOnly
            );
            break;
        case CVarType::STRING:
            ImGui::InputText(
                param->name.c_str(),
                get_var_array<std::string>()->get_ptr(param->array_index),
                editable ? 0 : ImGuiInputTextFlags_ReadOnly
            );
            break;
        }
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::BeginItemTooltip()) {
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
            ImGui::TextUnformatted(param->description.c_str());
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    };

    for (auto param : filtered_params) {
        edit_parameter(param);
    }
}

template <typename T> T* CVarSystemImpl::get_current_var(const std::string& id) {
    CVarParam* param = get_var(id);
    if (!param) {
        return nullptr;
    } else {
        return get_var_array<T>()->get_ptr(param->array_index);
    }
}

template <typename T> void CVarSystemImpl::set_current_var(const std::string& id, const T& value) {
    CVarParam* param = get_var(id);
    if (param) {
        get_var_array<T>()->set(value, param->array_index);
    }
}

CVars* CVars::get() {
    static CVarSystemImpl system;

    return &system;
}

template <typename T> T get_var_current_by_index(int32_t index) {
    return CVarSystemImpl::get()->get_var_array<T>()->get(index);
}

template <typename T> T* get_var_current_ptr_by_index(int32_t index) {
    return CVarSystemImpl::get()->get_var_array<T>()->get_ptr(index);
}

template <typename T> void set_var_current_by_index(int32_t index, const T& data) {
    CVarSystemImpl::get()->get_var_array<T>()->set(data, index);
}

IntCVar::IntCVar(const std::string& name, const std::string& description, int32_t default_value, CVarFlags flags) {
    CVarParam* cvar = CVars::get()->create_int_var(name, description, default_value, default_value);
    cvar->flags     = flags;

    index = cvar->array_index;
}

int32_t IntCVar::get() {
    return get_var_current_by_index<CVarType>(index);
}

int32_t* IntCVar::get_ptr() {
    return get_var_current_ptr_by_index<CVarType>(index);
}

void IntCVar::set(int32_t val) {
    set_var_current_by_index<CVarType>(index, val);
}

FloatCVar::FloatCVar(const std::string& name, const std::string& description, float default_value, CVarFlags flags) {
    CVarParam* cvar = CVars::get()->create_float_var(name, description, default_value, default_value);
    cvar->flags     = flags;

    index = cvar->array_index;
}

float FloatCVar::get() {
    return get_var_current_by_index<CVarType>(index);
}

float* FloatCVar::get_ptr() {
    return get_var_current_ptr_by_index<CVarType>(index);
}

void FloatCVar::set(float val) {
    set_var_current_by_index<CVarType>(index, val);
}

BoolCVar::BoolCVar(const std::string& name, const std::string& description, bool default_value, CVarFlags flags) {
    CVarParam* cvar = CVars::get()->create_bool_var(name, description, default_value, default_value);
    cvar->flags     = flags;

    index = cvar->array_index;
}

bool BoolCVar::get() {
    return get_var_current_by_index<CVarType>(index);
}

bool* BoolCVar::get_ptr() {
    return get_var_current_ptr_by_index<CVarType>(index);
}

void BoolCVar::set(bool val) {
    set_var_current_by_index<CVarType>(index, val);
}

StringCVar::StringCVar(
    const std::string& name, const std::string& description, const std::string& default_value, CVarFlags flags
) {
    CVarParam* cvar = CVars::get()->create_string_var(name, description, default_value, default_value);
    cvar->flags     = flags;

    index = cvar->array_index;
}

std::string StringCVar::get() {
    return get_var_current_by_index<CVarType>(index);
}

void StringCVar::set(const std::string& val) {
    set_var_current_by_index<CVarType>(index, val);
}
