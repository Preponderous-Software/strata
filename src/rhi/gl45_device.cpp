// OpenGL 4.5 core backend for GpuDevice, using direct state access.
#include <glad/gl.h>

#include <cstring>
#include <string>
#include <vector>

#include "core/error.hpp"
#include "core/log.hpp"
#include "rhi/gpu_device.hpp"

namespace strata::rhi {
namespace {

struct BufferRecord {
    GLuint name = 0;
    size_t size = 0;
    bool dynamic = false;
};

struct TextureRecord {
    GLuint name = 0;
    TextureDesc desc;
};

struct TargetRecord {
    GLuint framebuffer = 0;
};

struct PipelineRecord {
    GLuint program = 0;
    GLuint vertexArray = 0;
    PipelineDesc desc;  // shader source pointers are not kept valid; only state is read
    std::vector<VertexBufferLayout> buffers;
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

GLuint compileShader(GLenum type, const char* source, const char* pipelineName) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[4096];
        glGetShaderInfoLog(shader, sizeof log, nullptr, log);
        glDeleteShader(shader);
        fail(STRATA_ERROR_GPU, "%s: %s shader failed to compile: %s", pipelineName,
             type == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
    }
    return shader;
}

void GLAD_API_PTR debugCallback(GLenum, GLenum type, GLuint, GLenum severity, GLsizei, const GLchar* message,
                                const void*) {
    if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) return;
    const LogLevel level = type == GL_DEBUG_TYPE_ERROR ? LogLevel::Error : LogLevel::Warning;
    logf(level, "GL: %s", message);
}

struct HostState {
    GLint program = 0, vertexArray = 0, drawFramebuffer = 0, readFramebuffer = 0;
    GLint viewport[4] = {0, 0, 0, 0};
    GLboolean depthTest = GL_FALSE, cullFace = GL_FALSE, blend = GL_FALSE, scissor = GL_FALSE;
    GLboolean polygonOffsetFill = GL_FALSE, depthClamp = GL_FALSE, depthMask = GL_TRUE;
    GLboolean colorMask[4] = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
    GLint depthFunc = GL_LESS, cullFaceMode = GL_BACK, frontFace = GL_CCW;
    GLfloat polygonOffsetFactor = 0, polygonOffsetUnits = 0;
    GLint activeTexture = GL_TEXTURE0;
    GLint texture2D[2] = {0, 0}, texture2DArray[2] = {0, 0};
    GLint uniformBuffers[3] = {0, 0, 0};
    GLint elementArrayBuffer = 0, arrayBuffer = 0;
};

class Gl45Device final : public GpuDevice {
public:
    explicit Gl45Device(bool debugOutput) {
        const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
        const char* version = reinterpret_cast<const char*>(glGetString(GL_VERSION));
        description_ = std::string(renderer ? renderer : "?") + " | " + (version ? version : "?");
        GLint alignment = 256;
        glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &alignment);
        uniformAlignment_ = static_cast<size_t>(alignment > 0 ? alignment : 256);
        // Fullscreen passes draw with no vertex buffers, but core profile
        // still needs some VAO bound.
        glCreateVertexArrays(1, &emptyVertexArray_);
        if (debugOutput) {
            glEnable(GL_DEBUG_OUTPUT);
            glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
            glDebugMessageCallback(debugCallback, nullptr);
            debugGroups_ = true;
        }
    }

    ~Gl45Device() override {
        pipelines_.forEach([](PipelineRecord& p) {
            glDeleteProgram(p.program);
            glDeleteVertexArrays(1, &p.vertexArray);
        });
        targets_.forEach([](TargetRecord& t) { glDeleteFramebuffers(1, &t.framebuffer); });
        textures_.forEach([](TextureRecord& t) { glDeleteTextures(1, &t.name); });
        buffers_.forEach([](BufferRecord& b) { glDeleteBuffers(1, &b.name); });
        glDeleteVertexArrays(1, &emptyVertexArray_);
    }

