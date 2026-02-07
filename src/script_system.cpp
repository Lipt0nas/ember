#include "script_system.hpp"

#include "world.hpp"

#include "glm/gtc/random.hpp"

#include <fstream>
#include <iterator>

#include <angelscript.h>
#include <scriptarray.h>
#include <scriptbuilder.h>
#include <scriptdictionary.h>
#include <scriptgrid.h>
#include <scripthandle.h>
#include <scriptstdstring.h>
#include <weakref.h>

namespace {
    auto hash_script = [](const std::filesystem::path& path) -> uint32_t {
        uint32_t hash = 0;

        for (auto& it : path.string()) {
            hash = 37 * hash + 17 + static_cast<char>(it);
        }

        return hash;
    };

    void script_message_callback(const asSMessageInfo* msg, void* param) {
        switch (msg->type) {
        case asMSGTYPE_ERROR:
            spdlog::error("{} ({}:{}): {}", msg->section, msg->row, msg->col, msg->message);
            break;
        case asMSGTYPE_WARNING:
            spdlog::warn("{} ({}:{}): {}", msg->section, msg->row, msg->col, msg->message);
            break;
        case asMSGTYPE_INFORMATION:
            spdlog::info("{} ({}:{}): {}", msg->section, msg->row, msg->col, msg->message);
            break;
        }
    }

    int script_include_callback(const char* include, const char* from, CScriptBuilder* builder, void* user_param) {
        if (strcmp(include, from) == 0) {
            return -1;
        }

        auto scripts = (std::unordered_map<std::string, std::string>*)user_param;
        auto entry   = scripts->find(include);

        if (entry == scripts->end()) {
            return -1;
        }

        builder->AddSectionFromMemory(include, entry->second.c_str(), entry->second.size());

        return 0;
    }

    void script_log_trace(const std::string& string) {
        spdlog::trace("{}", string);
    }

    void script_log_debug(const std::string& string) {
        spdlog::debug("{}", string);
    }

    void script_log_info(const std::string& string) {
        spdlog::info("{}", string);
    }

    void script_log_warn(const std::string& string) {
        spdlog::warn("{}", string);
    }

    void script_log_error(const std::string& string) {
        spdlog::error("{}", string);
    }

    void script_log_critical(const std::string& string) {
        spdlog::critical("{}", string);
    }

    float script_rand_float(float min, float max) {
        return glm::linearRand(min, max);
    }

    int script_rand_int(int min, int max) {
        return glm::linearRand(min, max);
    }

    void vec2_construct_default(void* memory) {
        new (memory) glm::vec2(0.0f);
    }

    void vec2_construct_values(float x, float y, void* memory) {
        new (memory) glm::vec2(x, y);
    }

    void vec2_construct_copy(const glm::vec2& other, void* memory) {
        new (memory) glm::vec2(other);
    }

    void vec2_construct_float(float v, void* memory) {
        new (memory) glm::vec2(v);
    }

    void vec2_destruct(glm::vec2* ptr) {
        ptr->~vec();
    }

    glm::vec2& vec2_assign(const glm::vec2& other, glm::vec2* self) {
        return (*self = other);
    }

    glm::vec2 vec2_add(const glm::vec2& other, const glm::vec2* self) {
        return *self + other;
    }

    glm::vec2 vec2_sub(const glm::vec2& other, const glm::vec2* self) {
        return *self - other;
    }

    glm::vec2 vec2_mul(float scalar, const glm::vec2* self) {
        return *self * scalar;
    }

    glm::vec2 vec2_div(float scalar, const glm::vec2* self) {
        return *self / scalar;
    }

    glm::vec2 vec2_neg(const glm::vec2* self) {
        return -*self;
    }

    glm::vec2& vec2_mul_assign_scalar(float scalar, glm::vec2* self) {
        return (*self *= scalar);
    }

    glm::vec2& vec2_mul_assign_vec(const glm::vec2& other, glm::vec2* self) {
        return (*self *= other);
    }

    glm::vec2& vec2_div_assign_scalar(float scalar, glm::vec2* self) {
        return (*self /= scalar);
    }

    glm::vec2& vec2_div_assign_vec(const glm::vec2& other, glm::vec2* self) {
        return (*self /= other);
    }

    glm::vec2& vec2_add_assign(const glm::vec2& other, glm::vec2* self) {
        return (*self += other);
    }

    glm::vec2& vec2_sub_assign(const glm::vec2& other, glm::vec2* self) {
        return (*self -= other);
    }

    glm::vec2 vec2_mul_r(float scalar, const glm::vec2* self) {
        return scalar * *self;
    }

    glm::vec2 vec2_div_r(float scalar, const glm::vec2* self) {
        return glm::vec2(scalar) / *self;
    }

    float vec2_length(const glm::vec2* self) {
        return glm::length(*self);
    }

    glm::vec2 vec2_normalize(const glm::vec2* self) {
        return glm::normalize(*self);
    }

    float vec2_dot(const glm::vec2& other, const glm::vec2* self) {
        return glm::dot(*self, other);
    }

    std::string vec2_to_string(const glm::vec2* self) {
        std::stringstream ss;
        ss << "vec2(" << self->x << ", " << self->y << ")";
        return ss.str();
    }

    void vec3_construct_default(void* memory) {
        new (memory) glm::vec3(0.0f);
    }

    void vec3_construct_values(float x, float y, float z, void* memory) {
        new (memory) glm::vec3(x, y, z);
    }

    void vec3_construct_copy(const glm::vec3& other, void* memory) {
        new (memory) glm::vec3(other);
    }

    void vec3_construct_float(float v, void* memory) {
        new (memory) glm::vec3(v);
    }

    void vec3_destruct(glm::vec3* ptr) {
        ptr->~vec();
    }

    glm::vec3& vec3_assign(const glm::vec3& other, glm::vec3* self) {
        return (*self = other);
    }

    glm::vec3 vec3_add(const glm::vec3& other, const glm::vec3* self) {
        return *self + other;
    }

    glm::vec3 vec3_sub(const glm::vec3& other, const glm::vec3* self) {
        return *self - other;
    }

    glm::vec3 vec3_mul(float scalar, const glm::vec3* self) {
        return *self * scalar;
    }

    glm::vec3 vec3_div(float scalar, const glm::vec3* self) {
        return *self / scalar;
    }

    glm::vec3 vec3_neg(const glm::vec3* self) {
        return -*self;
    }

    glm::vec3& vec3_mul_assign_scalar(float scalar, glm::vec3* self) {
        return (*self *= scalar);
    }

    glm::vec3& vec3_mul_assign_vec(const glm::vec3& other, glm::vec3* self) {
        return (*self *= other);
    }

    glm::vec3& vec3_div_assign_scalar(float scalar, glm::vec3* self) {
        return (*self /= scalar);
    }

    glm::vec3& vec3_div_assign_vec(const glm::vec3& other, glm::vec3* self) {
        return (*self /= other);
    }

    glm::vec3& vec3_add_assign(const glm::vec3& other, glm::vec3* self) {
        return (*self += other);
    }

    glm::vec3& vec3_sub_assign(const glm::vec3& other, glm::vec3* self) {
        return (*self -= other);
    }

    glm::vec3 vec3_mul_r(float scalar, const glm::vec3* self) {
        return scalar * *self;
    }

    glm::vec3 vec3_div_r(float scalar, const glm::vec3* self) {
        return glm::vec3(scalar) / *self;
    }

    float vec3_length(const glm::vec3* self) {
        return glm::length(*self);
    }

    glm::vec3 vec3_normalize(const glm::vec3* self) {
        return glm::normalize(*self);
    }

    float vec3_dot(const glm::vec3& other, const glm::vec3* self) {
        return glm::dot(*self, other);
    }

    glm::vec3 vec3_cross(const glm::vec3& other, const glm::vec3* self) {
        return glm::cross(*self, other);
    }

    std::string vec3_to_string(const glm::vec3* self) {
        std::stringstream ss;
        ss << "vec3(" << self->x << ", " << self->y << ", " << self->z << ")";
        return ss.str();
    }

    void vec4_construct_default(void* memory) {
        new (memory) glm::vec4(0.0f);
    }

    void vec4_construct_values(float x, float y, float z, float w, void* memory) {
        new (memory) glm::vec4(x, y, z, w);
    }

    void vec4_construct_copy(const glm::vec4& other, void* memory) {
        new (memory) glm::vec4(other);
    }

    void vec4_construct_float(float v, void* memory) {
        new (memory) glm::vec4(v);
    }

    void vec4_destruct(glm::vec4* ptr) {
        ptr->~vec();
    }

    glm::vec4& vec4_assign(const glm::vec4& other, glm::vec4* self) {
        return (*self = other);
    }

    glm::vec4 vec4_add(const glm::vec4& other, const glm::vec4* self) {
        return *self + other;
    }

    glm::vec4 vec4_sub(const glm::vec4& other, const glm::vec4* self) {
        return *self - other;
    }

    glm::vec4 vec4_mul(float scalar, const glm::vec4* self) {
        return *self * scalar;
    }

    glm::vec4 vec4_div(float scalar, const glm::vec4* self) {
        return *self / scalar;
    }

    glm::vec4 vec4_neg(const glm::vec4* self) {
        return -*self;
    }

    glm::vec4& vec4_mul_assign_scalar(float scalar, glm::vec4* self) {
        return (*self *= scalar);
    }

    glm::vec4& vec4_mul_assign_vec(const glm::vec4& other, glm::vec4* self) {
        return (*self *= other);
    }

    glm::vec4& vec4_div_assign_scalar(float scalar, glm::vec4* self) {
        return (*self /= scalar);
    }

    glm::vec4& vec4_div_assign_vec(const glm::vec4& other, glm::vec4* self) {
        return (*self /= other);
    }

    glm::vec4& vec4_add_assign(const glm::vec4& other, glm::vec4* self) {
        return (*self += other);
    }

    glm::vec4& vec4_sub_assign(const glm::vec4& other, glm::vec4* self) {
        return (*self -= other);
    }

    glm::vec4 vec4_mul_r(float scalar, const glm::vec4* self) {
        return scalar * *self;
    }

    glm::vec4 vec4_div_r(float scalar, const glm::vec4* self) {
        return glm::vec4(scalar) / *self;
    }

    float vec4_length(const glm::vec4* self) {
        return glm::length(*self);
    }

    glm::vec4 vec4_normalize(const glm::vec4* self) {
        return glm::normalize(*self);
    }

    float vec4_dot(const glm::vec4& other, const glm::vec4* self) {
        return glm::dot(*self, other);
    }

    std::string vec4_to_string(const glm::vec4* self) {
        std::stringstream ss;
        ss << "vec4(" << self->x << ", " << self->y << ", " << self->z << ", " << self->w << ")";
        return ss.str();
    }

    void mat4_construct_default(void* memory) {
        new (memory) glm::mat4(1.0f);
    }

