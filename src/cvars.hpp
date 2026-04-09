#pragma once

#include "ember.hpp"

enum class CVarFlags : uint32_t {
    NONE           = 0,
    DO_NOT_PERSIST = 1 << 0,
    HIDDEN         = 1 << 1,
    READONLY       = 2 << 2
};
DEFINE_BITFIELD_STRUCT(CVarFlags);

class CVarParam;

class CVars {
public:
    static constexpr std::string FILENAME = "settings.json";

    static CVars* get();

    virtual CVarParam* get_var(const std::string& id) = 0;

    virtual int32_t*     get_int_var(const std::string& id)    = 0;
    virtual float*       get_float_var(const std::string& id)  = 0;
    virtual bool*        get_bool_var(const std::string& id)   = 0;
    virtual std::string* get_string_var(const std::string& id) = 0;

    virtual void set_int_var(const std::string& id, int32_t value)               = 0;
    virtual void set_float_var(const std::string& id, float value)               = 0;
    virtual void set_bool_var(const std::string& id, bool value)                 = 0;
    virtual void set_string_var(const std::string& id, const std::string& value) = 0;

    virtual CVarParam* create_int_var(
        const std::string& id, const std::string& description, int32_t default_value, int32_t initial_value
    ) = 0;
    virtual CVarParam* create_float_var(
        const std::string& id, const std::string& description, float default_value, float initial_value
    ) = 0;
    virtual CVarParam*
    create_bool_var(const std::string& id, const std::string& description, bool default_value, bool initial_value) = 0;
    virtual CVarParam* create_string_var(
        const std::string& id,
        const std::string& description,
        const std::string& default_value,
        const std::string& initial_value
    ) = 0;

    virtual void load_from_file(const std::filesystem::path& path) = 0;
    virtual void save_to_file(const std::filesystem::path& path)   = 0;

    virtual void register_console_commands(class imgui_console* console) = 0;

private:
};

template <typename T> struct CVar {
protected:
    int index;
    using CVarType = T;
};

struct IntCVar : CVar<int32_t> {
    IntCVar(
        const std::string& name,
        const std::string& description,
        int32_t            default_value,
        CVarFlags          flags = CVarFlags::NONE
    );

    int32_t  get();
    int32_t* get_ptr();

    void set(int32_t val);
};

struct FloatCVar : CVar<float> {
    FloatCVar(
        const std::string& name, const std::string& description, float default_value, CVarFlags flags = CVarFlags::NONE
    );

    float  get();
    float* get_ptr();

    void set(float val);
};

struct BoolCVar : CVar<bool> {
    BoolCVar(
        const std::string& name, const std::string& description, bool default_value, CVarFlags flags = CVarFlags::NONE
    );

    bool  get();
    bool* get_ptr();

    void set(bool val);
};

struct StringCVar : CVar<std::string> {
    StringCVar(
        const std::string& name,
        const std::string& description,
        const std::string& default_value,
        CVarFlags          flags = CVarFlags::NONE
    );

    std::string get();

    void set(const std::string& val);
};
