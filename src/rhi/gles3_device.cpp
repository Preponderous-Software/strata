// OpenGL ES 3.0 / WebGL 2 backend for GpuDevice.
//
// The same contract as the GL 4.5 device, without direct state access: every
// object is edited by binding it. What GLES 3.0 lacks is emulated here so the
// passes above stay unchanged:
//
//   * base vertex / base instance: folded into the attribute pointers, which
//     are set at draw time from the bound vertex buffers;
//   * binding qualifiers in GLSL: set after linking (see glsl_es.hpp);
//   * cube map arrays: one cube map per layer group, each on its own texture
//     unit, chosen in the shader by a branch;
//   * depth clamp: EXT_depth_clamp when the context has it, otherwise the
//     shaders clamp depth themselves (see glsl_es.hpp);
//   * debug output and object labels: none.
//
// Under Emscripten the GL calls are Emscripten's WebGL 2 bindings; natively
// they are glad's GLES 3.0 entry points (tests run this device on Mesa).
#if defined(__EMSCRIPTEN__)
#include <GLES3/gl3.h>
#include <emscripten/html5.h>
#else
#include <glad/gl.h>
#endif

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "core/error.hpp"
#include "core/log.hpp"
#include "rhi/glsl_es.hpp"
#include "rhi/gpu_device.hpp"

#ifndef GL_DEPTH_CLAMP_EXT
#define GL_DEPTH_CLAMP_EXT 0x864F
#endif

namespace strata::rhi {
namespace {

// A cube map array of up to this many cubes is emulated (STRATA_MAX_SHADOWED_POINT_LIGHTS).
constexpr int kMaxEmulatedCubes = 4;
// Cube 0 uses the unit the shader names; cubes 1.. use these. Strata's own
// units are 1 to 4.
constexpr int kFirstExtraCubeUnit = 5;
constexpr int kMaxBindings = 8;

struct BufferRecord {
    GLuint name = 0;
    size_t size = 0;
    bool dynamic = false;
    bool index = false;
};

struct TextureRecord {
    GLuint name = 0;            // all but emulated cube arrays
    std::vector<GLuint> cubes;  // an emulated cube map array: one cube map per layer
    GLenum target = GL_TEXTURE_2D;
    TextureDesc desc;
};

struct TargetRecord {
    GLuint framebuffer = 0;
};

struct VertexBinding {
    GLuint buffer = 0;
    size_t offset = 0;
};

struct PipelineRecord {
    GLuint program = 0;
    GLuint vertexArray = 0;
    PipelineDesc desc;  // shader source pointers are not kept valid; only state is read
    std::vector<VertexBufferLayout> buffers;
    std::vector<VertexAttribute> attributes;
    std::array<VertexBinding, kMaxBindings> bindings{};
};

template <typename Record>
class Table {
public:
    uint32_t add(const Record& record) {
        for (size_t i = 0; i < records_.size(); ++i) {
            if (!live_[i]) {
                records_[i] = record;
                live_[i] = true;
                return static_cast<uint32_t>(i + 1);
            }
        }
        records_.push_back(record);
        live_.push_back(true);
        return static_cast<uint32_t>(records_.size());
    }
    Record& get(uint32_t handle) {
        if (handle == 0 || handle > records_.size() || !live_[handle - 1]) {
            fail(STRATA_ERROR_INTERNAL, "stale or empty GPU handle %u", handle);
        }
        return records_[handle - 1];
    }
    const Record& get(uint32_t handle) const { return const_cast<Table*>(this)->get(handle); }
    void remove(uint32_t handle) {
        get(handle);
        live_[handle - 1] = false;
    }
    template <typename Fn>
    void forEach(Fn fn) {
        for (size_t i = 0; i < records_.size(); ++i) {
            if (live_[i]) fn(records_[i]);
        }
    }

private:
    std::vector<Record> records_;
    std::vector<bool> live_;
};

GLuint compileShader(GLenum type, const std::string& source, const char* pipelineName) {
    GLuint shader = glCreateShader(type);
    const char* text = source.c_str();
    glShaderSource(shader, 1, &text, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096] = {};
        glGetShaderInfoLog(shader, sizeof log, nullptr, log);
        glDeleteShader(shader);
        fail(STRATA_ERROR_GPU, "%s: %s shader (GLSL ES 3.00) failed to compile: %s", pipelineName,
             type == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
    }
    return shader;
}

bool hasDepthClampExtension() {
    if (const char* off = std::getenv("STRATA_GLES_NO_DEPTH_CLAMP")) {
        if (off[0] != '\0' && off[0] != '0') return false;
    }
#if defined(__EMSCRIPTEN__)
    return emscripten_webgl_enable_extension(emscripten_webgl_get_current_context(), "EXT_depth_clamp");
#else
    GLint count = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &count);
    for (GLint i = 0; i < count; ++i) {
        const char* name = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, static_cast<GLuint>(i)));
        if (name && std::strcmp(name, "GL_EXT_depth_clamp") == 0) return true;
    }
    return false;