    void mat4_construct_copy(const glm::mat4& other, void* memory) {
        new (memory) glm::mat4(other);
    }

    void mat4_destruct(glm::mat4* ptr) {
        ptr->~mat();
    }

    glm::mat4& mat4_assign(const glm::mat4& other, glm::mat4* self) {
        return (*self = other);
    }

    glm::mat4 mat4_mul_mat(const glm::mat4& other, const glm::mat4* self) {
        return *self * other;
    }

    glm::vec4 mat4_mul_vec(const glm::vec4& vec, const glm::mat4* self) {
        return *self * vec;
    }

    glm::mat4& mat4_mul_assign(const glm::mat4& other, glm::mat4* self) {
        return (*self *= other);
    }

    glm::mat4 mat4_translate(const glm::vec3& vec, const glm::mat4* self) {
        return glm::translate(*self, vec);
    }

    glm::mat4 mat4_rotate(float angle, const glm::vec3& axis, const glm::mat4* self) {
        return glm::rotate(*self, angle, axis);
    }

    glm::mat4 mat4_scale(const glm::vec3& vec, const glm::mat4* self) {
        return glm::scale(*self, vec);
    }

    glm::mat4 mat4_inverse(const glm::mat4* self) {
        return glm::inverse(*self);
    }

    glm::mat4 mat4_transpose(const glm::mat4* self) {
        return glm::transpose(*self);
    }

    glm::mat4 mat4_perspective(float fovy, float aspect, float near, float far) {
        return glm::perspective(fovy, aspect, near, far);
    }

    glm::mat4 mat4_ortho(float left, float right, float bottom, float top, float near, float far) {
        return glm::ortho(left, right, bottom, top, near, far);
    }

    glm::mat4 mat4_lookAt(const glm::vec3& eye, const glm::vec3& center, const glm::vec3& up) {
        return glm::lookAt(eye, center, up);
    }

    std::string mat4_to_string(const glm::mat4* self) {
        return glm::to_string(*self);
    }

    void quat_construct_default(void* memory) {
        new (memory) glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    }

    void quat_construct_values(float w, float x, float y, float z, void* memory) {
        new (memory) glm::quat(w, x, y, z);
    }

    void quat_construct_copy(const glm::quat& other, void* memory) {
        new (memory) glm::quat(other);
    }

    void quat_construct_axis_angle(const glm::vec3& axis, float angle, void* memory) {
        new (memory) glm::quat(glm::angleAxis(angle, axis));
    }

    void quat_destruct(glm::quat* ptr) {
        ptr->~qua();
    }

    glm::quat& quat_assign(const glm::quat& other, glm::quat* self) {
        return (*self = other);
    }

    glm::quat quat_mul(const glm::quat& other, const glm::quat* self) {
        return *self * other;
    }

    glm::vec3 quat_mul_vec(const glm::vec3& vec, const glm::quat* self) {
        return *self * vec;
    }

    glm::quat& quat_mul_assign(const glm::quat& other, glm::quat* self) {
        return (*self *= other);
    }

    glm::quat quat_normalize(const glm::quat* self) {
        return glm::normalize(*self);
    }

    glm::quat quat_conjugate(const glm::quat* self) {
        return glm::conjugate(*self);
    }

    glm::quat quat_inverse(const glm::quat* self) {
        return glm::inverse(*self);
    }

    float quat_dot(const glm::quat& other, const glm::quat* self) {
        return glm::dot(*self, other);
    }

    glm::quat quat_slerp(const glm::quat& other, float t, const glm::quat* self) {
        return glm::slerp(*self, other, t);
    }

    glm::mat4 quat_to_mat4(const glm::quat* self) {
        return glm::mat4_cast(*self);
    }

    glm::vec3 quat_euler_angles(const glm::quat* self) {
        return glm::eulerAngles(*self);
    }

    std::string quat_to_string(const glm::quat* self) {
        std::stringstream ss;
        ss << "quat(" << self->w << ", " << self->x << ", " << self->y << ", " << self->z << ")";
        return ss.str();
    }

