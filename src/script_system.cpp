#include "script_system.hpp"
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
} // namespace

ScriptSystem::ScriptSystem(Scene& scene, JPH::PhysicsSystem& physics_system, InputSystem& input_system)
    : scene(scene), physics_system(physics_system), input_system(input_system) {
    engine = asCreateScriptEngine();

    engine->SetMessageCallback(asFUNCTION(script_message_callback), 0, asCALL_CDECL);
    auto default_namespace = engine->GetDefaultNamespace();

    RegisterScriptArray(engine, true);
    RegisterStdString(engine);
    RegisterStdStringUtils(engine);
    RegisterScriptHandle(engine);
    RegisterScriptWeakRef(engine);
    RegisterScriptDictionary(engine);
    RegisterScriptGrid(engine);

    engine->RegisterInterface("INode");

    register_glm_types(engine);

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
        &input_system
    );

    engine->RegisterGlobalFunction(
        "bool is_key_pressed(int)",
        asMETHOD(InputSystem, InputSystem::is_key_pressed),
        asCALL_THISCALL_ASGLOBAL,
        &input_system
    );

    engine->RegisterGlobalFunction(
        "bool is_key_just_pressed(int)",
        asMETHOD(InputSystem, InputSystem::is_key_just_pressed),
        asCALL_THISCALL_ASGLOBAL,
        &input_system
    );

    engine->RegisterGlobalFunction(
        "bool is_button_pressed(int)",
        asMETHOD(InputSystem, InputSystem::is_button_pressed),
        asCALL_THISCALL_ASGLOBAL,
        &input_system
    );

    engine->RegisterGlobalFunction(
        "bool is_button_just_pressed(int)",
        asMETHOD(InputSystem, InputSystem::is_button_just_pressed),
        asCALL_THISCALL_ASGLOBAL,
        &input_system
    );

    engine->SetDefaultNamespace("World");
    engine->RegisterGlobalFunction(
        "uint clone_node(string &in)", asMETHOD(ScriptSystem, ScriptSystem::clone_node), asCALL_THISCALL_ASGLOBAL, this
    );

    engine->RegisterGlobalFunction(
        "bool cast_ray(vec3, vec3, float, float &out, uint &out)",
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
        "vec3 get_node_position(uint)",
        asMETHOD(ScriptSystem, ScriptSystem::get_node_position),
        asCALL_THISCALL_ASGLOBAL,
        this
    );

    engine->RegisterGlobalFunction(
        "float get_node_scale(uint)",
        asMETHOD(ScriptSystem, ScriptSystem::get_node_scale),
        asCALL_THISCALL_ASGLOBAL,
        this
    );

    engine->RegisterGlobalFunction(
        "void set_node_position(uint, vec3)",
        asMETHOD(ScriptSystem, ScriptSystem::set_node_position),
        asCALL_THISCALL_ASGLOBAL,
        this
    );

    engine->RegisterGlobalFunction(
        "void set_node_scale(uint, float)",
        asMETHOD(ScriptSystem, ScriptSystem::set_node_scale),
        asCALL_THISCALL_ASGLOBAL,
        this
    );

    engine->RegisterGlobalFunction(
        "void set_node_physics_body_box(uint, vec3)",
        asMETHOD(ScriptSystem, ScriptSystem::set_node_physics_body_box),
        asCALL_THISCALL_ASGLOBAL,
        this
    );

    engine->RegisterGlobalFunction(
        "uint get_node(string &in)", asMETHOD(ScriptSystem, ScriptSystem::get_node), asCALL_THISCALL_ASGLOBAL, this
    );

    engine->SetDefaultNamespace("Random");
    engine->RegisterGlobalFunction("float random_float(float, float)", asFUNCTION(script_rand_float), asCALL_CDECL);
    engine->RegisterGlobalFunction("int random_int(int, int)", asFUNCTION(script_rand_int), asCALL_CDECL);

    engine->SetDefaultNamespace(default_namespace);

    context = engine->CreateContext();
}

