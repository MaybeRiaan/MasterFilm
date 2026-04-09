// src/platform/GLSLDispatch.cpp
#ifdef MASTERFILM_ENABLE_OPENGL

#include "GLSLDispatch.h"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cstdio>

namespace MasterFilm {

    // ── Shared fullscreen quad vertex shader ──────────────────────────────────────

    static const char* kFullscreenVertSrc = R"GLSL(
#version 410 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
out vec2 vTexCoord;
void main() {
    vTexCoord = aUV;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)GLSL";

    // ── ShaderProgram helpers ─────────────────────────────────────────────────────

    GLint ShaderProgram::loc(const std::string& name)
    {
        auto it = uniforms.find(name);
        if (it != uniforms.end()) return it->second;
        GLint l = glGetUniformLocation(id, name.c_str());
        uniforms[name] = l;
        return l;
    }

    void ShaderProgram::destroy()
    {
        if (id) { glDeleteProgram(id); id = 0; }
    }

    // ── GLSLDispatch ──────────────────────────────────────────────────────────────

    GLSLDispatch::GLSLDispatch()
    {
        // All GL calls deferred to initShaders() — no GL context exists at construction time.
    }

    GLSLDispatch::~GLSLDispatch()
    {
        mToneColor.destroy();
        mHalationH.destroy();
        mHalationV.destroy();
        mGrain.destroy();
        mAcutance.destroy();
        if (mQuadVAO) glDeleteVertexArrays(1, &mQuadVAO);
        if (mQuadVBO) glDeleteBuffers(1, &mQuadVBO);
    }

    void GLSLDispatch::buildFullscreenQuad()
    {
        // Two triangles covering NDC clip space, with UV [0,1]
        float verts[] = {
            -1.0f, -1.0f,  0.0f, 0.0f,
             1.0f, -1.0f,  1.0f, 0.0f,
             1.0f,  1.0f,  1.0f, 1.0f,
            -1.0f, -1.0f,  0.0f, 0.0f,
             1.0f,  1.0f,  1.0f, 1.0f,
            -1.0f,  1.0f,  0.0f, 1.0f,
        };

        glGenVertexArrays(1, &mQuadVAO);
        glGenBuffers(1, &mQuadVBO);

        glBindVertexArray(mQuadVAO);
        glBindBuffer(GL_ARRAY_BUFFER, mQuadVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));

        glBindVertexArray(0);
    }

    std::string GLSLDispatch::loadFile(const std::string& path)
    {
        std::ifstream f(path);
        if (!f.is_open())
            throw std::runtime_error("GLSLDispatch: cannot open shader: " + path);
        std::ostringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

    GLuint GLSLDispatch::compileShader(GLenum type, const std::string& src)
    {
        GLuint shader = glCreateShader(type);
        const char* c = src.c_str();
        glShaderSource(shader, 1, &c, nullptr);
        glCompileShader(shader);

        GLint ok = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[2048];
            glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
            glDeleteShader(shader);
            throw std::runtime_error(std::string("Shader compile error:\n") + log);
        }
        return shader;
    }

    ShaderProgram GLSLDispatch::compileProgram(const std::string& vertSrc,
        const std::string& fragPath)
    {
        ShaderProgram prog;
        std::string fragSrc = loadFile(fragPath);

        GLuint vert = compileShader(GL_VERTEX_SHADER, vertSrc);
        GLuint frag = compileShader(GL_FRAGMENT_SHADER, fragSrc);

        prog.id = glCreateProgram();
        glAttachShader(prog.id, vert);
        glAttachShader(prog.id, frag);
        glLinkProgram(prog.id);

        glDeleteShader(vert);
        glDeleteShader(frag);

        GLint ok = 0;
        glGetProgramiv(prog.id, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[2048];
            glGetProgramInfoLog(prog.id, sizeof(log), nullptr, log);
            prog.destroy();
            throw std::runtime_error(std::string("Program link error:\n") + log);
        }
        return prog;
    }

    bool GLSLDispatch::initShaders(const std::string& resourceDir)
    {
#if defined(_WIN32)
        // Initialise GLEW here — a GL context is guaranteed to exist at this point
        // because Resolve calls render (which calls initShaders) only after setup.
        glewExperimental = GL_TRUE;
        GLenum glewErr = glewInit();
        if (glewErr != GLEW_OK) {
            std::fprintf(stderr, "[MasterFilm] GLEW init failed: %s\n",
                reinterpret_cast<const char*>(glewGetErrorString(glewErr)));
            return false;
        }
#endif

        buildFullscreenQuad();
        std::string sd = resourceDir + "/shaders/";
        std::string vs(kFullscreenVertSrc);

        try {
            mToneColor = compileProgram(vs, sd + "tone_color.glsl");
            mHalationH = compileProgram(vs, sd + "halation_h.glsl");
            mHalationV = compileProgram(vs, sd + "halation_v.glsl");
            mGrain = compileProgram(vs, sd + "grain.glsl");
            mAcutance = compileProgram(vs, sd + "acutance.glsl");
        }
        catch (const std::exception& e) {
            std::fprintf(stderr, "[MasterFilm] Shader init failed: %s\n", e.what());
            return false;
        }
        return true;
    }

    bool GLSLDispatch::renderPass(ShaderProgram& prog,
        GLuint srcTex, GLuint dstFBO,
        int width, int height,
        GLuint srcTex2, const char* tex2Name)
    {
        if (!prog.isValid()) return false;

        glBindFramebuffer(GL_FRAMEBUFFER, dstFBO);
        glViewport(0, 0, width, height);

        glUseProgram(prog.id);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, srcTex);
        glUniform1i(prog.loc("uSrc"), 0);

        if (srcTex2 != 0 && tex2Name != nullptr) {
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, srcTex2);
            glUniform1i(prog.loc(tex2Name), 1);
            glActiveTexture(GL_TEXTURE0);   // restore before draw
        }

        glBindVertexArray(mQuadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);

        glUseProgram(0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        return true;
    }

} // namespace MasterFilm

#endif // MASTERFILM_ENABLE_OPENGL