#endif
}

// Texture units Strata binds (0 unused, 1 cascades, 2 point shadows, 3 the
// overlay font, 4 a mesh's texture, 5-7 the other point shadow cubes), saved
// and restored for a host context.
constexpr int kHostTextureUnits = kFirstExtraCubeUnit + kMaxEmulatedCubes - 1;
// Textures are bound to a scratch unit while they are created, so no unit a
// pass uses is disturbed; no shader samples it. It is the device's last
// fragment unit, so a device needs only one more than Strata binds. It used
// to be fixed at 15, which demanded 16 units: WebGL 2's minimum, yet some
// phone browsers report fewer (13 was seen on a real phone).
constexpr int kMinFragmentTextureUnits = kHostTextureUnits + 1;
// Uniform buffer slots Strata binds (0 frame, 1 pass, 2 point lights, 3 mesh).
constexpr int kHostUniformSlots = 4;

struct HostState {
    GLint program = 0, vertexArray = 0, drawFramebuffer = 0, readFramebuffer = 0;
    GLint viewport[4] = {0, 0, 0, 0};
    GLboolean depthTest = GL_FALSE, cullFace = GL_FALSE, blend = GL_FALSE, scissor = GL_FALSE;
    GLboolean polygonOffsetFill = GL_FALSE, depthClamp = GL_FALSE, depthMask = GL_TRUE;
    GLboolean colorMask[4] = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
    GLint depthFunc = GL_LESS, cullFaceMode = GL_BACK, frontFace = GL_CCW;
    GLfloat polygonOffsetFactor = 0, polygonOffsetUnits = 0;
    GLint activeTexture = GL_TEXTURE0;
    GLint blendSrcRgb = GL_ONE, blendDstRgb = GL_ZERO, blendSrcAlpha = GL_ONE, blendDstAlpha = GL_ZERO;
    GLint blendEquationRgb = GL_FUNC_ADD, blendEquationAlpha = GL_FUNC_ADD;
    GLint texture2D[kHostTextureUnits] = {}, texture2DArray[kHostTextureUnits] = {},
          textureCube[kHostTextureUnits] = {};
    GLint uniformBuffers[kHostUniformSlots] = {};
    GLint arrayBuffer = 0;
    GLint unpackAlignment = 4, packAlignment = 4;
};