void ScriptSystem::load_scripts(const std::filesystem::path& path) {
    auto hash_script = [](const std::filesystem::path& path) -> uint32_t {
        uint32_t hash = 0;

        for (auto& it : path.string()) {
            hash = 37 * hash + 17 + static_cast<char>(it);
        }

        return hash;
    };

    for (const auto& entry : std::filesystem::recursive_directory_iterator(path)) {
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

        asIScriptModule* script_module =
            engine->GetModule(entry.path().filename().string().c_str(), asGM_ALWAYS_CREATE);

        int r;
        r = script_module->AddScriptSection(entry.path().filename().string().c_str(), source.c_str(), source.length());
        if (r < 0) {
            spdlog::error("Failed to load script {}", entry.path().string());
            continue;
        }

        r = script_module->Build();
        if (r < 0) {
            spdlog::error("Failed to build script {}", entry.path().string());
            continue;
        }

        asITypeInfo* type       = nullptr;
        int          type_count = script_module->GetObjectTypeCount();
        for (int i = 0; i < type_count; i++) {
            bool found_type     = false;
            type                = script_module->GetObjectTypeByIndex(i);
            int interface_count = type->GetInterfaceCount();
            for (int j = 0; j < interface_count; j++) {
                if (strcmp(type->GetInterface(j)->GetName(), "INode") == 0) {
                    found_type = true;
                    break;
                }
            }

            if (found_type == true) {
                break;
            }

            type = nullptr;
        }

        if (!type) {
            spdlog::warn("Script doesn't implement INode, it will not be loaded");
            continue;
        }

        std::string constructor_name = std::string(type->GetName()) + "@ " + std::string(type->GetName()) + "()";
        auto        constructor      = type->GetFactoryByDecl(constructor_name.c_str());
        if (!constructor) {
            spdlog::warn("Script doesn't have a default constructor, it will not be loaded");
            continue;
        }

        auto on_update       = type->GetMethodByDecl("void update(float)");
        auto on_fixed_update = type->GetMethodByDecl("void fixed_update(float)");

        auto hash = hash_script(entry.path().filename());

        scripts.insert(
            {hash,
             Script{
                 .name            = entry.path().filename().string(),
                 .source          = source,
                 .constructor     = constructor,
                 .on_update       = on_update,
                 .on_fixed_update = on_fixed_update,
             }}
        );
    }
}

void ScriptSystem::clear() {
    auto view = scene.entity_registry.view<components::Script>();
    for (auto [e, s] : view.each()) {
        ((asIScriptObject*)s.object)->Release();
    }
}

const std::unordered_map<uint32_t, Script>& ScriptSystem::get_scripts() {
    return scripts;
}

void ScriptSystem::initialize(components::Script& s) {
    if (scripts.contains(s.script_id)) {
        auto& script = scripts.at(s.script_id);
        if (script.constructor && !s.object) {
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
                s.object = *((asIScriptObject**)context->GetAddressOfReturnValue());
                ((asIScriptObject*)s.object)->AddRef();
            }
            context->Unprepare();
        }
    }
}

