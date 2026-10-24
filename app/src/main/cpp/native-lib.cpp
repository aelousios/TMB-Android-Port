#include <jni.h>
#include <GLES2/gl2.h>
#include <android/log.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <memory>
#include <utility>
#include <sstream>
#include <string>
#include <vector>

#include "iso9660.h"
#include "ps2_memory.h"

namespace {
constexpr const char* kLogTag = "TMBNativePortLab";
constexpr float kPi = 3.14159265358979323846f;
constexpr int kControlLeft = 0;
constexpr int kControlRight = 1;
constexpr int kControlAccelerate = 2;
constexpr int kControlBrake = 3;
constexpr int kControlFire = 4;
constexpr int kControlSpecial = 5;

struct State {
    float x = 0.0f;
    float z = -4.0f;
    float yaw = 0.0f;
    float speed = 0.0f;
    float enemyX = 6.0f;
    float enemyZ = 7.0f;
    float enemyYaw = 2.2f;
    float enemyHealth = 100.0f;
    float fireCooldown = 0.0f;
    float specialCooldown = 0.0f;
    float damageFlash = 0.0f;
    unsigned int shotsFired = 0;
    unsigned int hits = 0;
    std::array<bool, 6> controls{};
};

std::mutex gMutex;
State gState;
std::mutex gPreparedMemoryMutex;
std::unique_ptr<tmbport::Ps2EeMemory> gPreparedMemory;
int gWidth = 1;
int gHeight = 1;
GLuint gProgram = 0;
GLint gPosition = -1;
GLint gColor = -1;
GLint gMvp = -1;

struct Vec3 { float x, y, z; };
using Mat4 = std::array<float, 16>;

Vec3 sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 normalize(Vec3 v) {
    const float length = std::sqrt(std::max(0.000001f, dot(v, v)));
    return {v.x / length, v.y / length, v.z / length};
}

Mat4 identity() {
    Mat4 m{};
    m[0] = m[5] = m[10] = m[15] = 1.0f;
    return m;
}

Mat4 multiply(const Mat4& a, const Mat4& b) {
    Mat4 out{};
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            out[col * 4 + row] =
                a[0 * 4 + row] * b[col * 4 + 0] +
                a[1 * 4 + row] * b[col * 4 + 1] +
                a[2 * 4 + row] * b[col * 4 + 2] +
                a[3 * 4 + row] * b[col * 4 + 3];
        }
    }
    return out;
}

Mat4 perspective(float fovRadians, float aspect, float nearPlane, float farPlane) {
    const float f = 1.0f / std::tan(fovRadians / 2.0f);
    Mat4 m{};
    m[0] = f / std::max(0.1f, aspect);
    m[5] = f;
    m[10] = (farPlane + nearPlane) / (nearPlane - farPlane);
    m[11] = -1.0f;
    m[14] = (2.0f * farPlane * nearPlane) / (nearPlane - farPlane);
    return m;
}

Mat4 lookAt(Vec3 eye, Vec3 center, Vec3 up) {
    const Vec3 f = normalize(sub(center, eye));
    const Vec3 s = normalize(cross(f, up));
    const Vec3 u = cross(s, f);
    Mat4 m = identity();
    m[0] = s.x; m[4] = s.y; m[8] = s.z;
    m[1] = u.x; m[5] = u.y; m[9] = u.z;
    m[2] = -f.x; m[6] = -f.y; m[10] = -f.z;
    m[12] = -dot(s, eye);
    m[13] = -dot(u, eye);
    m[14] = dot(f, eye);
    return m;
}

GLuint compileShader(GLenum type, const char* source) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled != GL_TRUE) {
        GLint length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        std::string log(static_cast<size_t>(std::max(1, length)), '\0');
        glGetShaderInfoLog(shader, length, nullptr, log.data());
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "Shader compile failed: %s", log.c_str());
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