class Gles3Device final : public GpuDevice {
public:
    explicit Gles3Device(bool debugOutput) {
        (void)debugOutput;  // GLES 3.0 has no core debug output
        const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
        const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
        GLint alignment = 256;
        glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &alignment);
        uniformAlignment_ = static_cast<size_t>(alignment > 0 ? alignment : 256);
        GLint fragmentUnits = 0;
        glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &fragmentUnits);
        if (fragmentUnits < kMinFragmentTextureUnits) {
            fail(STRATA_ERROR_GPU, "the GLES device needs %d fragment texture units; this context has %d",
                 kMinFragmentTextureUnits, fragmentUnits);
        }
        scratchUnit_ = fragmentUnits - 1;
        depthClamp_ = hasDepthClampExtension();
        description_ = std::string(renderer ? renderer : "?") + " | " + (version ? version : "?") +
                       (depthClamp_ ? "" : " | depth clamp emulated");
        glGenVertexArrays(1, &emptyVertexArray_);
    }

    ~Gles3Device() override {
        pipelines_.forEach([](PipelineRecord& p) {
            glDeleteProgram(p.program);
            glDeleteVertexArrays(1, &p.vertexArray);
        });
        targets_.forEach([](TargetRecord& t) { glDeleteFramebuffers(1, &t.framebuffer); });
        textures_.forEach([](TextureRecord& t) { deleteTexture(t); });
        buffers_.forEach([](BufferRecord& b) { glDeleteBuffers(1, &b.name); });
        glDeleteVertexArrays(1, &emptyVertexArray_);
    }

    std::string description() const override { return description_; }
    size_t uniformBufferAlignment() const override { return uniformAlignment_; }

    // Vertex and uniform buffers are created and edited through
    // COPY_WRITE_BUFFER, which binds nothing a draw reads. WebGL fixes a
    // buffer's kind at its first binding, so index buffers are made (and
    // edited) only through ELEMENT_ARRAY_BUFFER, with no vertex array bound.
    BufferHandle createBuffer(size_t bytes, const void* initial, bool dynamic, const char* debugName) override {
        (void)debugName;
        BufferRecord record;
        glGenBuffers(1, &record.name);
        record.size = bytes;
        record.dynamic = dynamic;
        upload(record, 0, bytes, initial, true);
        return BufferHandle{buffers_.add(record)};
    }

    BufferHandle createIndexBuffer(size_t bytes, const void* initial, const char* debugName) override {
        (void)debugName;
        BufferRecord record;
        glGenBuffers(1, &record.name);
        record.size = bytes;
        record.index = true;
        upload(record, 0, bytes, initial, true);
        return BufferHandle{buffers_.add(record)};
    }

    void resizeBuffer(BufferHandle buffer, size_t bytes) override {
        BufferRecord& record = buffers_.get(buffer.value);
        record.size = bytes;
        upload(record, 0, bytes, nullptr, true);
    }

    size_t bufferSize(BufferHandle buffer) const override { return buffers_.get(buffer.value).size; }

    void updateBuffer(BufferHandle buffer, size_t offset, size_t bytes, const void* data) override {
        if (bytes == 0) return;
        BufferRecord& record = buffers_.get(buffer.value);
        if (offset + bytes > record.size) {
            fail(STRATA_ERROR_INTERNAL, "buffer update of %zu bytes at %zu overruns %zu", bytes, offset, record.size);
        }
        upload(record, offset, bytes, data, false);
    }

    TextureHandle createTexture(const TextureDesc& desc) override {
        TextureRecord record;
        record.desc = desc;
        record.desc.initialData = nullptr;
        const bool cube = desc.cube && desc.layers > 0;
        const GLenum format = desc.format == TextureFormat::RGBA8 ? GL_RGBA8
                              : desc.format == TextureFormat::R8 ? GL_R8
                                                                 : GL_DEPTH_COMPONENT32F;
        const bool mipmaps = desc.mipmaps && desc.layers == 0 && desc.format == TextureFormat::RGBA8;
        GLsizei levels = 1;
        if (mipmaps) {
            for (int size = std::max(desc.width, desc.height); size > 1; size /= 2) ++levels;
        }
        glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(scratchUnit_));
        if (cube) {
            if (desc.layers > kMaxEmulatedCubes) {
                fail(STRATA_ERROR_GPU, "the GLES device emulates cube map arrays of at most %d cubes (asked for %d)",
                     kMaxEmulatedCubes, desc.layers);
            }
            record.target = GL_TEXTURE_CUBE_MAP;
            record.cubes.resize(static_cast<size_t>(desc.layers));
            glGenTextures(desc.layers, record.cubes.data());
            for (GLuint name : record.cubes) {
                glBindTexture(GL_TEXTURE_CUBE_MAP, name);
                glTexStorage2D(GL_TEXTURE_CUBE_MAP, 1, format, desc.width, desc.height);
                setParameters(GL_TEXTURE_CUBE_MAP, desc, false);
            }
            glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
        } else {
            record.target = desc.layers > 0 ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D;
            glGenTextures(1, &record.name);
            glBindTexture(record.target, record.name);
            if (desc.layers > 0) {
                glTexStorage3D(GL_TEXTURE_2D_ARRAY, 1, format, desc.width, desc.height, desc.layers);
            } else {
                glTexStorage2D(GL_TEXTURE_2D, levels, format, desc.width, desc.height);
            }
            if (desc.initialData && desc.layers == 0 && desc.format != TextureFormat::Depth32F) {
                const bool r8 = desc.format == TextureFormat::R8;
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, desc.width, desc.height, r8 ? GL_RED : GL_RGBA,
                                GL_UNSIGNED_BYTE, desc.initialData);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
            }
            if (mipmaps && desc.initialData) glGenerateMipmap(GL_TEXTURE_2D);
            setParameters(record.target, desc, mipmaps);
            glBindTexture(record.target, 0);
        }
        glActiveTexture(GL_TEXTURE0);
        return TextureHandle{textures_.add(record)};
    }

    void updateTexture(TextureHandle texture, int x, int y, int width, int height, const void* data) override {
        const TextureRecord& record = textures_.get(texture.value);
        const TextureDesc& desc = record.desc;
        if (record.target != GL_TEXTURE_2D || desc.format == TextureFormat::Depth32F) {
            fail(STRATA_ERROR_INTERNAL, "updateTexture: only 2D colour textures can be updated");
        }
        const bool r8 = desc.format == TextureFormat::R8;
        // Bound on the scratch unit, as in createTexture, so no unit a pass
        // uses changes.
        glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(scratchUnit_));
        glBindTexture(GL_TEXTURE_2D, record.name);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, x, y, width, height, r8 ? GL_RED : GL_RGBA, GL_UNSIGNED_BYTE, data);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        if (desc.mipmaps && desc.format == TextureFormat::RGBA8) glGenerateMipmap(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE0);
    }

    TargetHandle createTarget(const TargetDesc& desc) override {
        TargetRecord record;
        glGenFramebuffers(1, &record.framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, record.framebuffer);
        attach(GL_COLOR_ATTACHMENT0, desc.color);
        attach(GL_DEPTH_ATTACHMENT, desc.depth);
        const GLenum buffer = desc.color.texture ? GL_COLOR_ATTACHMENT0 : GL_NONE;
        glDrawBuffers(1, &buffer);
        glReadBuffer(buffer);
        const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            glDeleteFramebuffers(1, &record.framebuffer);
            fail(STRATA_ERROR_GPU, "render target %s is incomplete (0x%x)", desc.debugName ? desc.debugName : "?",
                 status);
        }
        return TargetHandle{targets_.add(record)};
    }

    PipelineHandle createPipeline(const PipelineDesc& desc) override {
        GlslEsOptions options;
        options.emulateDepthClamp = desc.depthClamp && !depthClamp_;
        options.depthBiasSlope = desc.depthBiasSlope;
        options.depthBiasConstant = desc.depthBiasConstant;
        const std::string fragment450 = desc.fragmentSource;
        const auto cubeAt = fragment450.find("uniform samplerCubeArrayShadow");
        if (cubeAt != std::string::npos) {
            // The unit the GL 4.5 shader names: "layout(binding = N) uniform samplerCubeArrayShadow".
            const auto open = fragment450.rfind("binding = ", cubeAt);
            const int unit = open == std::string::npos ? -1 : std::atoi(fragment450.c_str() + open + 10);
            options.cubeArrayUnits.push_back(unit);
            for (int i = 1; i < kMaxEmulatedCubes; ++i) options.cubeArrayUnits.push_back(kFirstExtraCubeUnit + i - 1);
            cubeArrayUnit_ = unit;
        }
        const GlslEsShader vertex = translateToGlslEs(desc.vertexSource, ShaderStage::Vertex, options);
        const GlslEsShader fragment = translateToGlslEs(fragment450, ShaderStage::Fragment, options);

        GLuint vs = compileShader(GL_VERTEX_SHADER, vertex.source, desc.name);
        GLuint fs = 0;
        try {
            fs = compileShader(GL_FRAGMENT_SHADER, fragment.source, desc.name);
        } catch (...) {
            glDeleteShader(vs);
            throw;
        }
        PipelineRecord record;
        record.program = glCreateProgram();
        glAttachShader(record.program, vs);
        glAttachShader(record.program, fs);
        glLinkProgram(record.program);
        glDetachShader(record.program, vs);
        glDetachShader(record.program, fs);
        glDeleteShader(vs);
        glDeleteShader(fs);
        GLint linked = GL_FALSE;
        glGetProgramiv(record.program, GL_LINK_STATUS, &linked);
        if (!linked) {
            char log[4096] = {};
            glGetProgramInfoLog(record.program, sizeof log, nullptr, log);
            glDeleteProgram(record.program);
            fail(STRATA_ERROR_GPU, "%s: program failed to link: %s", desc.name, log);
        }

        // The bindings GLSL ES cannot state.
        for (const GlslEsShader* shader : {&vertex, &fragment}) {
            for (const GlslEsBinding& b : shader->blocks) {
                const GLuint index = glGetUniformBlockIndex(record.program, b.name.c_str());
                if (index != GL_INVALID_INDEX) glUniformBlockBinding(record.program, index, static_cast<GLuint>(b.unit));
            }
        }
        GLint previousProgram = 0;
        glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
        glUseProgram(record.program);
        for (const GlslEsShader* shader : {&vertex, &fragment}) {
            for (const GlslEsBinding& b : shader->samplers) {
                const GLint location = glGetUniformLocation(record.program, b.name.c_str());
                if (location >= 0) glUniform1i(location, b.unit);
            }
        }
        glUseProgram(static_cast<GLuint>(previousProgram));

        glGenVertexArrays(1, &record.vertexArray);
        glBindVertexArray(record.vertexArray);
        for (const VertexAttribute& a : desc.attributes) {
            if (a.binding >= kMaxBindings) fail(STRATA_ERROR_INTERNAL, "vertex binding %u is out of range", a.binding);
            glEnableVertexAttribArray(a.location);
            bool perInstance = false;
            for (const VertexBufferLayout& layout : desc.buffers) {
                if (layout.binding == a.binding) perInstance = layout.perInstance;
            }
            glVertexAttribDivisor(a.location, perInstance ? 1 : 0);
        }
        glBindVertexArray(0);
        record.desc = desc;
        record.desc.vertexSource = nullptr;
        record.desc.fragmentSource = nullptr;
        record.buffers = desc.buffers;
        record.attributes = desc.attributes;
        return PipelineHandle{pipelines_.add(record)};
    }

    void destroy(BufferHandle buffer) override {
        if (!buffer) return;
        glDeleteBuffers(1, &buffers_.get(buffer.value).name);
        buffers_.remove(buffer.value);
    }
    void destroy(TextureHandle texture) override {
        if (!texture) return;
        deleteTexture(textures_.get(texture.value));
        textures_.remove(texture.value);
    }
    void destroy(TargetHandle target) override {
        if (!target) return;
        glDeleteFramebuffers(1, &targets_.get(target.value).framebuffer);
        targets_.remove(target.value);
    }
    void destroy(PipelineHandle pipeline) override {
        if (!pipeline) return;
        PipelineRecord& record = pipelines_.get(pipeline.value);
        if (current_ == &record) current_ = nullptr;
        glDeleteProgram(record.program);
        glDeleteVertexArrays(1, &record.vertexArray);
        pipelines_.remove(pipeline.value);
    }

    void beginPass(const PassDesc& desc) override {
        const GLuint framebuffer = desc.target ? targets_.get(desc.target.value).framebuffer : 0;
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glViewport(desc.x, desc.y, desc.width, desc.height);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_BLEND);
        GLbitfield mask = 0;
        if (desc.clearColor) {
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glClearColor(desc.color[0], desc.color[1], desc.color[2], desc.color[3]);
            mask |= GL_COLOR_BUFFER_BIT;
        }
        if (desc.clearDepth) {
            glDepthMask(GL_TRUE);
            glClearDepthf(desc.depth);
            mask |= GL_DEPTH_BUFFER_BIT;
        }
        if (mask) glClear(mask);
    }

    void bindPipeline(PipelineHandle pipeline) override {
        PipelineRecord& record = pipelines_.get(pipeline.value);
        current_ = &record;
        const PipelineDesc& d = record.desc;
        glUseProgram(record.program);
        glBindVertexArray(record.vertexArray);
        if (d.depthTest == DepthTest::Off) {
            glDisable(GL_DEPTH_TEST);
        } else {
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(d.depthTest == DepthTest::Less ? GL_LESS : GL_LEQUAL);
        }
        glDepthMask(d.depthWrite ? GL_TRUE : GL_FALSE);
        const GLboolean c = d.colorWrite ? GL_TRUE : GL_FALSE;
        glColorMask(c, c, c, c);
        if (d.cull == CullMode::None) {
            glDisable(GL_CULL_FACE);
        } else {
            glEnable(GL_CULL_FACE);
            glCullFace(d.cull == CullMode::Back ? GL_BACK : GL_FRONT);
            glFrontFace(GL_CCW);
        }
        if (d.depthBiasConstant != 0.0f || d.depthBiasSlope != 0.0f) {
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(d.depthBiasSlope, d.depthBiasConstant);
        } else {
            glDisable(GL_POLYGON_OFFSET_FILL);
        }
        if (depthClamp_) {
            if (d.depthClamp) {
                glEnable(GL_DEPTH_CLAMP_EXT);
            } else {
                glDisable(GL_DEPTH_CLAMP_EXT);
            }
        }
        if (d.blend == BlendMode::None) {
            glDisable(GL_BLEND);
        } else {
            glEnable(GL_BLEND);
            glBlendEquation(GL_FUNC_ADD);
            // Destination alpha is kept as it was: screenshots force it opaque.
            glBlendFuncSeparate(GL_SRC_ALPHA, d.blend == BlendMode::Alpha ? GL_ONE_MINUS_SRC_ALPHA : GL_ONE, GL_ZERO,
                                GL_ONE);
        }
    }

    void bindVertexBuffer(uint32_t binding, BufferHandle buffer, size_t offset) override {
        if (!current_) fail(STRATA_ERROR_INTERNAL, "vertex buffer bound with no pipeline");
        if (binding >= kMaxBindings) fail(STRATA_ERROR_INTERNAL, "vertex binding %u is out of range", binding);
        current_->bindings[binding] = VertexBinding{buffers_.get(buffer.value).name, offset};
    }

    void bindIndexBuffer(BufferHandle buffer) override {
        if (!current_) fail(STRATA_ERROR_INTERNAL, "index buffer bound with no pipeline");
        if (!buffers_.get(buffer.value).index) fail(STRATA_ERROR_INTERNAL, "bound a buffer as indices that was not made for them");
        // The pipeline's vertex array is bound, so this is recorded in it.
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffers_.get(buffer.value).name);
    }

    void bindUniformBuffer(uint32_t slot, BufferHandle buffer, size_t offset, size_t bytes) override {
        glBindBufferRange(GL_UNIFORM_BUFFER, slot, buffers_.get(buffer.value).name, static_cast<GLintptr>(offset),
                          static_cast<GLsizeiptr>(bytes));
    }

    void bindTexture(uint32_t unit, TextureHandle texture) override {
        if (!texture) {
            glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(GL_TEXTURE_2D, 0);
            glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
            glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
            glActiveTexture(GL_TEXTURE0);
            return;
        }
        const TextureRecord& record = textures_.get(texture.value);
        if (!record.cubes.empty()) {
            if (static_cast<int>(unit) != cubeArrayUnit_) {
                fail(STRATA_ERROR_INTERNAL, "cube map array bound to unit %u, but its shader expects unit %d", unit,
                     cubeArrayUnit_);
            }
            // Every cube unit gets a cube, so no sampler reads an empty unit.
            for (int i = 0; i < kMaxEmulatedCubes; ++i) {
                const int cubeUnit = i == 0 ? static_cast<int>(unit) : kFirstExtraCubeUnit + i - 1;
                const size_t cube = static_cast<size_t>(i) < record.cubes.size() ? static_cast<size_t>(i) : 0;
                glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(cubeUnit));
                glBindTexture(GL_TEXTURE_CUBE_MAP, record.cubes[cube]);
            }
        } else {
            glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(record.target, record.name);
        }
        glActiveTexture(GL_TEXTURE0);
    }

    void drawIndexed(const DrawIndexed& draw) override {
        if (draw.indexCount == 0 || draw.instanceCount == 0) return;
        applyAttributes(draw.baseVertex, draw.baseInstance);
        glDrawElementsInstanced(GL_TRIANGLES, static_cast<GLsizei>(draw.indexCount), GL_UNSIGNED_INT,
                                reinterpret_cast<const void*>(static_cast<uintptr_t>(draw.firstIndex) * sizeof(uint32_t)),
                                static_cast<GLsizei>(draw.instanceCount));
    }

    void drawArrays(const DrawArrays& draw) override {
        if (draw.vertexCount == 0 || draw.instanceCount == 0) return;
        applyAttributes(0, draw.baseInstance);
        const GLenum mode = current_ && current_->desc.topology == Topology::Lines ? GL_LINES : GL_TRIANGLES;
        glDrawArraysInstanced(mode, static_cast<GLint>(draw.firstVertex), static_cast<GLsizei>(draw.vertexCount),
                              static_cast<GLsizei>(draw.instanceCount));
    }

    void drawFullscreenTriangle() override {
        glBindVertexArray(emptyVertexArray_);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        if (current_) glBindVertexArray(current_->vertexArray);
    }

    void endPass() override {
        current_ = nullptr;
        glBindVertexArray(0);
    }

    void readPixels(TargetHandle target, int x, int y, int width, int height, void* rgba) override {
        const GLuint framebuffer = target ? targets_.get(target.value).framebuffer : 0;
        glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
        if (framebuffer == 0) glReadBuffer(GL_BACK);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
    }

    void finish() override { glFinish(); }

    void captureHostState() override {
        HostState& s = host_;
        glGetIntegerv(GL_CURRENT_PROGRAM, &s.program);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &s.vertexArray);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &s.drawFramebuffer);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &s.readFramebuffer);
        glGetIntegerv(GL_VIEWPORT, s.viewport);
        s.depthTest = glIsEnabled(GL_DEPTH_TEST);
        s.cullFace = glIsEnabled(GL_CULL_FACE);
        s.blend = glIsEnabled(GL_BLEND);
        s.scissor = glIsEnabled(GL_SCISSOR_TEST);
        s.polygonOffsetFill = glIsEnabled(GL_POLYGON_OFFSET_FILL);
        s.depthClamp = depthClamp_ ? glIsEnabled(GL_DEPTH_CLAMP_EXT) : GL_FALSE;
        glGetBooleanv(GL_DEPTH_WRITEMASK, &s.depthMask);
        glGetBooleanv(GL_COLOR_WRITEMASK, s.colorMask);
        glGetIntegerv(GL_DEPTH_FUNC, &s.depthFunc);
        glGetIntegerv(GL_CULL_FACE_MODE, &s.cullFaceMode);
        glGetIntegerv(GL_FRONT_FACE, &s.frontFace);
        glGetFloatv(GL_POLYGON_OFFSET_FACTOR, &s.polygonOffsetFactor);
        glGetFloatv(GL_POLYGON_OFFSET_UNITS, &s.polygonOffsetUnits);
        glGetIntegerv(GL_BLEND_SRC_RGB, &s.blendSrcRgb);
        glGetIntegerv(GL_BLEND_DST_RGB, &s.blendDstRgb);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &s.blendSrcAlpha);
        glGetIntegerv(GL_BLEND_DST_ALPHA, &s.blendDstAlpha);
        glGetIntegerv(GL_BLEND_EQUATION_RGB, &s.blendEquationRgb);
        glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &s.blendEquationAlpha);
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &s.unpackAlignment);
        glGetIntegerv(GL_PACK_ALIGNMENT, &s.packAlignment);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &s.activeTexture);
        for (int unit = 0; unit < kHostTextureUnits; ++unit) {
            glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &s.texture2D[unit]);
            glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &s.texture2DArray[unit]);
            glGetIntegerv(GL_TEXTURE_BINDING_CUBE_MAP, &s.textureCube[unit]);
        }
        for (int slot = 0; slot < kHostUniformSlots; ++slot) {
            glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING, static_cast<GLuint>(slot), &s.uniformBuffers[slot]);
        }
        glActiveTexture(static_cast<GLenum>(s.activeTexture));
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &s.arrayBuffer);
    }

    void restoreHostState() override {
        const HostState& s = host_;
        glUseProgram(static_cast<GLuint>(s.program));
        glBindVertexArray(static_cast<GLuint>(s.vertexArray));
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(s.drawFramebuffer));
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(s.readFramebuffer));
        glViewport(s.viewport[0], s.viewport[1], s.viewport[2], s.viewport[3]);
        setEnabled(GL_DEPTH_TEST, s.depthTest);
        setEnabled(GL_CULL_FACE, s.cullFace);
        setEnabled(GL_BLEND, s.blend);
        setEnabled(GL_SCISSOR_TEST, s.scissor);
        setEnabled(GL_POLYGON_OFFSET_FILL, s.polygonOffsetFill);
        if (depthClamp_) setEnabled(GL_DEPTH_CLAMP_EXT, s.depthClamp);
        glDepthMask(s.depthMask);
        glColorMask(s.colorMask[0], s.colorMask[1], s.colorMask[2], s.colorMask[3]);
        glDepthFunc(static_cast<GLenum>(s.depthFunc));
        glCullFace(static_cast<GLenum>(s.cullFaceMode));
        glFrontFace(static_cast<GLenum>(s.frontFace));
        glPolygonOffset(s.polygonOffsetFactor, s.polygonOffsetUnits);
        glBlendFuncSeparate(static_cast<GLenum>(s.blendSrcRgb), static_cast<GLenum>(s.blendDstRgb),
                            static_cast<GLenum>(s.blendSrcAlpha), static_cast<GLenum>(s.blendDstAlpha));
        glBlendEquationSeparate(static_cast<GLenum>(s.blendEquationRgb), static_cast<GLenum>(s.blendEquationAlpha));
        glPixelStorei(GL_UNPACK_ALIGNMENT, s.unpackAlignment);
        glPixelStorei(GL_PACK_ALIGNMENT, s.packAlignment);
        for (int unit = 0; unit < kHostTextureUnits; ++unit) {
            glActiveTexture(GL_TEXTURE0 + static_cast<GLenum>(unit));
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(s.texture2D[unit]));
            glBindTexture(GL_TEXTURE_2D_ARRAY, static_cast<GLuint>(s.texture2DArray[unit]));
            glBindTexture(GL_TEXTURE_CUBE_MAP, static_cast<GLuint>(s.textureCube[unit]));
        }
        for (int slot = 0; slot < kHostUniformSlots; ++slot) {
            glBindBufferBase(GL_UNIFORM_BUFFER, static_cast<GLuint>(slot), static_cast<GLuint>(s.uniformBuffers[slot]));
        }
        glActiveTexture(static_cast<GLenum>(s.activeTexture));
        glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(s.arrayBuffer));
    }

