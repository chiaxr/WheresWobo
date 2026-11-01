#include "rendering/toon_shader.h"

#include "rlgl.h"

namespace wobo {

namespace {

// Desktop GL and WebGL want different dialects; ask the live context which.
bool wants_glsl_100() {
    const int version = rlGetVersion();
    return version == RL_OPENGL_ES_20 || version == RL_OPENGL_ES_30;
}

const char* vs_330 = R"(#version 330
in vec3 vertexPosition;
in vec2 vertexTexCoord;
in vec3 vertexNormal;
in vec4 vertexColor;
uniform mat4 mvp;
out vec2 fragTexCoord;
out vec4 fragColor;
out vec3 fragNormal;
void main() {
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    fragNormal = vertexNormal;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

const char* vs_100 = R"(#version 100
attribute vec3 vertexPosition;
attribute vec2 vertexTexCoord;
attribute vec3 vertexNormal;
attribute vec4 vertexColor;
uniform mat4 mvp;
varying vec2 fragTexCoord;
varying vec4 fragColor;
varying vec3 fragNormal;
void main() {
    fragTexCoord = vertexTexCoord;
    fragColor = vertexColor;
    fragNormal = vertexNormal;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

// Three flat bands of light plus a cool tint in the darkest one, which is
// what gives cel shading its painted look rather than plain darkening.
#define WOBO_TOON_BODY                                                         \
    "    vec3 n = normalize(fragNormal);\n"                                    \
    "    float d = dot(n, normalize(lightDir));\n"                             \
    "    float band = d > 0.55 ? 1.0 : (d > 0.12 ? 0.84 : 0.68);\n"            \
    "    vec3 shade = mix(vec3(0.80, 0.84, 1.0), vec3(1.0), step(0.12, d));\n" \
    "    vec3 lit = base.rgb * band * shade;\n"                                \
    "    vec3 col = mix(lit, base.rgb, flatMix);\n"

const char* fs_330 = "#version 330\n"
    "in vec2 fragTexCoord;\n"
    "in vec4 fragColor;\n"
    "in vec3 fragNormal;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec4 colDiffuse;\n"
    "uniform vec3 lightDir;\n"
    "uniform float flatMix;\n"
    "out vec4 finalColor;\n"
    "void main() {\n"
    "    vec4 base = texture(texture0, fragTexCoord) * colDiffuse * fragColor;\n"
    WOBO_TOON_BODY
    "    finalColor = vec4(col, base.a);\n"
    "}\n";

const char* fs_100 = "#version 100\n"
    "precision mediump float;\n"
    "varying vec2 fragTexCoord;\n"
    "varying vec4 fragColor;\n"
    "varying vec3 fragNormal;\n"
    "uniform sampler2D texture0;\n"
    "uniform vec4 colDiffuse;\n"
    "uniform vec3 lightDir;\n"
    "uniform float flatMix;\n"
    "void main() {\n"
    "    vec4 base = texture2D(texture0, fragTexCoord) * colDiffuse * fragColor;\n"
    WOBO_TOON_BODY
    "    gl_FragColor = vec4(col, base.a);\n"
    "}\n";

}  // namespace

void ToonShader::load() {
    if (loaded_) return;
    const bool es = wants_glsl_100();
    shader_ = LoadShaderFromMemory(es ? vs_100 : vs_330, es ? fs_100 : fs_330);
    if (shader_.id == 0 || shader_.id == rlGetShaderIdDefault()) return;
    loc_light_dir_ = GetShaderLocation(shader_, "lightDir");
    loc_flat_ = GetShaderLocation(shader_, "flatMix");
    loaded_ = true;
}

void ToonShader::unload() {
    if (!loaded_) return;
    UnloadShader(shader_);
    shader_ = Shader{};
    loaded_ = false;
}

void ToonShader::begin() const {
    if (!loaded_) return;
    BeginShaderMode(shader_);
    const float dir[3] = {kLightDir.x, kLightDir.y, kLightDir.z};
    SetShaderValue(shader_, loc_light_dir_, dir, SHADER_UNIFORM_VEC3);
    set_flat(false);
}

void ToonShader::end() const {
    if (!loaded_) return;
    EndShaderMode();
}

void ToonShader::set_flat(bool flat) const {
    if (!loaded_) return;
    rlDrawRenderBatchActive();
    const float v = flat ? 1.0f : 0.0f;
    SetShaderValue(shader_, loc_flat_, &v, SHADER_UNIFORM_FLOAT);
}

}  // namespace wobo