    std::string description() const override { return description_; }
    size_t uniformBufferAlignment() const override { return uniformAlignment_; }

    BufferHandle createBuffer(size_t bytes, const void* initial, bool dynamic, const char* debugName) override {
        BufferRecord record;
        glCreateBuffers(1, &record.name);
        record.size = bytes;
        record.dynamic = dynamic;
        glNamedBufferData(record.name, static_cast<GLsizeiptr>(bytes), initial, dynamic ? GL_STREAM_DRAW : GL_STATIC_DRAW);
        label(GL_BUFFER, record.name, debugName);
        return BufferHandle{buffers_.add(record)};
    }

    void resizeBuffer(BufferHandle buffer, size_t bytes) override {
        BufferRecord& record = buffers_.get(buffer.value);
        glNamedBufferData(record.name, static_cast<GLsizeiptr>(bytes), nullptr,
                          record.dynamic ? GL_STREAM_DRAW : GL_STATIC_DRAW);
        record.size = bytes;
    }

    size_t bufferSize(BufferHandle buffer) const override { return buffers_.get(buffer.value).size; }

    void updateBuffer(BufferHandle buffer, size_t offset, size_t bytes, const void* data) override {
        if (bytes == 0) return;
        BufferRecord& record = buffers_.get(buffer.value);
        if (offset + bytes > record.size) {
            fail(STRATA_ERROR_INTERNAL, "buffer update of %zu bytes at %zu overruns %zu", bytes, offset, record.size);
        }
        glNamedBufferSubData(record.name, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(bytes), data);
    }