bool initializeGl() {
    static constexpr char vertexSource[] =
        "attribute vec3 aPosition;\n"
        "attribute vec3 aColor;\n"
        "uniform mat4 uMvp;\n"
        "varying vec3 vColor;\n"
        "void main(){ gl_Position = uMvp * vec4(aPosition, 1.0); vColor = aColor; }\n";
    static constexpr char fragmentSource[] =
        "precision mediump float;\n"
        "varying vec3 vColor;\n"
        "void main(){ gl_FragColor = vec4(vColor, 1.0); }\n";
    GLuint vs = compileShader(GL_VERTEX_SHADER, vertexSource);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fragmentSource);
    if (vs == 0 || fs == 0) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return false;
    }
    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glBindAttribLocation(program, 0, "aPosition");
    glBindAttribLocation(program, 1, "aColor");
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        GLint length = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
        std::string log(static_cast<size_t>(std::max(1, length)), '\0');
        glGetProgramInfoLog(program, length, nullptr, log.data());
        __android_log_print(ANDROID_LOG_ERROR, kLogTag, "Program link failed: %s", log.c_str());
        glDeleteProgram(program);
        return false;
    }
    if (gProgram) glDeleteProgram(gProgram);
    gProgram = program;
    gPosition = glGetAttribLocation(gProgram, "aPosition");
    gColor = glGetAttribLocation(gProgram, "aColor");
    gMvp = glGetUniformLocation(gProgram, "uMvp");
    return gPosition >= 0 && gColor >= 0 && gMvp >= 0;
}

using Vertices = std::vector<float>;
void pushVertex(Vertices& v, Vec3 p, const std::array<float, 3>& c) {
    v.insert(v.end(), {p.x, p.y, p.z, c[0], c[1], c[2]});
}
void pushTriangle(Vertices& v, Vec3 a, Vec3 b, Vec3 c, const std::array<float, 3>& color) {
    pushVertex(v, a, color); pushVertex(v, b, color); pushVertex(v, c, color);
}
void pushLine(Vertices& v, Vec3 a, Vec3 b, const std::array<float, 3>& color) {
    pushVertex(v, a, color); pushVertex(v, b, color);
}

void appendBox(Vertices& v, float cx, float cy, float cz, float w, float h, float d,
               float yaw, const std::array<float, 3>& color) {
    const float hw = w * 0.5f, hh = h * 0.5f, hd = d * 0.5f;
    const std::array<Vec3, 8> local{{
        {-hw,-hh,-hd},{hw,-hh,-hd},{hw,hh,-hd},{-hw,hh,-hd},
        {-hw,-hh,hd},{hw,-hh,hd},{hw,hh,hd},{-hw,hh,hd}
    }};
    std::array<Vec3, 8> p{};
    const float cs = std::cos(yaw), sn = std::sin(yaw);
    for (size_t i = 0; i < local.size(); ++i) {
        p[i] = {cx + local[i].x * cs + local[i].z * sn,
                cy + local[i].y,
                cz - local[i].x * sn + local[i].z * cs};
    }
    static constexpr int faces[][3] = {
        {0,2,1},{0,3,2}, {4,5,6},{4,6,7}, {0,1,5},{0,5,4},
        {3,7,6},{3,6,2}, {1,2,6},{1,6,5}, {0,4,7},{0,7,3}
    };
    static constexpr float shade[] = {0.62f,0.62f,1.10f,1.10f,0.74f,0.74f,
                                      0.54f,0.54f,0.82f,0.82f,0.68f,0.68f};
    for (size_t i = 0; i < 12; ++i) {
        const auto toned = std::array<float,3>{
            std::min(1.0f, color[0] * shade[i]),
            std::min(1.0f, color[1] * shade[i]),
            std::min(1.0f, color[2] * shade[i])};
        pushTriangle(v, p[faces[i][0]], p[faces[i][1]], p[faces[i][2]], toned);
    }
}

void draw(const Vertices& vertices, GLenum mode, const Mat4& mvp) {
    if (vertices.empty() || !gProgram) return;
    glUseProgram(gProgram);
    glUniformMatrix4fv(gMvp, 1, GL_FALSE, mvp.data());
    glEnableVertexAttribArray(static_cast<GLuint>(gPosition));
    glEnableVertexAttribArray(static_cast<GLuint>(gColor));
    constexpr GLsizei stride = 6 * static_cast<GLsizei>(sizeof(float));
    glVertexAttribPointer(static_cast<GLuint>(gPosition), 3, GL_FLOAT, GL_FALSE, stride, vertices.data());
    glVertexAttribPointer(static_cast<GLuint>(gColor), 3, GL_FLOAT, GL_FALSE, stride, vertices.data() + 3);
    glDrawArrays(mode, 0, static_cast<GLsizei>(vertices.size() / 6));
    glDisableVertexAttribArray(static_cast<GLuint>(gPosition));
    glDisableVertexAttribArray(static_cast<GLuint>(gColor));
}