private:
    static void setEnabled(GLenum cap, GLboolean on) {
        if (on) {
            glEnable(cap);
        } else {
            glDisable(cap);
        }
    }

    // Replaces (whole) or edits the buffer's storage.
    void upload(const BufferRecord& record, size_t offset, size_t bytes, const void* data, bool whole) {
        GLenum target = GL_COPY_WRITE_BUFFER;
        if (record.index) {
            target = GL_ELEMENT_ARRAY_BUFFER;
            glBindVertexArray(0);  // the element binding belongs to the bound vertex array
        }
        glBindBuffer(target, record.name);
        if (whole) {
            glBufferData(target, static_cast<GLsizeiptr>(bytes), data, record.dynamic ? GL_STREAM_DRAW : GL_STATIC_DRAW);
        } else {
            glBufferSubData(target, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(bytes), data);
        }
        glBindBuffer(target, 0);
        if (record.index && current_) glBindVertexArray(current_->vertexArray);
    }

    static void deleteTexture(TextureRecord& t) {
        if (t.name) glDeleteTextures(1, &t.name);
        if (!t.cubes.empty()) glDeleteTextures(static_cast<GLsizei>(t.cubes.size()), t.cubes.data());
    }

    static void setParameters(GLenum target, const TextureDesc& desc, bool mipmaps) {
        const GLint filter = desc.nearest ? GL_NEAREST : GL_LINEAR;
        glTexParameteri(target, GL_TEXTURE_MIN_FILTER,
                        mipmaps ? (desc.nearest ? GL_NEAREST_MIPMAP_LINEAR : GL_LINEAR_MIPMAP_LINEAR) : filter);
        glTexParameteri(target, GL_TEXTURE_MAG_FILTER, filter);
        const bool cube = target == GL_TEXTURE_CUBE_MAP;
        glTexParameteri(target, GL_TEXTURE_WRAP_S, desc.repeatU && !cube ? GL_REPEAT : GL_CLAMP_TO_EDGE);
        glTexParameteri(target, GL_TEXTURE_WRAP_T, desc.repeatV && !cube ? GL_REPEAT : GL_CLAMP_TO_EDGE);
        if (cube) glTexParameteri(target, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        if (desc.depthCompare) {
            glTexParameteri(target, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
            glTexParameteri(target, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
        }
    }

    // Expects the framebuffer bound to GL_FRAMEBUFFER.
    void attach(GLenum point, const Attachment& attachment) {
        if (!attachment.texture) return;
        const TextureRecord& record = textures_.get(attachment.texture.value);
        if (!record.cubes.empty()) {
            if (attachment.layer < 0) fail(STRATA_ERROR_INTERNAL, "a whole cube map array cannot be attached");
            const size_t cube = static_cast<size_t>(attachment.layer / 6);
            const GLenum face = GL_TEXTURE_CUBE_MAP_POSITIVE_X + static_cast<GLenum>(attachment.layer % 6);
            glFramebufferTexture2D(GL_FRAMEBUFFER, point, face, record.cubes.at(cube), 0);
        } else if (attachment.layer >= 0) {
            glFramebufferTextureLayer(GL_FRAMEBUFFER, point, record.name, 0, attachment.layer);
        } else {
            glFramebufferTexture2D(GL_FRAMEBUFFER, point, GL_TEXTURE_2D, record.name, 0);
        }
    }

    // GLES 3.0 has neither base vertex nor base instance, so both are folded
    // into the attribute pointers of the bound pipeline.
    void applyAttributes(int32_t baseVertex, uint32_t baseInstance) {
        if (!current_) fail(STRATA_ERROR_INTERNAL, "draw with no pipeline");
        for (const VertexAttribute& a : current_->attributes) {
            const VertexBinding& vb = current_->bindings[a.binding];
            if (!vb.buffer) fail(STRATA_ERROR_INTERNAL, "%s: vertex binding %u has no buffer", current_->desc.name, a.binding);
            GLsizei stride = 0;
            bool perInstance = false;
            for (const VertexBufferLayout& layout : current_->buffers) {
                if (layout.binding == a.binding) {
                    stride = static_cast<GLsizei>(layout.stride);
                    perInstance = layout.perInstance;
                }
            }
            const int64_t first = perInstance ? static_cast<int64_t>(baseInstance) : static_cast<int64_t>(baseVertex);
            const uintptr_t offset = static_cast<uintptr_t>(static_cast<int64_t>(vb.offset) + a.offset + first * stride);
            glBindBuffer(GL_ARRAY_BUFFER, vb.buffer);
            if (a.type == AttribType::Float) {
                glVertexAttribPointer(a.location, a.components, GL_FLOAT, GL_FALSE, stride,
                                      reinterpret_cast<const void*>(offset));
            } else {
                glVertexAttribIPointer(a.location, a.components, GL_INT, stride, reinterpret_cast<const void*>(offset));
            }
        }
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }

    std::string description_;
    size_t uniformAlignment_ = 256;
    GLuint emptyVertexArray_ = 0;
    bool depthClamp_ = false;
    int scratchUnit_ = kMinFragmentTextureUnits - 1;
    int cubeArrayUnit_ = -1;
    Table<BufferRecord> buffers_;
    Table<TextureRecord> textures_;
    Table<TargetRecord> targets_;
    Table<PipelineRecord> pipelines_;
    PipelineRecord* current_ = nullptr;
    HostState host_;
};

}  // namespace

std::unique_ptr<GpuDevice> createGles3Device(bool debugOutput) { return std::make_unique<Gles3Device>(debugOutput); }

}  // namespace strata::rhi