    TextureHandle createTexture(const TextureDesc& desc) override {
        TextureRecord record;
        record.desc = desc;
        const GLenum target = desc.layers > 0 ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D;
        const GLenum format = desc.format == TextureFormat::RGBA8 ? GL_RGBA8 : GL_DEPTH_COMPONENT32F;
        glCreateTextures(target, 1, &record.name);
        if (desc.layers > 0) {
            glTextureStorage3D(record.name, 1, format, desc.width, desc.height, desc.layers);
        } else {
            glTextureStorage2D(record.name, 1, format, desc.width, desc.height);
        }
        glTextureParameteri(record.name, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTextureParameteri(record.name, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(record.name, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTextureParameteri(record.name, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        if (desc.depthCompare) {
            glTextureParameteri(record.name, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
            glTextureParameteri(record.name, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
        }
        label(GL_TEXTURE, record.name, desc.debugName);
        return TextureHandle{textures_.add(record)};
    }

    TargetHandle createTarget(const TargetDesc& desc) override {
        TargetRecord record;
        glCreateFramebuffers(1, &record.framebuffer);
        attach(record.framebuffer, GL_COLOR_ATTACHMENT0, desc.color);
        attach(record.framebuffer, GL_DEPTH_ATTACHMENT, desc.depth);
        if (desc.color.texture) {
            glNamedFramebufferDrawBuffer(record.framebuffer, GL_COLOR_ATTACHMENT0);
            glNamedFramebufferReadBuffer(record.framebuffer, GL_COLOR_ATTACHMENT0);
        } else {
            glNamedFramebufferDrawBuffer(record.framebuffer, GL_NONE);
            glNamedFramebufferReadBuffer(record.framebuffer, GL_NONE);
        }
        const GLenum status = glCheckNamedFramebufferStatus(record.framebuffer, GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            glDeleteFramebuffers(1, &record.framebuffer);
            fail(STRATA_ERROR_GPU, "render target %s is incomplete (0x%x)", desc.debugName ? desc.debugName : "?",
                 status);
        }
        label(GL_FRAMEBUFFER, record.framebuffer, desc.debugName);
        return TargetHandle{targets_.add(record)};
    }

    PipelineHandle createPipeline(const PipelineDesc& desc) override {
        GLuint vs = compileShader(GL_VERTEX_SHADER, desc.vertexSource, desc.name);
        GLuint fs = 0;
        try {
            fs = compileShader(GL_FRAGMENT_SHADER, desc.fragmentSource, desc.name);
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
            char log[4096];
            glGetProgramInfoLog(record.program, sizeof log, nullptr, log);
            glDeleteProgram(record.program);
            fail(STRATA_ERROR_GPU, "%s: program failed to link: %s", desc.name, log);
        }
        label(GL_PROGRAM, record.program, desc.name);

        glCreateVertexArrays(1, &record.vertexArray);
        for (const VertexBufferLayout& layout : desc.buffers) {
            glVertexArrayBindingDivisor(record.vertexArray, layout.binding, layout.perInstance ? 1 : 0);
        }
        for (const VertexAttribute& a : desc.attributes) {
            glEnableVertexArrayAttrib(record.vertexArray, a.location);
            if (a.type == AttribType::Float) {
                glVertexArrayAttribFormat(record.vertexArray, a.location, a.components, GL_FLOAT, GL_FALSE, a.offset);
            } else {
                glVertexArrayAttribIFormat(record.vertexArray, a.location, a.components, GL_INT, a.offset);
            }
            glVertexArrayAttribBinding(record.vertexArray, a.location, a.binding);
        }
        record.desc = desc;
        record.desc.vertexSource = nullptr;
        record.desc.fragmentSource = nullptr;
        record.buffers = desc.buffers;
        return PipelineHandle{pipelines_.add(record)};
    }

    void destroy(BufferHandle buffer) override {
        if (!buffer) return;
        glDeleteBuffers(1, &buffers_.get(buffer.value).name);
        buffers_.remove(buffer.value);
    }
    void destroy(TextureHandle texture) override {
        if (!texture) return;
        glDeleteTextures(1, &textures_.get(texture.value).name);
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
            glClearDepth(desc.depth);
            mask |= GL_DEPTH_BUFFER_BIT;
        }
        if (mask) glClear(mask);
        if (debugGroups_) glPushDebugGroup(GL_DEBUG_SOURCE_APPLICATION, 0, -1, desc.name);
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
        if (d.depthClamp) {
            glEnable(GL_DEPTH_CLAMP);
        } else {
            glDisable(GL_DEPTH_CLAMP);
        }
    }

    void bindVertexBuffer(uint32_t binding, BufferHandle buffer, size_t offset) override {
        if (!current_) fail(STRATA_ERROR_INTERNAL, "vertex buffer bound with no pipeline");
        GLsizei stride = 0;
        for (const VertexBufferLayout& layout : current_->buffers) {
            if (layout.binding == binding) stride = static_cast<GLsizei>(layout.stride);
        }
        glVertexArrayVertexBuffer(current_->vertexArray, binding, buffers_.get(buffer.value).name,
                                  static_cast<GLintptr>(offset), stride);
    }

    void bindIndexBuffer(BufferHandle buffer) override {
        if (!current_) fail(STRATA_ERROR_INTERNAL, "index buffer bound with no pipeline");
        glVertexArrayElementBuffer(current_->vertexArray, buffers_.get(buffer.value).name);
    }

    void bindUniformBuffer(uint32_t slot, BufferHandle buffer, size_t offset, size_t bytes) override {
        glBindBufferRange(GL_UNIFORM_BUFFER, slot, buffers_.get(buffer.value).name, static_cast<GLintptr>(offset),
                          static_cast<GLsizeiptr>(bytes));
    }

    void bindTexture(uint32_t unit, TextureHandle texture) override {
        glBindTextureUnit(unit, texture ? textures_.get(texture.value).name : 0);
    }

    void drawIndexed(const DrawIndexed& draw) override {
        if (draw.indexCount == 0 || draw.instanceCount == 0) return;
        glDrawElementsInstancedBaseVertexBaseInstance(
            GL_TRIANGLES, static_cast<GLsizei>(draw.indexCount), GL_UNSIGNED_INT,
            reinterpret_cast<const void*>(static_cast<uintptr_t>(draw.firstIndex) * sizeof(uint32_t)),
            static_cast<GLsizei>(draw.instanceCount), draw.baseVertex, draw.baseInstance);
    }

    void drawFullscreenTriangle() override {
        glBindVertexArray(emptyVertexArray_);
        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    void endPass() override {
        if (debugGroups_) glPopDebugGroup();
        current_ = nullptr;
        glBindVertexArray(0);
    }

    void readPixels(TargetHandle target, int x, int y, int width, int height, void* rgba) override {
        const GLuint framebuffer = target ? targets_.get(target.value).framebuffer : 0;
        glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
        if (framebuffer == 0) glReadBuffer(GL_BACK);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
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
        s.depthClamp = glIsEnabled(GL_DEPTH_CLAMP);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &s.depthMask);
        glGetBooleanv(GL_COLOR_WRITEMASK, s.colorMask);
        glGetIntegerv(GL_DEPTH_FUNC, &s.depthFunc);
        glGetIntegerv(GL_CULL_FACE_MODE, &s.cullFaceMode);
        glGetIntegerv(GL_FRONT_FACE, &s.frontFace);
        glGetFloatv(GL_POLYGON_OFFSET_FACTOR, &s.polygonOffsetFactor);
        glGetFloatv(GL_POLYGON_OFFSET_UNITS, &s.polygonOffsetUnits);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &s.activeTexture);
        for (int unit = 0; unit < 2; ++unit) {
            glActiveTexture(GL_TEXTURE0 + unit);
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &s.texture2D[unit]);
            glGetIntegerv(GL_TEXTURE_BINDING_2D_ARRAY, &s.texture2DArray[unit]);
        }
        for (int slot = 0; slot < 3; ++slot) {
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
        setEnabled(GL_DEPTH_CLAMP, s.depthClamp);
        glDepthMask(s.depthMask);
        glColorMask(s.colorMask[0], s.colorMask[1], s.colorMask[2], s.colorMask[3]);
        glDepthFunc(static_cast<GLenum>(s.depthFunc));
        glCullFace(static_cast<GLenum>(s.cullFaceMode));
        glFrontFace(static_cast<GLenum>(s.frontFace));
        glPolygonOffset(s.polygonOffsetFactor, s.polygonOffsetUnits);
        for (int unit = 0; unit < 2; ++unit) {
            glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(s.texture2D[unit]));
            glBindTexture(GL_TEXTURE_2D_ARRAY, static_cast<GLuint>(s.texture2DArray[unit]));
        }
        for (int slot = 0; slot < 3; ++slot) {
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

    void attach(GLuint framebuffer, GLenum point, const Attachment& attachment) {
        if (!attachment.texture) return;
        const GLuint texture = textures_.get(attachment.texture.value).name;
        if (attachment.layer >= 0) {
            glNamedFramebufferTextureLayer(framebuffer, point, texture, 0, attachment.layer);
        } else {
            glNamedFramebufferTexture(framebuffer, point, texture, 0);
        }
    }

    static void label(GLenum identifier, GLuint name, const char* text) {
        if (text) glObjectLabel(identifier, name, -1, text);
    }

    std::string description_;
    size_t uniformAlignment_ = 256;
    GLuint emptyVertexArray_ = 0;
    bool debugGroups_ = false;
    Table<BufferRecord> buffers_;
    Table<TextureRecord> textures_;
    Table<TargetRecord> targets_;
    Table<PipelineRecord> pipelines_;
    PipelineRecord* current_ = nullptr;
    HostState host_;
};

}  // namespace

std::unique_ptr<GpuDevice> createGl45Device(bool debugOutput) { return std::make_unique<Gl45Device>(debugOutput); }

}  // namespace strata::rhi