void update(State& s, float dt) {
    dt = std::clamp(dt, 0.0f, 0.05f);
    const float steer = (s.controls[kControlRight] ? 1.0f : 0.0f) - (s.controls[kControlLeft] ? 1.0f : 0.0f);
    if (s.controls[kControlAccelerate]) s.speed += 11.0f * dt;
    if (s.controls[kControlBrake]) s.speed -= 15.0f * dt;
    if (!s.controls[kControlAccelerate] && !s.controls[kControlBrake]) {
        const float drag = 2.7f * dt;
        if (s.speed > 0.0f) s.speed = std::max(0.0f, s.speed - drag);
        else s.speed = std::min(0.0f, s.speed + drag);
    }
    s.speed = std::clamp(s.speed, -7.0f, 18.0f);
    const float steeringFactor = 0.35f + std::min(1.0f, std::abs(s.speed) / 8.0f) * 0.75f;
    s.yaw += steer * steeringFactor * dt * (s.speed < -0.2f ? -1.0f : 1.0f);
    s.x += std::sin(s.yaw) * s.speed * dt;
    s.z += std::cos(s.yaw) * s.speed * dt;
    if (std::abs(s.x) > 22.5f || std::abs(s.z) > 22.5f) {
        s.x = std::clamp(s.x, -22.5f, 22.5f);
        s.z = std::clamp(s.z, -22.5f, 22.5f);
        s.speed *= -0.28f;
        s.damageFlash = 0.2f;
    }
    s.fireCooldown = std::max(0.0f, s.fireCooldown - dt);
    s.specialCooldown = std::max(0.0f, s.specialCooldown - dt);
    s.damageFlash = std::max(0.0f, s.damageFlash - dt);
    if (s.controls[kControlFire] && s.fireCooldown <= 0.0f) {
        ++s.shotsFired;
        s.fireCooldown = 0.25f;
        const float dx = s.enemyX - s.x;
        const float dz = s.enemyZ - s.z;
        const float forwardX = std::sin(s.yaw), forwardZ = std::cos(s.yaw);
        if (dx * forwardX + dz * forwardZ > 0.0f && std::sqrt(dx*dx + dz*dz) < 13.0f &&
            std::abs(dx * forwardZ - dz * forwardX) < 2.1f) {
            s.enemyHealth = std::max(0.0f, s.enemyHealth - 4.0f);
            ++s.hits;
        }
    }
    if (s.controls[kControlSpecial] && s.specialCooldown <= 0.0f) {
        s.specialCooldown = 1.0f;
        const float dx = s.enemyX - s.x;
        const float dz = s.enemyZ - s.z;
        if (std::sqrt(dx*dx + dz*dz) < 18.0f) {
            s.enemyHealth = std::max(0.0f, s.enemyHealth - 14.0f);
            ++s.hits;
        }
    }
    // Placeholder opponent motion: this is a controls/rendering test, not original TMB AI.
    const float t = static_cast<float>(s.shotsFired) * 0.001f; (void)t;
    s.enemyYaw += dt * 0.16f;
    s.enemyX += std::sin(s.enemyYaw) * dt * 0.9f;
    s.enemyZ += std::cos(s.enemyYaw) * dt * 0.9f;
    if (s.enemyHealth <= 0.0f) {
        s.enemyHealth = 100.0f;
        s.enemyX = -s.enemyX + 1.5f;
        s.enemyZ = -s.enemyZ;
    }
}