    void register_glm_vec2(asIScriptEngine* engine) {
        int r;

        r = engine->RegisterObjectType(
            "vec2",
            sizeof(glm::vec2),
            asOBJ_VALUE | asOBJ_POD | asOBJ_APP_CLASS_ALLFLOATS | asGetTypeTraits<glm::vec2>()
        );
        assert(r >= 0);

        r = engine->RegisterObjectBehaviour(
            "vec2", asBEHAVE_CONSTRUCT, "void f(float)", asFUNCTION(vec2_construct_float), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);

        r = engine->RegisterObjectBehaviour(
            "vec2", asBEHAVE_CONSTRUCT, "void f()", asFUNCTION(vec2_construct_default), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
        r = engine->RegisterObjectBehaviour(
            "vec2", asBEHAVE_CONSTRUCT, "void f(float, float)", asFUNCTION(vec2_construct_values), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
        r = engine->RegisterObjectBehaviour(
            "vec2", asBEHAVE_CONSTRUCT, "void f(const vec2 &in)", asFUNCTION(vec2_construct_copy), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
        r = engine->RegisterObjectBehaviour(
            "vec2", asBEHAVE_DESTRUCT, "void f()", asFUNCTION(vec2_destruct), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);

        r = engine->RegisterObjectProperty("vec2", "float x", asOFFSET(glm::vec2, x));
        assert(r >= 0);
        r = engine->RegisterObjectProperty("vec2", "float y", asOFFSET(glm::vec2, y));
        assert(r >= 0);

        r = engine->RegisterObjectMethod(
            "vec2", "vec2 &opAssign(const vec2 &in)", asFUNCTION(vec2_assign), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
        r = engine->RegisterObjectMethod(
            "vec2", "vec2 opAdd(const vec2 &in) const", asFUNCTION(vec2_add), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
        r = engine->RegisterObjectMethod(
            "vec2", "vec2 opSub(const vec2 &in) const", asFUNCTION(vec2_sub), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
        r = engine->RegisterObjectMethod("vec2", "vec2 opMul(float) const", asFUNCTION(vec2_mul), asCALL_CDECL_OBJLAST);
        assert(r >= 0);
        r = engine->RegisterObjectMethod("vec2", "vec2 opDiv(float) const", asFUNCTION(vec2_div), asCALL_CDECL_OBJLAST);
        assert(r >= 0);
        r = engine->RegisterObjectMethod("vec2", "vec2 opNeg() const", asFUNCTION(vec2_neg), asCALL_CDECL_OBJLAST);
        assert(r >= 0);
        r = engine->RegisterObjectMethod(
            "vec2", "vec2 opMul_r(float) const", asFUNCTION(vec2_mul_r), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
        r = engine->RegisterObjectMethod(
            "vec2", "vec2 opDiv_r(float) const", asFUNCTION(vec2_div_r), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);

        r = engine->RegisterObjectMethod(
            "vec2", "vec2 &opAddAssign(const vec2 &in)", asFUNCTION(vec2_add_assign), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
        r = engine->RegisterObjectMethod(
            "vec2", "vec2 &opSubAssign(const vec2 &in)", asFUNCTION(vec2_sub_assign), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
        r = engine->RegisterObjectMethod(
            "vec2", "vec2 &opMulAssign(float)", asFUNCTION(vec2_mul_assign_scalar), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
        r = engine->RegisterObjectMethod(
            "vec2", "vec2 &opMulAssign(const vec2 &in)", asFUNCTION(vec2_mul_assign_vec), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
        r = engine->RegisterObjectMethod(
            "vec2", "vec2 &opDivAssign(float)", asFUNCTION(vec2_div_assign_scalar), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
        r = engine->RegisterObjectMethod(
            "vec2", "vec2 &opDivAssign(const vec2 &in)", asFUNCTION(vec2_div_assign_vec), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);

        r = engine->RegisterObjectMethod("vec2", "float length() const", asFUNCTION(vec2_length), asCALL_CDECL_OBJLAST);
        assert(r >= 0);
        r = engine->RegisterObjectMethod(
            "vec2", "vec2 normalize() const", asFUNCTION(vec2_normalize), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
        r = engine->RegisterObjectMethod(
            "vec2", "float dot(const vec2 &in) const", asFUNCTION(vec2_dot), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
        r = engine->RegisterObjectMethod(
            "vec2", "string to_string() const", asFUNCTION(vec2_to_string), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
    }

    void register_glm_vec3(asIScriptEngine* engine) {
        int r;

        r = engine->RegisterObjectType(
            "vec3",
            sizeof(glm::vec3),
            asOBJ_VALUE | asOBJ_POD | asOBJ_APP_CLASS_ALLFLOATS | asGetTypeTraits<glm::vec3>()
        );
        assert(r >= 0);

        r = engine->RegisterObjectBehaviour(
            "vec3", asBEHAVE_CONSTRUCT, "void f()", asFUNCTION(vec3_construct_default), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
        r = engine->RegisterObjectBehaviour(
            "vec3",
            asBEHAVE_CONSTRUCT,
            "void f(float, float, float)",
            asFUNCTION(vec3_construct_values),
            asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);

        r = engine->RegisterObjectBehaviour(
            "vec3", asBEHAVE_CONSTRUCT, "void f(float)", asFUNCTION(vec3_construct_float), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);

        r = engine->RegisterObjectBehaviour(
            "vec3", asBEHAVE_CONSTRUCT, "void f(const vec3 &in)", asFUNCTION(vec3_construct_copy), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
        r = engine->RegisterObjectBehaviour(
            "vec3", asBEHAVE_DESTRUCT, "void f()", asFUNCTION(vec3_destruct), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);

        r = engine->RegisterObjectProperty("vec3", "float x", asOFFSET(glm::vec3, x));
        assert(r >= 0);
        r = engine->RegisterObjectProperty("vec3", "float y", asOFFSET(glm::vec3, y));
        assert(r >= 0);
        r = engine->RegisterObjectProperty("vec3", "float z", asOFFSET(glm::vec3, z));
        assert(r >= 0);

        r = engine->RegisterObjectMethod(
            "vec3", "vec3 &opAssign(const vec3 &in)", asFUNCTION(vec3_assign), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
        r = engine->RegisterObjectMethod(
            "vec3", "vec3 opAdd(const vec3 &in) const", asFUNCTION(vec3_add), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
        r = engine->RegisterObjectMethod(
            "vec3", "vec3 opSub(const vec3 &in) const", asFUNCTION(vec3_sub), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
        r = engine->RegisterObjectMethod("vec3", "vec3 opMul(float) const", asFUNCTION(vec3_mul), asCALL_CDECL_OBJLAST);
        assert(r >= 0);
        r = engine->RegisterObjectMethod("vec3", "vec3 opDiv(float) const", asFUNCTION(vec3_div), asCALL_CDECL_OBJLAST);
        assert(r >= 0);
        r = engine->RegisterObjectMethod("vec3", "vec3 opNeg() const", asFUNCTION(vec3_neg), asCALL_CDECL_OBJLAST);
        assert(r >= 0);
        r = engine->RegisterObjectMethod(
            "vec3", "vec3 opMul_r(float) const", asFUNCTION(vec3_mul_r), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
        r = engine->RegisterObjectMethod(
            "vec3", "vec3 opDiv_r(float) const", asFUNCTION(vec3_div_r), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);

        r = engine->RegisterObjectMethod(
            "vec3", "vec3 &opAddAssign(const vec3 &in)", asFUNCTION(vec3_add_assign), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
        r = engine->RegisterObjectMethod(
            "vec3", "vec3 &opSubAssign(const vec3 &in)", asFUNCTION(vec3_sub_assign), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
        r = engine->RegisterObjectMethod(
            "vec3", "vec3 &opMulAssign(float)", asFUNCTION(vec3_mul_assign_scalar), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
        r = engine->RegisterObjectMethod(
            "vec3", "vec3 &opMulAssign(const vec3 &in)", asFUNCTION(vec3_mul_assign_vec), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
        r = engine->RegisterObjectMethod(
            "vec3", "vec3 &opDivAssign(float)", asFUNCTION(vec3_div_assign_scalar), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
        r = engine->RegisterObjectMethod(
            "vec3", "vec3 &opDivAssign(const vec3 &in)", asFUNCTION(vec3_div_assign_vec), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);

        r = engine->RegisterObjectMethod("vec3", "float length() const", asFUNCTION(vec3_length), asCALL_CDECL_OBJLAST);
        assert(r >= 0);
        r = engine->RegisterObjectMethod(
            "vec3", "vec3 normalize() const", asFUNCTION(vec3_normalize), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
        r = engine->RegisterObjectMethod(
            "vec3", "float dot(const vec3 &in) const", asFUNCTION(vec3_dot), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
        r = engine->RegisterObjectMethod(
            "vec3", "vec3 cross(const vec3 &in) const", asFUNCTION(vec3_cross), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
        r = engine->RegisterObjectMethod(
            "vec3", "string to_string() const", asFUNCTION(vec3_to_string), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
    }

    void register_glm_vec4(asIScriptEngine* engine) {
        int r;

        r = engine->RegisterObjectType(
            "vec4",
            sizeof(glm::vec4),
            asOBJ_VALUE | asOBJ_POD | asOBJ_APP_CLASS_ALLFLOATS | asGetTypeTraits<glm::vec4>()
        );
        assert(r >= 0);

        r = engine->RegisterObjectBehaviour(
            "vec4", asBEHAVE_CONSTRUCT, "void f()", asFUNCTION(vec4_construct_default), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
        r = engine->RegisterObjectBehaviour(
            "vec4",
            asBEHAVE_CONSTRUCT,
            "void f(float, float, float, float)",
            asFUNCTION(vec4_construct_values),
            asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);

        r = engine->RegisterObjectBehaviour(
            "vec4", asBEHAVE_CONSTRUCT, "void f(float)", asFUNCTION(vec4_construct_float), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);

        r = engine->RegisterObjectBehaviour(
            "vec4", asBEHAVE_CONSTRUCT, "void f(const vec4 &in)", asFUNCTION(vec4_construct_copy), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
        r = engine->RegisterObjectBehaviour(
            "vec4", asBEHAVE_DESTRUCT, "void f()", asFUNCTION(vec4_destruct), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);

        r = engine->RegisterObjectProperty("vec4", "float x", asOFFSET(glm::vec4, x));
        assert(r >= 0);
        r = engine->RegisterObjectProperty("vec4", "float y", asOFFSET(glm::vec4, y));
        assert(r >= 0);
        r = engine->RegisterObjectProperty("vec4", "float z", asOFFSET(glm::vec4, z));
        assert(r >= 0);
        r = engine->RegisterObjectProperty("vec4", "float w", asOFFSET(glm::vec4, w));
        assert(r >= 0);

        r = engine->RegisterObjectMethod(
            "vec4", "vec4 &opAssign(const vec4 &in)", asFUNCTION(vec4_assign), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
        r = engine->RegisterObjectMethod(
            "vec4", "vec4 opAdd(const vec4 &in) const", asFUNCTION(vec4_add), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
        r = engine->RegisterObjectMethod(
            "vec4", "vec4 opSub(const vec4 &in) const", asFUNCTION(vec4_sub), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
        r = engine->RegisterObjectMethod("vec4", "vec4 opMul(float) const", asFUNCTION(vec4_mul), asCALL_CDECL_OBJLAST);
        assert(r >= 0);
        r = engine->RegisterObjectMethod("vec4", "vec4 opDiv(float) const", asFUNCTION(vec4_div), asCALL_CDECL_OBJLAST);
        assert(r >= 0);
        r = engine->RegisterObjectMethod("vec4", "vec4 opNeg() const", asFUNCTION(vec4_neg), asCALL_CDECL_OBJLAST);
        assert(r >= 0);
        r = engine->RegisterObjectMethod(
            "vec4", "vec4 opMul_r(float) const", asFUNCTION(vec4_mul_r), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
        r = engine->RegisterObjectMethod(
            "vec4", "vec4 opDiv_r(float) const", asFUNCTION(vec4_div_r), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);

        r = engine->RegisterObjectMethod(
            "vec4", "vec4 &opAddAssign(const vec4 &in)", asFUNCTION(vec4_add_assign), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
        r = engine->RegisterObjectMethod(
            "vec4", "vec4 &opSubAssign(const vec4 &in)", asFUNCTION(vec4_sub_assign), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
        r = engine->RegisterObjectMethod(
            "vec4", "vec4 &opMulAssign(float)", asFUNCTION(vec4_mul_assign_scalar), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
        r = engine->RegisterObjectMethod(
            "vec4", "vec4 &opMulAssign(const vec4 &in)", asFUNCTION(vec4_mul_assign_vec), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
        r = engine->RegisterObjectMethod(
            "vec4", "vec4 &opDivAssign(float)", asFUNCTION(vec4_div_assign_scalar), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
        r = engine->RegisterObjectMethod(
            "vec4", "vec4 &opDivAssign(const vec4 &in)", asFUNCTION(vec4_div_assign_vec), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);

        r = engine->RegisterObjectMethod("vec4", "float length() const", asFUNCTION(vec4_length), asCALL_CDECL_OBJLAST);
        assert(r >= 0);
        r = engine->RegisterObjectMethod(
            "vec4", "vec4 normalize() const", asFUNCTION(vec4_normalize), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
        r = engine->RegisterObjectMethod(
            "vec4", "float dot(const vec4 &in) const", asFUNCTION(vec4_dot), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
        r = engine->RegisterObjectMethod(
            "vec4", "string to_string() const", asFUNCTION(vec4_to_string), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
    }

    void register_glm_mat4(asIScriptEngine* engine) {
        int r;

        r = engine->RegisterObjectType(
            "mat4",
            sizeof(glm::mat4),
            asOBJ_VALUE | asOBJ_POD | asOBJ_APP_CLASS_ALLFLOATS | asGetTypeTraits<glm::mat4>()
        );
        assert(r >= 0);

        r = engine->RegisterObjectBehaviour(
            "mat4", asBEHAVE_CONSTRUCT, "void f()", asFUNCTION(mat4_construct_default), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
        r = engine->RegisterObjectBehaviour(
            "mat4", asBEHAVE_CONSTRUCT, "void f(const mat4 &in)", asFUNCTION(mat4_construct_copy), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
        r = engine->RegisterObjectBehaviour(
            "mat4", asBEHAVE_DESTRUCT, "void f()", asFUNCTION(mat4_destruct), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);

        r = engine->RegisterObjectMethod(
            "mat4", "mat4 &opAssign(const mat4 &in)", asFUNCTION(mat4_assign), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
        r = engine->RegisterObjectMethod(
            "mat4", "mat4 opMul(const mat4 &in) const", asFUNCTION(mat4_mul_mat), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
        r = engine->RegisterObjectMethod(
            "mat4", "vec4 opMul(const vec4 &in) const", asFUNCTION(mat4_mul_vec), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);

        r = engine->RegisterObjectMethod(
            "mat4", "mat4 &opMulAssign(const mat4 &in)", asFUNCTION(mat4_mul_assign), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);

        r = engine->RegisterObjectMethod(
            "mat4", "mat4 translate(const vec3 &in) const", asFUNCTION(mat4_translate), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
        r = engine->RegisterObjectMethod(
            "mat4", "mat4 rotate(float, const vec3 &in) const", asFUNCTION(mat4_rotate), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
        r = engine->RegisterObjectMethod(
            "mat4", "mat4 scale(const vec3 &in) const", asFUNCTION(mat4_scale), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
        r = engine->RegisterObjectMethod(
            "mat4", "mat4 inverse() const", asFUNCTION(mat4_inverse), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
        r = engine->RegisterObjectMethod(
            "mat4", "mat4 transpose() const", asFUNCTION(mat4_transpose), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
        r = engine->RegisterObjectMethod(
            "mat4", "string toString() const", asFUNCTION(mat4_to_string), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);

        r = engine->RegisterGlobalFunction(
            "mat4 perspective(float, float, float, float)", asFUNCTION(mat4_perspective), asCALL_CDECL
        );
        assert(r >= 0);
        r = engine->RegisterGlobalFunction(
            "mat4 ortho(float, float, float, float, float, float)", asFUNCTION(mat4_ortho), asCALL_CDECL
        );
        assert(r >= 0);
        r = engine->RegisterGlobalFunction(
            "mat4 look_at(const vec3 &in, const vec3 &in, const vec3 &in)", asFUNCTION(mat4_lookAt), asCALL_CDECL
        );
        assert(r >= 0);
    }

    void register_glm_quat(asIScriptEngine* engine) {
        int r;

        r = engine->RegisterObjectType(
            "quat",
            sizeof(glm::quat),
            asOBJ_VALUE | asOBJ_POD | asOBJ_APP_CLASS_ALLFLOATS | asGetTypeTraits<glm::quat>()
        );
        assert(r >= 0);

        r = engine->RegisterObjectBehaviour(
            "quat", asBEHAVE_CONSTRUCT, "void f()", asFUNCTION(quat_construct_default), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
        r = engine->RegisterObjectBehaviour(
            "quat",
            asBEHAVE_CONSTRUCT,
            "void f(float, float, float, float)",
            asFUNCTION(quat_construct_values),
            asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
        r = engine->RegisterObjectBehaviour(
            "quat", asBEHAVE_CONSTRUCT, "void f(const quat &in)", asFUNCTION(quat_construct_copy), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
        r = engine->RegisterObjectBehaviour(
            "quat",
            asBEHAVE_CONSTRUCT,
            "void f(const vec3 &in, float)",
            asFUNCTION(quat_construct_axis_angle),
            asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
        r = engine->RegisterObjectBehaviour(
            "quat", asBEHAVE_DESTRUCT, "void f()", asFUNCTION(quat_destruct), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);

        r = engine->RegisterObjectProperty("quat", "float w", asOFFSET(glm::quat, w));
        assert(r >= 0);
        r = engine->RegisterObjectProperty("quat", "float x", asOFFSET(glm::quat, x));
        assert(r >= 0);
        r = engine->RegisterObjectProperty("quat", "float y", asOFFSET(glm::quat, y));
        assert(r >= 0);
        r = engine->RegisterObjectProperty("quat", "float z", asOFFSET(glm::quat, z));
        assert(r >= 0);

        r = engine->RegisterObjectMethod(
            "quat", "quat &opAssign(const quat &in)", asFUNCTION(quat_assign), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
        r = engine->RegisterObjectMethod(
            "quat", "quat opMul(const quat &in) const", asFUNCTION(quat_mul), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
        r = engine->RegisterObjectMethod(
            "quat", "vec3 opMul(const vec3 &in) const", asFUNCTION(quat_mul_vec), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);

        r = engine->RegisterObjectMethod(
            "quat", "quat &opMulAssign(const quat &in)", asFUNCTION(quat_mul_assign), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);

        r = engine->RegisterObjectMethod(
            "quat", "quat normalize() const", asFUNCTION(quat_normalize), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
        r = engine->RegisterObjectMethod(
            "quat", "quat conjugate() const", asFUNCTION(quat_conjugate), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
        r = engine->RegisterObjectMethod(
            "quat", "quat inverse() const", asFUNCTION(quat_inverse), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
        r = engine->RegisterObjectMethod(
            "quat", "float dot(const quat &in) const", asFUNCTION(quat_dot), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
        r = engine->RegisterObjectMethod(
            "quat", "quat slerp(const quat &in, float) const", asFUNCTION(quat_slerp), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
        r = engine->RegisterObjectMethod("quat", "mat4 toMat4() const", asFUNCTION(quat_to_mat4), asCALL_CDECL_OBJLAST);
        assert(r >= 0);
        r = engine->RegisterObjectMethod(
            "quat", "vec3 euler_angles() const", asFUNCTION(quat_euler_angles), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
        r = engine->RegisterObjectMethod(
            "quat", "string to_string() const", asFUNCTION(quat_to_string), asCALL_CDECL_OBJLAST
        );
        assert(r >= 0);
    }

    void register_glm_types(asIScriptEngine* engine) {
        register_glm_vec2(engine);
        register_glm_vec3(engine);
        register_glm_vec4(engine);
        register_glm_mat4(engine);
        register_glm_quat(engine);
    }

    template <class Stream> void generate_enum_list(const asIScriptEngine* engine, Stream& stream) {
        for (int i = 0; i < engine->GetEnumCount(); i++) {
            const auto e = engine->GetEnumByIndex(i);
            if (not e)
                continue;
            const std::string_view ns = e->GetNamespace();
            if (not ns.empty())
                stream << std::format("namespace {} {{\n", ns);
            stream << std::format("enum {} {{\n", e->GetName());
            for (int j = 0; j < e->GetEnumValueCount(); ++j) {
                stream << std::format("\t{}", e->GetEnumValueByIndex(j, nullptr));
                if (j < e->GetEnumValueCount() - 1)
                    stream << ",";
                stream << "\n";
            }
            stream << "}\n";
            if (not ns.empty())
                stream << "}\n";
        }
    }

    template <class Stream> void generate_class_type_list(const asIScriptEngine* engine, Stream& stream) {
        for (int i = 0; i < engine->GetObjectTypeCount(); i++) {
            const auto t = engine->GetObjectTypeByIndex(i);
            if (not t)
                continue;

            const std::string_view ns = t->GetNamespace();
            if (not ns.empty())
                stream << std::format("namespace {} {{\n", ns);

            stream << std::format("class {}", t->GetName());
            if (t->GetSubTypeCount() > 0) {
                stream << "<";
                for (int sub = 0; sub < t->GetSubTypeCount(); ++sub) {
                    if (sub < t->GetSubTypeCount() - 1)
                        stream << ", ";
                    const auto st = t->GetSubType(sub);
                    stream << st->GetName();
                }

                stream << ">";
            }

            stream << "{\n";
            for (int j = 0; j < t->GetBehaviourCount(); ++j) {
                asEBehaviours behaviours;
                const auto    f = t->GetBehaviourByIndex(j, &behaviours);
                if (behaviours == asBEHAVE_CONSTRUCT || behaviours == asBEHAVE_DESTRUCT) {
                    stream << std::format("\t{};\n", f->GetDeclaration(false, true, true));
                }
            }
            for (int j = 0; j < t->GetMethodCount(); ++j) {
                const auto m = t->GetMethodByIndex(j);
                stream << std::format("\t{};\n", m->GetDeclaration(false, true, true));
            }
            for (int j = 0; j < t->GetPropertyCount(); ++j) {
                stream << std::format("\t{};\n", t->GetPropertyDeclaration(j, true));
            }
            for (int j = 0; j < t->GetChildFuncdefCount(); ++j) {
                stream << std::format(
                    "\tfuncdef {};\n", t->GetChildFuncdef(j)->GetFuncdefSignature()->GetDeclaration(false)
                );
            }
            stream << "}\n";
            if (not ns.empty())
                stream << "}\n";
        }
    }

    template <class Stream> void generate_global_function_list(const asIScriptEngine* engine, Stream& stream) {
        for (int i = 0; i < engine->GetGlobalFunctionCount(); i++) {
            const auto f = engine->GetGlobalFunctionByIndex(i);
            if (not f)
                continue;
            const std::string_view ns = f->GetNamespace();
            if (not ns.empty())
                stream << std::format("namespace {} {{ ", ns);
            stream << std::format("{};", f->GetDeclaration(false, false, true));
            if (not ns.empty())
                stream << " }";
            stream << "\n";
        }
    }

    template <class Stream> void generate_global_property_list(const asIScriptEngine* engine, Stream& stream) {
        for (int i = 0; i < engine->GetGlobalPropertyCount(); i++) {
            const char* name;
            const char* ns0;
            int         type;
            engine->GetGlobalPropertyByIndex(i, &name, &ns0, &type, nullptr, nullptr, nullptr, nullptr);

            const std::string t = engine->GetTypeDeclaration(type, true);
            if (t.empty())
                continue;

            std::string_view ns = ns0;
            if (not ns.empty())
                stream << std::format("namespace {} {{ ", ns);

            stream << std::format("{} {};", t, name);
            if (not ns.empty())
                stream << " }";
            stream << "\n";
        }
    }

    template <class Stream> void generate_global_typedefs(const asIScriptEngine* engine, Stream& stream) {
        for (int i = 0; i < engine->GetTypedefCount(); ++i) {
            const auto type = engine->GetTypedefByIndex(i);
            if (not type)
                continue;
            const std::string_view ns = type->GetNamespace();
            if (not ns.empty())
                stream << std::format("namespace {} {{\n", ns);
            stream << std::format(
                "typedef {} {};\n", engine->GetTypeDeclaration(type->GetTypedefTypeId()), type->GetName()
            );
            if (not ns.empty())
                stream << "}\n";
        }
    }

    glm::vec3 node_get_position(components::Transform* o) {
        return o->position;
    }

    void node_set_position(glm::vec3 position, components::Transform* o) {
        o->dirty    = true;
        o->position = position;
    }

    float node_get_scale(components::Transform* o) {
        return o->scale;
    }

    void node_set_scale(float scale, components::Transform* o) {
        o->dirty = true;
        o->scale = scale;
    }

    glm::quat node_get_rotation(components::Transform* o) {
        return o->rotation;
    }

    void node_set_rotation(glm::quat rotation, components::Transform* o) {
        o->dirty    = true;
        o->rotation = rotation;
    }

    glm::vec3 node_get_world_position(components::Transform* o) {
        return o->world_position;
    }

    float node_get_world_scale(components::Transform* o) {
        return o->world_scale;
    }

    glm::quat node_get_world_rotation(components::Transform* o) {
        return o->world_rotation;
    }

    Entity node_to_handle(void* obj) {
        return *reinterpret_cast<Entity*>(obj);
    }

    World* get_world_from_context() {
        return reinterpret_cast<World*>(asGetActiveContext()->GetEngine()->GetUserData());
    }

    bool node_is_valid(void* obj) {
        Entity id = *reinterpret_cast<Entity*>(obj);
        if (id == entt::null) {
            return false;
        }

        return get_world_from_context()->scene.entity_registry.valid(id);
    }

    Entity clone_node(void* obj) {
        Entity id = *reinterpret_cast<Entity*>(obj);
        if (id == entt::null) {
            return entt::null;
        }

        return get_world_from_context()->scene.clone_node(id);
    }

    Entity find_child(void* obj, const std::string& name) {
        Entity id = *reinterpret_cast<Entity*>(obj);
        if (id == entt::null) {
            return entt::null;
        }

        auto world = get_world_from_context();

        auto c = world->scene.get_component<components::Children>(id);
        if (c) {
            for (Entity child : c->children) {
                auto n = world->scene.get_component<components::Name>(child);
                if (n->name == name) {
                    return child;
                }
            }
        }

        return entt::null;
    }

    bool node_has_tag(void* obj, const std::string& tag) {
        Entity id = *reinterpret_cast<Entity*>(obj);
        if (id == entt::null) {
            return false;
        }

        return get_world_from_context()->scene.node_has_tag(id, tag);
    }

    void node_physics_set_linear_velocity(glm::vec3 velocity, components::Physics* p) {
        if (p->is_static || p->body_id.IsInvalid()) {
            return;
        }

        auto world  = get_world_from_context();
        auto entity = world->scene.get_node_from_component(*p);
        if (world->scene.entity_registry.valid(entity)) {
            world->physics.system.GetBodyInterface().SetLinearVelocity(
                p->body_id, JPH::Vec3(velocity.x, velocity.y, velocity.z)
            );
        }
    }

    void node_physics_set_angular_velocity(glm::vec3 velocity, components::Physics* p) {
        if (p->is_static || p->body_id.IsInvalid()) {
            return;
        }

        auto world  = get_world_from_context();
        auto entity = world->scene.get_node_from_component(*p);
        if (world->scene.entity_registry.valid(entity)) {
            world->physics.system.GetBodyInterface().SetAngularVelocity(
                p->body_id, JPH::Vec3(velocity.x, velocity.y, velocity.z)
            );
        }
    }

    void node_physics_set_active(bool active, components::Physics* p) {
        if (p->body_id.IsInvalid()) {
            return;
        }

        auto world  = get_world_from_context();
        auto entity = world->scene.get_node_from_component(*p);

        if (world->scene.entity_registry.valid(entity)) {
            if (active) {
                world->physics.system.GetBodyInterface().ActivateBody(p->body_id);
            } else {
                world->physics.system.GetBodyInterface().DeactivateBody(p->body_id);
            }
        }
    }

    void node_physics_set_friction(float friction, components::Physics* p) {
        if (p->body_id.IsInvalid()) {
            return;
        }

        auto world  = get_world_from_context();
        auto entity = world->scene.get_node_from_component(*p);

        if (world->scene.entity_registry.valid(entity)) {
            world->physics.system.GetBodyInterface().SetFriction(p->body_id, friction);
        }
    }

    void node_physics_set_restitution(float restitution, components::Physics* p) {
        if (p->body_id.IsInvalid()) {
            return;
        }

        auto world  = get_world_from_context();
        auto entity = world->scene.get_node_from_component(*p);

        if (world->scene.entity_registry.valid(entity)) {
            world->physics.system.GetBodyInterface().SetRestitution(p->body_id, restitution);
        }
    }

    void node_physics_set_box_body(glm::vec3 half_extents, float mass, components::Physics* p) {
        auto world  = get_world_from_context();
        auto entity = world->scene.get_node_from_component(*p);

        auto t = world->scene.get_component<components::Transform>(entity);

        if (!p->body_id.IsInvalid()) {
            world->physics.system.GetBodyInterface().RemoveBody(p->body_id);
            world->physics.system.GetBodyInterface().DestroyBody(p->body_id);
        }

        auto body_settings = JPH::BodyCreationSettings(
            new JPH::BoxShape(JPH::RVec3(half_extents.x, half_extents.y, half_extents.z)),
            JPH::Vec3(t->position.x, t->position.y, t->position.z),
            JPH::Quat::sIdentity(),
            JPH::EMotionType::Dynamic,
            Layers::MOVING
        );
        body_settings.mOverrideMassProperties       = JPH::EOverrideMassProperties::CalculateInertia;
        body_settings.mMassPropertiesOverride.mMass = mass;

        JPH::BodyID body_id =
            world->physics.system.GetBodyInterface().CreateAndAddBody(body_settings, JPH::EActivation::Activate);
        p->body_id    = body_id;
        p->is_static  = false;
        p->last_scale = t->scale;
    }

    Material* node_mesh_get_material(components::Mesh* m) {
        return &get_world_from_context()->scene.materials[m->mesh.material_id];
    }

    Mesh* node_mesh_get_mesh(components::Mesh* m) {
        return &get_world_from_context()->scene.meshes[m->mesh.mesh_id];
    }

    void node_mesh_material_make_dedicated(components::Mesh* m) {
        auto world = get_world_from_context();

        world->scene.materials.push_back(world->scene.materials[m->mesh.material_id]);
        m->mesh.material_id = world->scene.materials.size() - 1;
    }
} // namespace

void node_get_component(asIScriptGeneric* gen) {
    auto type_id = gen->GetEngine()->GetTypeInfoById(gen->GetReturnTypeId())->GetTypeId();

    Entity entity = *reinterpret_cast<Entity*>(gen->GetObject());

    auto world = get_world_from_context();
    auto it    = world->script.component_retrieve_map.find(type_id);

    if (it != world->script.component_retrieve_map.end()) {
        gen->SetReturnAddress(it->second(world->scene, entity));
    }
}

ScriptSystem::ScriptSystem() {
    spdlog::info("Initializing script system");

    engine = asCreateScriptEngine();

    engine->SetMessageCallback(asFUNCTION(script_message_callback), 0, asCALL_CDECL);
    engine->SetEngineProperty(asEP_DISALLOW_EMPTY_LIST_ELEMENTS, true);
    engine->SetEngineProperty(asEP_DISALLOW_VALUE_ASSIGN_FOR_REF_TYPE, true);

    context        = engine->CreateContext();
    script_builder = new CScriptBuilder();
}

void ScriptSystem::initialize(class World* world) {
    this->world = world;
    engine->SetUserData(world);

    auto default_namespace = engine->GetDefaultNamespace();

    RegisterScriptArray(engine, true);
    RegisterStdString(engine);
    RegisterStdStringUtils(engine);
    RegisterScriptHandle(engine);
    RegisterScriptWeakRef(engine);
    RegisterScriptDictionary(engine);
    RegisterScriptGrid(engine);

    register_glm_types(engine);

    engine->RegisterEnum("Key");
    for (int i = 0; i < world->input.get_key_count(); i++) {
        auto name = world->input.key_to_string(static_cast<Key>(i));
        engine->RegisterEnumValue("Key", name.c_str(), i);
    }

    engine->RegisterEnum("Button");
    for (int i = 0; i < world->input.get_button_count(); i++) {
        auto name = world->input.button_to_string(static_cast<Button>(i));
        engine->RegisterEnumValue("Button", name.c_str(), i);
    }

    engine->RegisterObjectType("Material", 0, asOBJ_REF | asOBJ_NOCOUNT);
    engine->RegisterObjectProperty("Material", "uint albedo_index", asOFFSET(Material, albedo_index));
    engine->RegisterObjectProperty("Material", "uint normals_index", asOFFSET(Material, normals_index));
    engine->RegisterObjectProperty("Material", "uint material_index", asOFFSET(Material, material_index));
    engine->RegisterObjectProperty("Material", "uint emissive_index", asOFFSET(Material, emissive_index));
    engine->RegisterObjectProperty("Material", "vec3 emissive_factor", asOFFSET(Material, emissive_factor));
    engine->RegisterObjectProperty("Material", "vec3 albedo_factor", asOFFSET(Material, albedo_factor));
    engine->RegisterObjectProperty("Material", "float roughness_factor", asOFFSET(Material, roughness_factor));
    engine->RegisterObjectProperty("Material", "float metallic_factor", asOFFSET(Material, metallic_factor));
    engine->RegisterObjectProperty("Material", "float normal_scale", asOFFSET(Material, normal_scale));

    engine->RegisterObjectType("Mesh", 0, asOBJ_REF | asOBJ_NOCOUNT);
    engine->RegisterObjectProperty("Mesh", "vec3 center", asOFFSET(Mesh, center));
    engine->RegisterObjectProperty("Mesh", "float radius", asOFFSET(Mesh, radius));
    engine->RegisterObjectProperty("Mesh", "vec3 bounds_min", asOFFSET(Mesh, bounds_min));
    engine->RegisterObjectProperty("Mesh", "vec3 bounds_max", asOFFSET(Mesh, bounds_max));

    register_node_type(engine);

    engine->SetDefaultNamespace("Log");
    engine->RegisterGlobalFunction("void trace(string &in)", asFUNCTION(script_log_trace), asCALL_CDECL);
    engine->RegisterGlobalFunction("void debug(string &in)", asFUNCTION(script_log_debug), asCALL_CDECL);
    engine->RegisterGlobalFunction("void info(string &in)", asFUNCTION(script_log_info), asCALL_CDECL);
    engine->RegisterGlobalFunction("void warn(string &in)", asFUNCTION(script_log_warn), asCALL_CDECL);
    engine->RegisterGlobalFunction("void error(string &in)", asFUNCTION(script_log_error), asCALL_CDECL);
    engine->RegisterGlobalFunction("void critical(string &in)", asFUNCTION(script_log_critical), asCALL_CDECL);

    engine->SetDefaultNamespace("Input");
    engine->RegisterGlobalFunction(
        "vec2 get_mouse_position()",
        asMETHOD(InputSystem, InputSystem::get_mouse_position),
        asCALL_THISCALL_ASGLOBAL,
        &world->input
    );

    engine->RegisterGlobalFunction(
        "bool is_key_pressed(Key)",
        asMETHOD(InputSystem, InputSystem::is_key_pressed),
        asCALL_THISCALL_ASGLOBAL,
        &world->input
    );

    engine->RegisterGlobalFunction(
        "bool is_key_just_pressed(Key)",
        asMETHOD(InputSystem, InputSystem::is_key_just_pressed),
        asCALL_THISCALL_ASGLOBAL,
        &world->input
    );

    engine->RegisterGlobalFunction(
        "string key_to_string(Key)",
        asMETHOD(InputSystem, InputSystem::key_to_string),
        asCALL_THISCALL_ASGLOBAL,
        &world->input
    );

    engine->RegisterGlobalFunction(
        "Key string_to_key(string &in)",
        asMETHOD(InputSystem, InputSystem::string_to_key),
        asCALL_THISCALL_ASGLOBAL,
        &world->input
    );

    engine->RegisterGlobalFunction(
        "bool is_button_pressed(Button)",
        asMETHOD(InputSystem, InputSystem::is_button_pressed),
        asCALL_THISCALL_ASGLOBAL,
        &world->input
    );

    engine->RegisterGlobalFunction(
        "bool is_button_just_pressed(Button)",
        asMETHOD(InputSystem, InputSystem::is_button_just_pressed),
        asCALL_THISCALL_ASGLOBAL,
        &world->input
    );

    engine->RegisterGlobalFunction(
        "string button_to_string(Button)",
        asMETHOD(InputSystem, InputSystem::button_to_string),
        asCALL_THISCALL_ASGLOBAL,
        &world->input
    );

    engine->RegisterGlobalFunction(
        "Button string_to_button(string &in)",
        asMETHOD(InputSystem, InputSystem::string_to_button),
        asCALL_THISCALL_ASGLOBAL,
        &world->input
    );

    engine->SetDefaultNamespace("Components");
    {
        engine->RegisterObjectType("Transform", 0, asOBJ_REF | asOBJ_NOCOUNT);
        engine->RegisterObjectMethod(
            "Transform", "vec3 get_position() property", asFUNCTION(node_get_position), asCALL_CDECL_OBJLAST
        );
        engine->RegisterObjectMethod(
            "Transform", "void set_position(vec3) property", asFUNCTION(node_set_position), asCALL_CDECL_OBJLAST
        );
        engine->RegisterObjectMethod(
            "Transform", "float get_scale() property", asFUNCTION(node_get_scale), asCALL_CDECL_OBJLAST
        );
        engine->RegisterObjectMethod(
            "Transform", "void set_scale(float) property", asFUNCTION(node_set_scale), asCALL_CDECL_OBJLAST
        );
        engine->RegisterObjectMethod(
            "Transform", "quat get_rotation() property", asFUNCTION(node_get_rotation), asCALL_CDECL_OBJLAST
        );
        engine->RegisterObjectMethod(
            "Transform", "void set_rotation(quat) property", asFUNCTION(node_set_rotation), asCALL_CDECL_OBJLAST
        );
        engine->RegisterObjectMethod(
            "Transform", "vec3 get_world_position() property", asFUNCTION(node_get_world_position), asCALL_CDECL_OBJLAST
        );
        engine->RegisterObjectMethod(
            "Transform", "float get_world_scale() property", asFUNCTION(node_get_world_scale), asCALL_CDECL_OBJLAST
        );
        engine->RegisterObjectMethod(
            "Transform", "quat get_world_rotation() property", asFUNCTION(node_get_world_rotation), asCALL_CDECL_OBJLAST
        );
        engine->RegisterObjectMethod(
            "Transform",
            "vec3 transform(vec3)",
            asFUNCTION(+[](glm::vec3 point, components::Transform* t) {
                return rotate_quat(point, t->world_rotation) * t->world_scale + t->world_position;
            }),
            asCALL_CDECL_OBJLAST
        );
        engine->RegisterObjectMethod(
            "Transform",
            "vec3 transform_translate(vec3)",
            asFUNCTION(+[](glm::vec3 point, components::Transform* t) {
                return point + t->world_position;
            }),
            asCALL_CDECL_OBJLAST
        );
        engine->RegisterObjectMethod(
            "Transform",
            "vec3 transform_scale(vec3)",
            asFUNCTION(+[](glm::vec3 point, components::Transform* t) {
                return point * t->world_scale;
            }),
            asCALL_CDECL_OBJLAST
        );
        engine->RegisterObjectMethod(
            "Transform",
            "vec3 transform_rotate(vec3)",
            asFUNCTION(+[](glm::vec3 point, components::Transform* t) {
                return rotate_quat(point, t->rotation);
            }),
            asCALL_CDECL_OBJLAST
        );

        auto type = engine->GetTypeInfoByName("Transform");
        if (!type) {
            spdlog::error("Failed to get type info for Transform component");
            return;
        }

        component_retrieve_map.insert({
            type->GetTypeId(),
            [](Scene& scene, Entity e) {
                return scene.get_component<components::Transform>(e);
            },
        });
    }

    {
        engine->RegisterObjectType("Name", 0, asOBJ_REF | asOBJ_NOCOUNT);
        engine->RegisterObjectProperty("Name", "string name", asOFFSET(components::Name, name));

        auto type = engine->GetTypeInfoByName("Name");
        if (!type) {
            spdlog::error("Failed to get type info for Name component");
            return;
        }

        component_retrieve_map.insert({
            type->GetTypeId(),
            [](Scene& scene, Entity e) {
                return scene.get_component<components::Name>(e);
            },
        });
    }

    {
        engine->RegisterObjectType("Physics", 0, asOBJ_REF | asOBJ_NOCOUNT);
        engine->RegisterObjectMethod(
            "Physics",
            "void set_linear_velocity(vec3)",
            asFUNCTION(node_physics_set_linear_velocity),
            asCALL_CDECL_OBJLAST
        );
        engine->RegisterObjectMethod(
            "Physics",
            "void set_angular_velocity(vec3)",
            asFUNCTION(node_physics_set_angular_velocity),
            asCALL_CDECL_OBJLAST
        );
        engine->RegisterObjectMethod(
            "Physics", "void set_friction(float)", asFUNCTION(node_physics_set_friction), asCALL_CDECL_OBJLAST
        );
        engine->RegisterObjectMethod(
            "Physics", "void set_restitution(float)", asFUNCTION(node_physics_set_restitution), asCALL_CDECL_OBJLAST
        );
        engine->RegisterObjectMethod(
            "Physics", "void set_active(bool)", asFUNCTION(node_physics_set_active), asCALL_CDECL_OBJLAST
        );
        engine->RegisterObjectMethod(
            "Physics",
            "void set_box_body(vec3, float = 1.0f)",
            asFUNCTION(node_physics_set_box_body),
            asCALL_CDECL_OBJLAST
        );

        auto type = engine->GetTypeInfoByName("Physics");
        if (!type) {
            spdlog::error("Failed to get type info for Physics component");
            return;
        }

        component_retrieve_map.insert({
            type->GetTypeId(),
            [](Scene& scene, Entity e) {
                return scene.get_component<components::Physics>(e);
            },
        });
    }

    {
        engine->RegisterObjectType("Mesh", 0, asOBJ_REF | asOBJ_NOCOUNT);
        engine->RegisterObjectMethod(
            "Mesh", "Material@ get_material()", asFUNCTION(node_mesh_get_material), asCALL_CDECL_OBJLAST
        );
        engine->RegisterObjectMethod(
            "Mesh", "::Mesh@ get_mesh()", asFUNCTION(node_mesh_get_mesh), asCALL_CDECL_OBJLAST
        );
        engine->RegisterObjectMethod(
            "Mesh", "void dedicate_material()", asFUNCTION(node_mesh_material_make_dedicated), asCALL_CDECL_OBJLAST
        );

        auto type = engine->GetTypeInfoByName("Mesh");
        if (!type) {
            spdlog::error("Failed to get type info for Mesh component");
            return;
        }

        component_retrieve_map.insert({
            type->GetTypeId(),
            [](Scene& scene, Entity e) {
                return scene.get_component<components::Mesh>(e);
            },
        });
    }

    engine->SetDefaultNamespace("World");
    engine->RegisterGlobalFunction(
        "void delete_node(Node, bool = false)",
        asMETHOD(ScriptSystem, ScriptSystem::delete_node),
        asCALL_THISCALL_ASGLOBAL,
        this
    );

    engine->RegisterGlobalFunction(
        "array<Node>@ find_nodes_with_tag(string &in)",
        asMETHOD(ScriptSystem, ScriptSystem::find_nodes_with_tag),
        asCALL_THISCALL_ASGLOBAL,
        this
    );

    engine->RegisterGlobalFunction(
        "bool cast_ray(vec3, vec3, float, float &out, Node &out)",
        asMETHOD(ScriptSystem, ScriptSystem::cast_ray),
        asCALL_THISCALL_ASGLOBAL,
        this
    );

    engine->RegisterGlobalFunction(
        "vec3 get_player_position()",
        asMETHOD(ScriptSystem, ScriptSystem::get_player_position),
        asCALL_THISCALL_ASGLOBAL,
        this
    );

    engine->RegisterGlobalFunction(
        "vec3 get_player_look_direction()",
        asMETHOD(ScriptSystem, ScriptSystem::get_player_look_direction),
        asCALL_THISCALL_ASGLOBAL,
        this
    );

    engine->RegisterGlobalFunction(
        "vec3 get_player_velocity()",
        asMETHOD(ScriptSystem, ScriptSystem::get_player_velocity),
        asCALL_THISCALL_ASGLOBAL,
        this
    );

    engine->RegisterGlobalFunction(
        "Node find_node(string &in)", asMETHOD(ScriptSystem, ScriptSystem::find_node), asCALL_THISCALL_ASGLOBAL, this
    );

    engine->SetDefaultNamespace("Random");
    engine->RegisterGlobalFunction("float random_float(float, float)", asFUNCTION(script_rand_float), asCALL_CDECL);
    engine->RegisterGlobalFunction("int random_int(int, int)", asFUNCTION(script_rand_int), asCALL_CDECL);

    engine->SetDefaultNamespace(default_namespace);

    engine->RegisterGlobalFunction(
        "float min(float, float)", asFUNCTIONPR(glm::min, (float, float), float), asCALL_CDECL
    );
    engine->RegisterGlobalFunction(
        "float max(float, float)", asFUNCTIONPR(glm::max, (float, float), float), asCALL_CDECL
    );

    engine->RegisterGlobalFunction("float cos(float)", asFUNCTIONPR(glm::cos, (float), float), asCALL_CDECL);
    engine->RegisterGlobalFunction("float sin(float)", asFUNCTIONPR(glm::sin, (float), float), asCALL_CDECL);
    engine->RegisterGlobalFunction("float tan(float)", asFUNCTIONPR(glm::tan, (float), float), asCALL_CDECL);

    engine->RegisterGlobalFunction("float acos(float)", asFUNCTIONPR(glm::acos, (float), float), asCALL_CDECL);
    engine->RegisterGlobalFunction("float asin(float)", asFUNCTIONPR(glm::asin, (float), float), asCALL_CDECL);
    engine->RegisterGlobalFunction("float atan(float)", asFUNCTIONPR(glm::atan, (float), float), asCALL_CDECL);

    engine->RegisterGlobalFunction("float cosh(float)", asFUNCTIONPR(glm::cosh, (float), float), asCALL_CDECL);
    engine->RegisterGlobalFunction("float sinh(float)", asFUNCTIONPR(glm::sinh, (float), float), asCALL_CDECL);
    engine->RegisterGlobalFunction("float tanh(float)", asFUNCTIONPR(glm::tanh, (float), float), asCALL_CDECL);

    engine->RegisterGlobalFunction("float acosh(float)", asFUNCTIONPR(glm::acosh, (float), float), asCALL_CDECL);
    engine->RegisterGlobalFunction("float asinh(float)", asFUNCTIONPR(glm::asinh, (float), float), asCALL_CDECL);
    engine->RegisterGlobalFunction("float atanh(float)", asFUNCTIONPR(glm::atanh, (float), float), asCALL_CDECL);

    engine->RegisterGlobalFunction("float log(float)", asFUNCTIONPR(glm::log, (float), float), asCALL_CDECL);
    engine->RegisterGlobalFunction("float log2(float)", asFUNCTIONPR(glm::log2, (float), float), asCALL_CDECL);

    engine->RegisterGlobalFunction(
        "float pow(float, float)", asFUNCTIONPR(glm::pow, (float, float), float), asCALL_CDECL
    );

    engine->RegisterGlobalFunction("float sqrt(float)", asFUNCTIONPR(glm::sqrt, (float), float), asCALL_CDECL);
    engine->RegisterGlobalFunction(
        "float invsqrt(float)", asFUNCTIONPR(glm::inversesqrt, (float), float), asCALL_CDECL
    );

    engine->RegisterGlobalFunction("float abs(float)", asFUNCTIONPR(glm::abs, (float), float), asCALL_CDECL);

    engine->RegisterGlobalFunction("float ceil(float)", asFUNCTIONPR(glm::ceil, (float), float), asCALL_CDECL);
    engine->RegisterGlobalFunction("float floor(float)", asFUNCTIONPR(glm::floor, (float), float), asCALL_CDECL);

    engine->SetDefaultNamespace("Events");

    engine->RegisterInterface("IEvent");
    engine->RegisterInterfaceMethod("IEvent", "int get_event_type() const");
    engine->RegisterFuncdef("void EventCallback(IEvent@)");

    engine->RegisterGlobalFunction(
        "void subscribe(Node, int, EventCallback@)",
        asMETHOD(ScriptSystem, subscribe_to_event),
        asCALL_THISCALL_ASGLOBAL,
        this
    );

    engine->RegisterGlobalFunction(
        "void unsubscribe(Node, int)", asMETHOD(ScriptSystem, unsubscribe_from_event), asCALL_THISCALL_ASGLOBAL, this
    );

    engine->RegisterGlobalFunction(
        "void publish(IEvent@)", asMETHOD(ScriptSystem, publish_event), asCALL_THISCALL_ASGLOBAL, this
    );

    engine->RegisterGlobalFunction(
        "void publish_to_node(Node, IEvent@)",
        asMETHOD(ScriptSystem, publish_event_to_node),
        asCALL_THISCALL_ASGLOBAL,
        this
    );

    engine->RegisterGlobalFunction(
        "void publish_to_tag(string &in, IEvent@)",
        asMETHOD(ScriptSystem, publish_event_to_tag),
        asCALL_THISCALL_ASGLOBAL,
        this
    );

    prelude_code = R"(
        shared class NodeBehavior {
            protected uint node_id;

            void update(float dt) {}
            void fixed_update(float dt) {}

            Node opImplConv() const {
                return Node(node_id);
            }

            Node node() {
                return this;
            }

            void subscribe_to_event(int event_type, Events::EventCallback@ callback) {
                Events::subscribe(this.node_id, event_type, callback);
            }

            void unsubscribe_from_event(int event_type) {
                Events::unsubscribe(this.node_id, event_type);
            }
        }

        shared abstract class Event : Events::IEvent {
            private int type;

            Event(int type) {
                this.type = type;
            }

            int get_event_type() const {
                return type;
            }
        }
    )";

    // Compile a dummy module to be able to see the types defined in the prelude without needing to compile any actual
    // code
    asIScriptModule* prelude_module = engine->GetModule("_Prelude", asGM_ALWAYS_CREATE);
    prelude_module->AddScriptSection("prelude", prelude_code.c_str());
    prelude_module->Build();

    asITypeInfo* node_type = prelude_module->GetTypeInfoByName("NodeBehavior");
    if (!node_type) {
        spdlog::error("Failed to find Node type");
        return;
    }

    int prop_count = node_type->GetPropertyCount();
    for (int i = 0; i < prop_count; i++) {
        const char* prop_name;
        node_type->GetProperty(i, &prop_name);

        if (strcmp(prop_name, "node_id") == 0) {
            node_id_property_index = i;
            break;
        }
    }

    if (node_id_property_index == -1) {
        spdlog::error("Failed to cache node_id index");
    }
}

void ScriptSystem::load_scripts(const std::filesystem::path& path) {
    script_source_dir = path;
    spdlog::info("loading scripts from {}", script_source_dir.string());
    if (!std::filesystem::exists(path)) {
        return;
    }

    std::unordered_map<std::string, std::string> script_sources;
    script_builder->SetIncludeCallback(script_include_callback, &script_sources);

    for (const auto& entry : std::filesystem::recursive_directory_iterator(script_source_dir)) {
        if (!entry.is_regular_file()) {
            continue;
        }

        if (entry.path().extension() != ".as") {
            continue;
        }

        std::ifstream file(entry.path());
        if (!file.is_open()) {
            continue;
        }

        std::string source(std::istreambuf_iterator<char>(file), {});

        script_sources.insert({entry.path().filename().string(), source});
    }

    for (const auto& [filename, source] : script_sources) {
        auto hash           = hash_script(filename);
        scripts[hash].valid = false;

        int r;
        r = script_builder->StartNewModule(engine, filename.c_str());
        if (r < 0) {
            spdlog::error("Failed to start a new module {}", filename);
            continue;
        }

        r = script_builder->AddSectionFromMemory("__prelude__", prelude_code.c_str(), prelude_code.length());
        if (r < 0) {
            spdlog::error("Failed include prelude {}", filename);
            continue;
        }

        r = script_builder->AddSectionFromMemory(filename.c_str(), source.c_str(), source.length());
        if (r < 0) {
            spdlog::error("Failed to load script {}", filename);
            continue;
        }

        r = script_builder->BuildModule();
        if (r < 0) {
            spdlog::error("Failed to build script {}", filename);
            continue;
        }

        asIScriptModule* script_module = script_builder->GetModule();

        asITypeInfo* node_type  = nullptr;
        int          type_count = script_module->GetObjectTypeCount();
        for (int i = 0; i < type_count; i++) {
            asITypeInfo* type = script_module->GetObjectTypeByIndex(i);
            if (type->GetBaseType()) {
                if (strcmp(type->GetBaseType()->GetName(), "NodeBehavior") == 0) {
                    node_type = type;
                    break;
                }
            }
        }

        asIScriptFunction*                     constructor     = nullptr;
        asIScriptFunction*                     on_start        = nullptr;
        asIScriptFunction*                     on_update       = nullptr;
        asIScriptFunction*                     on_fixed_update = nullptr;
        std::vector<ScriptPropertyDescription> editable_properties;

        if (node_type) {
            std::string constructor_name =
                std::string(node_type->GetName()) + "@ " + std::string(node_type->GetName()) + "()";
            constructor = node_type->GetFactoryByDecl(constructor_name.c_str());
            if (!constructor) {
                spdlog::warn("Script doesn't have a default constructor, it will not be loaded");
                continue;
            }

            on_start        = node_type->GetMethodByDecl("void start()");
            on_update       = node_type->GetMethodByDecl("void update(float)");
            on_fixed_update = node_type->GetMethodByDecl("void fixed_update(float)");

            int prop_count = node_type->GetPropertyCount();
            for (int i = 0; i < prop_count; i++) {
                auto metadata = script_builder->GetMetadataForTypeProperty(node_type->GetTypeId(), i);
                for (auto& meta : metadata) {
                    if (meta.compare("editable") == 0) {
                        const char* name;
                        int         type_id;
                        int         offset;
                        node_type->GetProperty(i, &name, &type_id, nullptr, nullptr, &offset);

                        auto type_info = engine->GetTypeInfoById(type_id);

                        ScriptPropertyDescription::Type type = ScriptPropertyDescription::Type::UNKNOWN;
                        ScriptProperty                  default_value;
                        std::vector<std::string>        enum_values;

                        if (type_info && (type_info->GetFlags() & asOBJ_ENUM)) {
                            type                = ScriptPropertyDescription::Type::ENUM;
                            default_value.value = 0;

                            for (int e = 0; e < type_info->GetEnumValueCount(); e++) {
                                int         enum_val;
                                const char* enum_name = type_info->GetEnumValueByIndex(e, &enum_val);
                                enum_values.push_back(enum_name);
                            }
                        } else if (type_id == asTYPEID_BOOL) {
                            type                = ScriptPropertyDescription::Type::BOOL;
                            default_value.value = false;
                        } else if (type_id == asTYPEID_INT32) {
                            type                = ScriptPropertyDescription::Type::INT;
                            default_value.value = 0;
                        } else if (type_id == asTYPEID_FLOAT) {
                            type                = ScriptPropertyDescription::Type::FLOAT;
                            default_value.value = 0.0f;
                        } else if (type_id == engine->GetTypeIdByDecl("string")) {
                            type                = ScriptPropertyDescription::Type::STRING;
                            default_value.value = "";
                        } else if (type_id == engine->GetTypeIdByDecl("vec2")) {
                            type                = ScriptPropertyDescription::Type::VEC2;
                            default_value.value = glm::vec2(0.0f);
                        } else if (type_id == engine->GetTypeIdByDecl("vec3")) {
                            type                = ScriptPropertyDescription::Type::VEC3;
                            default_value.value = glm::vec3(0.0f);
                        } else if (type_id == engine->GetTypeIdByDecl("vec4")) {
                            type                = ScriptPropertyDescription::Type::VEC4;
                            default_value.value = glm::vec4(0.0f);
                        } else if (type_id == engine->GetTypeIdByDecl("quat")) {
                            type                = ScriptPropertyDescription::Type::QUAT;
                            default_value.value = glm::quat(0.0f, 0.0f, 0.0f, 1.0f);
                        } else {
                            spdlog::warn("Unsupported type: {}", name);
                        }

                        if (type != ScriptPropertyDescription::Type::UNKNOWN) {
                            editable_properties.push_back(
                                ScriptPropertyDescription{
                                    .type          = type,
                                    .default_value = default_value,
                                    .name          = name,
                                    .index         = i,
                                    .type_id       = type_id,
                                    .offset        = offset,
                                    .enum_values   = enum_values,
                                }
                            );
                        }
                    }
                }
            }
        }

        scripts[hash] = {
            Script{
                .valid               = true,
                .name                = filename,
                .source              = source,
                .module              = script_module,
                .constructor         = constructor,
                .on_start            = on_start,
                .on_update           = on_update,
                .on_fixed_update     = on_fixed_update,
                .editable_properties = editable_properties,
            },
        };
    }
}

void ScriptSystem::reload_scripts() {
    spdlog::info("releasing scripts");
    clear();

    spdlog::info("discarding modules");
    for (auto& [hash, handle] : scripts) {
        if (handle.module) {
            handle.module->Discard();
        }
    }
    spdlog::info("clearing map");
    scripts.clear();

    load_scripts(script_source_dir);
}

void ScriptSystem::generate_predefined_file() {
    if (!std::filesystem::exists(script_source_dir)) {
        return;
    }

    std::ofstream stream(script_source_dir / "as.predefined");
    generate_enum_list(engine, stream);
    generate_class_type_list(engine, stream);
    generate_global_function_list(engine, stream);
    generate_global_property_list(engine, stream);
    generate_global_typedefs(engine, stream);
}

void ScriptSystem::clear() {
    for (auto& [event_type, subscriptions] : event_subscriptions) {
        for (auto& sub : subscriptions) {
            if (sub.callback) {
                sub.callback->Release();
            }
        }
    }
    event_subscriptions.clear();

    auto view = world->scene.entity_registry.view<components::Script>();
    for (auto [e, s] : view.each()) {
        for (ScriptInstance& instance : s.scripts) {
            auto obj = (asIScriptObject*)instance.object;
            if (obj) {
                obj->Release();
            }
        }
    }
}

const std::unordered_map<uint32_t, Script>& ScriptSystem::get_scripts() {
    return scripts;
}

void ScriptSystem::construct_script_objects(Entity entity, components::Script& s) {
    for (ScriptInstance& instance : s.scripts) {
        if (scripts.contains(instance.script_id)) {
            auto& script = scripts.at(instance.script_id);
            if (script.valid && script.constructor && !instance.object) {
                context->Prepare(script.constructor);
                int r = context->Execute();
                if (r != asEXECUTION_FINISHED && r == asEXECUTION_EXCEPTION) {
                    spdlog::error(
                        "Exception executing script: {} ({}:{})",
                        context->GetExceptionString(),
                        context->GetExceptionFunction()->GetDeclaration(),
                        context->GetExceptionLineNumber()
                    );
                } else if (r == asEXECUTION_FINISHED) {
                    instance.object = *((asIScriptObject**)context->GetAddressOfReturnValue());
                    auto object     = ((asIScriptObject*)instance.object);
                    object->AddRef();

                    asITypeInfo* type        = object->GetObjectType();
                    uint32_t*    node_id_ptr = (uint32_t*)object->GetAddressOfProperty(node_id_property_index);
                    *node_id_ptr             = (uint32_t)entity;

                    for (auto& property : script.editable_properties) {
                        auto it = instance.property_overrides.find(property.name);

                        if (it != instance.property_overrides.end()) {
                            auto id = object->GetPropertyTypeId(property.index);

                            if (property.type_id == id) {
                                auto prop = object->GetAddressOfProperty(property.index);
                                std::visit(
                                    [prop](auto&& value) {
                                        using T                = std::decay_t<decltype(value)>;
                                        *static_cast<T*>(prop) = value;
                                    },
                                    it->second.value
                                );
                            }
                        }
                    }
                }
                context->Unprepare();
            }
        }
    }
}

void ScriptSystem::call_on_start(const components::Script& s) {
    for (const ScriptInstance& instance : s.scripts) {
        if (scripts.contains(instance.script_id)) {
            auto& script = scripts.at(instance.script_id);
            if (script.valid && script.on_start && instance.object) {
                context->Prepare(script.on_start);
                context->SetObject(instance.object);
                int r = context->Execute();
                if (r != asEXECUTION_FINISHED && r == asEXECUTION_EXCEPTION) {
                    spdlog::error(
                        "Exception executing script: {} ({}:{})",
                        context->GetExceptionString(),
                        context->GetExceptionFunction()->GetDeclaration(),
                        context->GetExceptionLineNumber()
                    );
                }
                context->Unprepare();
            }
        }
    }
}

void ScriptSystem::call_on_update(const components::Script& s, float delta) {
    for (const ScriptInstance& instance : s.scripts) {
        if (scripts.contains(instance.script_id)) {
            auto& script = scripts.at(instance.script_id);
            if (script.valid && script.on_update && instance.object) {
                context->Prepare(script.on_update);
                context->SetObject(instance.object);
                context->SetArgFloat(0, delta);
                int r = context->Execute();
                if (r != asEXECUTION_FINISHED && r == asEXECUTION_EXCEPTION) {
                    spdlog::error(
                        "Exception executing script: {} ({}:{})",
                        context->GetExceptionString(),
                        context->GetExceptionFunction()->GetDeclaration(),
                        context->GetExceptionLineNumber()
                    );
                }
                context->Unprepare();
            }
        }
    }
}

void ScriptSystem::call_on_fixed_update(const components::Script& s, float delta) {
    for (const ScriptInstance& instance : s.scripts) {
        if (scripts.contains(instance.script_id)) {
            auto& script = scripts.at(instance.script_id);
            if (script.valid && script.on_fixed_update && instance.object) {
                context->Prepare(script.on_fixed_update);
                context->SetObject(instance.object);
                context->SetArgFloat(0, delta);
                int r = context->Execute();
                if (r != asEXECUTION_FINISHED && r == asEXECUTION_EXCEPTION) {
                    spdlog::error(
                        "Exception executing script: {} ({}:{})",
                        context->GetExceptionString(),
                        context->GetExceptionFunction()->GetDeclaration(),
                        context->GetExceptionLineNumber()
                    );
                }
                context->Unprepare();
            }
        }
    }
}

Entity ScriptSystem::find_node(const std::string& name) {
    auto   view   = world->scene.entity_registry.view<components::Name>();
    Entity entity = entt::null;
    for (auto [e, n] : view.each()) {
        if (n.name == name) {
            entity = e;
            break;
        }
    }

    return entity;
}

void ScriptSystem::delete_node(Entity node, bool delete_children) {
    world->scene.delete_node(node, delete_children);
}

bool ScriptSystem::cast_ray(glm::vec3 origin, glm::vec3 dir, float max_distance, float& t, uint32_t& entity) {
    JPH::RRayCast ray;
    ray.mOrigin    = JPH::Vec3(origin.x, origin.y, origin.z);
    ray.mDirection = JPH::Vec3(dir.x, dir.y, dir.z) * max_distance;

    JPH::RayCastResult result;

    bool hit = world->physics.system.GetNarrowPhaseQuery().CastRay(
        ray,
        result,
        world->physics.system.GetDefaultBroadPhaseLayerFilter(Layers::MOVING | Layers::NON_MOVING),
        world->physics.system.GetDefaultLayerFilter(Layers::MOVING | Layers::NON_MOVING)
    );

    t      = 0.0f;
    entity = entt::null;

    if (hit) {
        t = result.mFraction;

        auto view = world->scene.entity_registry.view<components::Physics>();
        for (auto [e, p] : view.each()) {
            if (p.body_id == result.mBodyID) {
                entity = (uint32_t)e;
                break;
            }
        }
    }

    return hit;
}

CScriptArray* ScriptSystem::find_nodes_with_tag(const std::string& tag) {
    auto nodes = world->scene.find_nodes_with_tag(tag);

    auto array_type = engine->GetTypeInfoByDecl("array<Node>");
    auto array      = CScriptArray::Create(array_type, nodes.size());

    for (size_t i = 0; i < nodes.size(); i++) {
        array->SetValue(i, &nodes[i]);
    }

    return array;
}

void ScriptSystem::subscribe_to_event(Entity entity, int event_type, class asIScriptFunction* callback) {
    if (!callback) {
        spdlog::debug("null callback when registering event");
        return;
    }
    callback->AddRef();

    event_subscriptions[event_type].push_back({
        .node     = (uint32_t)entity,
        .callback = callback,
    });
}

void ScriptSystem::unsubscribe_from_event(Entity entity, int event_type) {
    auto it = event_subscriptions.find(event_type);
    if (it == event_subscriptions.end()) {
        return;
    }

    auto& subs = it->second;
    subs.erase(
        std::remove_if(
            subs.begin(),
            subs.end(),
            [entity](const EventSubscription& sub) {
                if (sub.node == (uint32_t)entity) {
                    if (sub.callback) {
                        sub.callback->Release();
                    }
                    return true;
                }
                return false;
            }
        ),
        subs.end()
    );

    if (subs.empty()) {
        event_subscriptions.erase(it);
    }
}

void ScriptSystem::publish_event(asIScriptObject* msg) {
    if (!msg) {
        spdlog::debug("Trying to publish null event");
        return;
    }

    int event_type = get_event_type_from_message(msg);
    if (event_type < 0) {
        spdlog::debug("Failed to get event type from message");
        return;
    }

    auto it = event_subscriptions.find(event_type);
    if (it == event_subscriptions.end()) {
        return;
    }

    asITypeInfo* msg_type = msg->GetObjectType();

    for (auto& sub : it->second) {
        invoke_event_callback(sub, msg);
    }
}

void ScriptSystem::publish_event_to_node(Entity target, asIScriptObject* msg) {
    if (!msg) {
        spdlog::debug("Trying to publish null event");
        return;
    }

    int event_type = get_event_type_from_message(msg);
    if (event_type < 0) {
        spdlog::debug("Failed to get event type from message");
        return;
    }

    auto it = event_subscriptions.find(event_type);
    if (it == event_subscriptions.end()) {
        return;
    }

    asITypeInfo* msg_type = msg->GetObjectType();

    for (auto& sub : it->second) {
        if (sub.node == (uint32_t)target) {
            invoke_event_callback(sub, msg);
        }
    }
}

void ScriptSystem::publish_event_to_tag(const std::string& tag, class asIScriptObject* msg) {
    if (!msg) {
        spdlog::error("Cannot publish null event");
        return;
    }

    int event_type = get_event_type_from_message(msg);
    if (event_type < 0) {
        spdlog::error("Failed to get event type from message");
        return;
    }

    auto it = event_subscriptions.find(event_type);
    if (it == event_subscriptions.end()) {
        return;
    }

    asITypeInfo* msg_type = msg->GetObjectType();

    auto entities = world->scene.find_nodes_with_tag(tag);
    for (auto& sub : it->second) {
        for (auto e : entities) {
            if (sub.node == (uint32_t)e) {
                invoke_event_callback(sub, msg);
                break;
            }
        }
    }
}

uint32_t ScriptSystem::get_event_type_from_message(asIScriptObject* msg) {
    asIScriptFunction* get_type_func = msg->GetObjectType()->GetMethodByName("get_event_type");

    if (!get_type_func) {
        spdlog::error("Message type {} doesn't implement get_event_type()", msg->GetObjectType()->GetName());
        return -1;
    }

    asIScriptContext* ctx = engine->CreateContext();
    if (!ctx) {
        spdlog::error("Failed to create script context");
        return -1;
    }

    int r = ctx->Prepare(get_type_func);
    if (r < 0) {
        spdlog::error("Failed to prepare context");
        ctx->Release();
        return -1;
    }

    ctx->SetObject(msg);
    r = ctx->Execute();

    if (r != asEXECUTION_FINISHED) {
        spdlog::error("Failed to execute get_event_type()");
        ctx->Release();
        return -1;
    }

    int event_type = ctx->GetReturnDWord();
    ctx->Release();

    return event_type;
}

uint32_t ScriptSystem::get_node_id_from_object(class asIScriptObject* object) {
    uint32_t* node_id_ptr = (uint32_t*)object->GetAddressOfProperty(node_id_property_index);
    if (!node_id_ptr) {
        spdlog::error("Failed to get node_id from script object");
        return 0;
    }
    return *node_id_ptr;
}

void ScriptSystem::invoke_event_callback(const EventSubscription& sub, class asIScriptObject* msg) {
    asIScriptContext* ctx = engine->CreateContext();
    if (!ctx) {
        spdlog::error("Failed to create script context");
        return;
    }

    int r = ctx->Prepare(sub.callback);
    if (r < 0) {
        spdlog::error("Failed to prepare callback");
        ctx->Release();
        return;
    }

    ctx->SetArgObject(0, msg);

    r = ctx->Execute();
    if (r != asEXECUTION_FINISHED) {
        if (r == asEXECUTION_EXCEPTION) {
            spdlog::error("Exception in callback: {}", ctx->GetExceptionString());
        } else {
            spdlog::error("Callback execution failed with code {}", r);
        }
    }

    ctx->Release();
}

void ScriptSystem::register_node_type(class asIScriptEngine* engine) {
    engine->RegisterObjectType("Node", sizeof(Entity), asOBJ_VALUE | asOBJ_POD | asGetTypeTraits<Entity>());
    engine->RegisterObjectBehaviour(
        "Node",
        asBEHAVE_CONSTRUCT,
        "void f(uint)",
        asFUNCTION(+[](Entity id, void* mem) {
            new (mem) Entity(id);
        }),
        asCALL_CDECL_OBJLAST
    );
    engine->RegisterObjectMethod("Node", "uint get_id() const", asFUNCTION(node_to_handle), asCALL_CDECL_OBJFIRST);
    engine->RegisterObjectMethod("Node", "bool is_valid() const", asFUNCTION(node_is_valid), asCALL_CDECL_OBJFIRST);

    engine->RegisterObjectMethod("Node", "T@ get_component<T>()", asFUNCTION(node_get_component), asCALL_GENERIC);
    engine->RegisterObjectMethod("Node", "Node clone()", asFUNCTION(clone_node), asCALL_CDECL_OBJFIRST);

    engine->RegisterObjectMethod("Node", "Node find_child(string &in)", asFUNCTION(find_child), asCALL_CDECL_OBJFIRST);

    engine->RegisterObjectMethod("Node", "bool has_tag(string &in)", asFUNCTION(node_has_tag), asCALL_CDECL_OBJFIRST);
}