void ScriptSystem::call_on_update(const components::Script& s, float delta) {
    if (scripts.contains(s.script_id)) {
        auto& script = scripts.at(s.script_id);
        if (script.on_update && s.object) {
            context->Prepare(script.on_update);
            context->SetObject(s.object);
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

void ScriptSystem::call_on_fixed_update(const components::Script& s, float delta) {
    if (scripts.contains(s.script_id)) {
        auto& script = scripts.at(s.script_id);
        if (script.on_fixed_update && s.object) {
            context->Prepare(script.on_fixed_update);
            context->SetObject(s.object);
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

Entity ScriptSystem::clone_node(const std::string& name) {
    Entity base = get_node(name);

    if (base != entt::null) {
        return clone_node_internal(base);
    }

    return entt::null;
}

Entity ScriptSystem::clone_node_internal(Entity e) {
    auto src_name = scene.get_component<components::Name>(e);

    Entity new_entity = scene.create_entity(src_name->name + "_clone");

    auto src_physics = scene.get_component<components::Physics>(e);
    if (src_physics) {
        auto& p              = scene.add_component<components::Physics>(new_entity);
        auto& body_interface = physics_system.GetBodyInterface();

        JPH::EMotionType motion_type = body_interface.GetMotionType(src_physics->body_id);
        JPH::Vec3        position    = body_interface.GetPosition(src_physics->body_id);
        JPH::Quat        rotation    = body_interface.GetRotation(src_physics->body_id);

        const JPH::Shape* shape = body_interface.GetShape(src_physics->body_id);

        JPH::BodyCreationSettings settings(
            shape,
            JPH::RVec3(position),
            rotation,
            motion_type,
            motion_type == JPH::EMotionType::Static ? Layers::NON_MOVING : Layers::MOVING
        );

        settings.mFriction      = body_interface.GetFriction(src_physics->body_id);
        settings.mGravityFactor = body_interface.GetGravityFactor(src_physics->body_id);

        JPH::Body*  new_body = body_interface.CreateBody(settings);
        JPH::BodyID new_id   = new_body->GetID();

        body_interface.AddBody(new_id, JPH::EActivation::Activate);

        p.body_id   = new_id;
        p.is_static = motion_type == JPH::EMotionType::Static;
    }

    auto src_mesh = scene.get_component<components::Mesh>(e);
    if (src_mesh) {
        auto& m = scene.add_component<components::Mesh>(new_entity);
        m.mesh  = src_mesh->mesh;
    }

    auto src_parent = scene.get_component<components::Parent>(e);
    if (src_parent) {
        scene.set_node_parent(new_entity, src_parent->parent);
    }

    auto src_children = scene.get_component<components::Children>(e);
    if (src_children) {
        for (Entity child : src_children->children) {
            Entity new_child = clone_node_internal(child);
            scene.set_node_parent(new_child, new_entity);
        }
    }

    return new_entity;
}

Entity ScriptSystem::get_node(const std::string& name) {
    auto   view   = scene.entity_registry.view<components::Name>();
    Entity entity = entt::null;
    for (auto [e, n] : view.each()) {
        if (n.name == name) {
            entity = e;
            break;
        }
    }

    return entity;
}

bool ScriptSystem::cast_ray(glm::vec3 origin, glm::vec3 dir, float max_distance, float& t, uint32_t& entity) {
    JPH::RRayCast ray;
    ray.mOrigin    = JPH::Vec3(origin.x, origin.y, origin.z);
    ray.mDirection = JPH::Vec3(dir.x, dir.y, dir.z) * max_distance;

    JPH::RayCastResult result;

    bool hit = this->physics_system.GetNarrowPhaseQuery().CastRay(
        ray,
        result,
        physics_system.GetDefaultBroadPhaseLayerFilter(Layers::MOVING | Layers::NON_MOVING),
        physics_system.GetDefaultLayerFilter(Layers::MOVING | Layers::NON_MOVING)
    );

    t      = 0.0f;
    entity = entt::null;

    if (hit) {
        t = result.mFraction;

        auto view = scene.entity_registry.view<components::Physics>();
        for (auto [e, p] : view.each()) {
            if (p.body_id == result.mBodyID) {
                entity = (uint32_t)e;
                break;
            }
        }
    }

    return hit;
}

void ScriptSystem::set_node_position(Entity entity, glm::vec3 position) {
    scene.get_component<components::Transform>(entity)->position = position;
}

void ScriptSystem::set_node_scale(Entity entity, float scale) {
    scene.get_component<components::Transform>(entity)->scale = scale;
}

glm::vec3 ScriptSystem::get_node_position(Entity entity) {
    return scene.get_component<components::Transform>(entity)->position;
}

float ScriptSystem::get_node_scale(Entity entity) {
    return scene.get_component<components::Transform>(entity)->scale;
}

void ScriptSystem::set_node_physics_body_box(Entity entity, glm::vec3 half_extents) {
    auto p = scene.get_component<components::Physics>(entity);

    if (!p) {
        return;
    }

    auto t = scene.get_component<components::Transform>(entity);

    if (!p->body_id.IsInvalid()) {
        physics_system.GetBodyInterface().RemoveBody(p->body_id);
        physics_system.GetBodyInterface().DestroyBody(p->body_id);
    }

    auto body_settings = JPH::BodyCreationSettings(
        new JPH::BoxShape(JPH::RVec3(half_extents.x, half_extents.y, half_extents.z)),
        JPH::Vec3(t->position.x, t->position.y, t->position.z),
        JPH::Quat::sIdentity(),
        JPH::EMotionType::Dynamic,
        Layers::MOVING
    );
    body_settings.mFriction                     = 0.7f;
    body_settings.mRestitution                  = 0.1f;
    body_settings.mOverrideMassProperties       = JPH::EOverrideMassProperties::CalculateInertia;
    body_settings.mMassPropertiesOverride.mMass = 1.0f;

    JPH::BodyID body_id = physics_system.GetBodyInterface().CreateAndAddBody(body_settings, JPH::EActivation::Activate);
    p->body_id          = body_id;
    p->is_static        = false;
    p->last_scale       = t->scale;
}