void renderFrame(float dt) {
    State state;
    {
        std::lock_guard<std::mutex> lock(gMutex);
        update(gState, dt);
        state = gState;
    }
    glViewport(0, 0, gWidth, gHeight);
    glClearColor(0.025f, 0.035f, 0.047f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    if (!gProgram) return;

    const Vec3 forward{std::sin(state.yaw), 0.0f, std::cos(state.yaw)};
    const Vec3 eye{state.x - forward.x * 11.0f, 8.5f, state.z - forward.z * 11.0f};
    const Vec3 target{state.x + forward.x * 2.0f, 0.8f, state.z + forward.z * 2.0f};
    const Mat4 view = lookAt(eye, target, {0.0f,1.0f,0.0f});
    const Mat4 projection = perspective(0.95f, static_cast<float>(gWidth) / static_cast<float>(std::max(1, gHeight)), 0.1f, 120.0f);
    const Mat4 mvp = multiply(projection, view);

    Vertices ground;
    ground.reserve(6);
    pushTriangle(ground, {-26.0f,-0.35f,-26.0f}, {26.0f,-0.35f,-26.0f}, {26.0f,-0.35f,26.0f}, {0.075f,0.092f,0.105f});
    pushTriangle(ground, {-26.0f,-0.35f,-26.0f}, {26.0f,-0.35f,26.0f}, {-26.0f,-0.35f,26.0f}, {0.075f,0.092f,0.105f});
    draw(ground, GL_TRIANGLES, mvp);

    Vertices grid;
    grid.reserve(1200);
    const std::array<float,3> gridColor{0.12f,0.155f,0.17f};
    const std::array<float,3> borderColor{0.5f,0.26f,0.12f};
    for (int i = -24; i <= 24; i += 2) {
        const float value = static_cast<float>(i);
        pushLine(grid, {value,-0.32f,-24.0f}, {value,-0.32f,24.0f}, gridColor);
        pushLine(grid, {-24.0f,-0.32f,value}, {24.0f,-0.32f,value}, gridColor);
    }
    pushLine(grid, {-24.0f,-0.30f,-24.0f},{24.0f,-0.30f,-24.0f},borderColor);
    pushLine(grid, {24.0f,-0.30f,-24.0f},{24.0f,-0.30f,24.0f},borderColor);
    pushLine(grid, {24.0f,-0.30f,24.0f},{-24.0f,-0.30f,24.0f},borderColor);
    pushLine(grid, {-24.0f,-0.30f,24.0f},{-24.0f,-0.30f,-24.0f},borderColor);
    draw(grid, GL_LINES, mvp);

    Vertices world;
    world.reserve(2400);
    const std::array<float,3> obstacleColor{0.24f,0.30f,0.32f};
    const std::array<std::array<float,2>,8> obstaclePositions{{
        {{-13.0f,-10.0f}},{{-10.0f,12.0f}},{{-2.0f,14.0f}},{{11.0f,13.0f}},
        {{15.0f,2.0f}},{{12.0f,-13.0f}},{{-12.0f,-15.0f}},{{1.0f,-18.0f}}
    }};
    for (size_t i = 0; i < obstaclePositions.size(); ++i) {
        const float x = obstaclePositions[i][0], z = obstaclePositions[i][1];
        appendBox(world, x, 0.15f, z, 1.7f, 0.9f, 1.7f, static_cast<float>(i) * 0.34f, obstacleColor);
        appendBox(world, x, 0.73f, z, 0.8f, 0.25f, 0.8f, static_cast<float>(i) * 0.34f, {0.30f,0.36f,0.37f});
    }
    // Arena barriers; intentionally simple geometry until an actual port renderer exists.
    appendBox(world, 0.0f, 0.6f, -24.3f, 49.0f, 1.5f, 0.6f, 0.0f, {0.18f,0.22f,0.25f});
    appendBox(world, 0.0f, 0.6f, 24.3f, 49.0f, 1.5f, 0.6f, 0.0f, {0.18f,0.22f,0.25f});
    appendBox(world, -24.3f, 0.6f, 0.0f, 0.6f, 1.5f, 49.0f, 0.0f, {0.18f,0.22f,0.25f});
    appendBox(world, 24.3f, 0.6f, 0.0f, 0.6f, 1.5f, 49.0f, 0.0f, {0.18f,0.22f,0.25f});

    appendBox(world, state.enemyX, 0.22f, state.enemyZ, 1.8f, 0.7f, 3.1f, state.enemyYaw, {0.67f,0.15f,0.12f});
    appendBox(world, state.enemyX, 0.62f, state.enemyZ, 1.2f, 0.28f, 1.15f, state.enemyYaw, {0.88f,0.29f,0.16f});
    appendBox(world, state.x, 0.22f, state.z, 1.9f, 0.72f, 3.3f, state.yaw,
              state.damageFlash > 0.0f ? std::array<float,3>{0.95f,0.72f,0.3f} : std::array<float,3>{0.10f,0.38f,0.67f});
    appendBox(world, state.x, 0.63f, state.z, 1.25f, 0.30f, 1.20f, state.yaw, {0.18f,0.57f,0.82f});
    // Wheels read clearly from the elevated chase camera.
    for (const float side : {-1.0f, 1.0f}) {
        for (const float frontBack : {-1.0f, 1.0f}) {
            const float localX = side * 0.92f, localZ = frontBack * 0.95f;
            const float wx = state.x + localX * std::cos(state.yaw) + localZ * std::sin(state.yaw);
            const float wz = state.z - localX * std::sin(state.yaw) + localZ * std::cos(state.yaw);
            appendBox(world, wx, 0.16f, wz, 0.28f, 0.38f, 0.68f, state.yaw, {0.035f,0.045f,0.055f});
        }
    }
    draw(world, GL_TRIANGLES, mvp);

    // Aiming reticle is world-space only in this foundation demo.
    Vertices effects;
    const float fx = state.x + std::sin(state.yaw) * 4.2f;
    const float fz = state.z + std::cos(state.yaw) * 4.2f;
    const std::array<float,3> reticleColor{0.95f,0.48f,0.18f};
    pushLine(effects, {fx-0.45f,0.07f,fz},{fx+0.45f,0.07f,fz},reticleColor);
    pushLine(effects, {fx,0.07f,fz-0.45f},{fx,0.07f,fz+0.45f},reticleColor);
    draw(effects, GL_LINES, mvp);
}

std::string hudText() {
    std::lock_guard<std::mutex> lock(gMutex);
    std::ostringstream out;
    out.setf(std::ios::fixed); out.precision(1);
    out << "TMB NATIVE PORT LAB  |  TECH DEMO\n"
        << "Speed " << std::abs(gState.speed) * 3.6f << " km/h   "
        << (gState.speed < -0.1f ? "REVERSE" : "DRIVE") << "\n"
        << "TARGET " << static_cast<int>(gState.enemyHealth) << "%   HITS " << gState.hits << "\n"
        << "ISO CHECK ONLY • ORIGINAL GAME NOT INTEGRATED";
    return out.str();
}
} // namespace

extern "C" JNIEXPORT void JNICALL
Java_dev_tmb_nativeport_NativeBridge_surfaceCreated(JNIEnv*, jobject) {
    if (!initializeGl()) __android_log_print(ANDROID_LOG_ERROR, kLogTag, "OpenGL setup failed");
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
}

extern "C" JNIEXPORT void JNICALL
Java_dev_tmb_nativeport_NativeBridge_surfaceChanged(JNIEnv*, jobject, jint width, jint height) {
    gWidth = std::max(1, static_cast<int>(width));
    gHeight = std::max(1, static_cast<int>(height));
    glViewport(0, 0, gWidth, gHeight);
}

extern "C" JNIEXPORT void JNICALL
Java_dev_tmb_nativeport_NativeBridge_renderFrame(JNIEnv*, jobject, jfloat deltaSeconds) {
    renderFrame(static_cast<float>(deltaSeconds));
}

extern "C" JNIEXPORT void JNICALL
Java_dev_tmb_nativeport_NativeBridge_setControl(JNIEnv*, jobject, jint control, jboolean pressed) {
    if (control < 0 || control >= static_cast<jint>(gState.controls.size())) return;
    std::lock_guard<std::mutex> lock(gMutex);
    gState.controls[static_cast<size_t>(control)] = pressed == JNI_TRUE;
}

extern "C" JNIEXPORT jstring JNICALL
Java_dev_tmb_nativeport_NativeBridge_getHudText(JNIEnv* env, jobject) {
    const std::string text = hudText();
    return env->NewStringUTF(text.c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_dev_tmb_nativeport_NativeBridge_inspectIsoFd(JNIEnv* env, jobject, jint fd) {
    const auto result = tmbport::inspect_iso_fd(static_cast<int>(fd));
    std::string message = result.message;

    // The selected ISO remains read-only. On successful ELF validation, stage
    // PT_LOAD data into a retained guest EE memory model for later runtime work.
    if (result.target_file_found && result.ps2_elf_valid) {
        auto candidate = std::make_unique<tmbport::Ps2EeMemory>();
        const std::uint64_t elfOffset = static_cast<std::uint64_t>(result.file_lba) * 2048U;
        const auto staged = tmbport::load_ps2_elf_into_ee_memory(
            static_cast<int>(fd), elfOffset, result.file_size, *candidate);
        if (staged.loaded) {
            {
                std::lock_guard<std::mutex> lock(gPreparedMemoryMutex);
                gPreparedMemory = std::move(candidate);
            }
            message += "\n\nGuest RAM staging passed: " + staged.message;
        } else {
            {
                std::lock_guard<std::mutex> lock(gPreparedMemoryMutex);
                gPreparedMemory.reset();
            }
            message += "\n\nGuest RAM staging failed: " + staged.message;
        }
    } else {
        std::lock_guard<std::mutex> lock(gPreparedMemoryMutex);
        gPreparedMemory.reset();
    }

    __android_log_print(ANDROID_LOG_INFO, kLogTag,
                        "ISO inspection: valid=%d ELF=%d size=%u",
                        result.valid_iso9660 ? 1 : 0, result.elf_magic_found ? 1 : 0, result.file_size);
    return env->NewStringUTF(message.c_str());
}
