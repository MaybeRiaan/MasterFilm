// src/platform/GLSLDispatch.h
// Manages OpenGL context acquisition, shader compilation, and fullscreen quad rendering.
// One instance per plugin instance — owns all GL objects for that instance.
#pragma once

#ifdef MASTERFILM_ENABLE_OPENGL

#include <string>
#include <unordered_map>

#if defined(__APPLE__)
#  include <OpenGL/gl3.h>
#elif defined(_WIN32)
   // GLEW must be included before any other GL header on Windows.
   // It includes gl.h internally AND loads all modern GL extensions (shaders,
   // VAOs, VBOs, FBOs etc.) which are not available in Windows' gl.h (OpenGL 1.1 only).
#  include <GL/glew.h>
#else
#  include <GL/gl.h>
#endif

namespace MasterFilm {

    // Compiled shader program handle with uniform location cache
    struct ShaderProgram {
        GLuint id = 0;
        std::unordered_map<std::string, GLint> uniforms;

        GLint loc(const std::string& name);
        bool  isValid() const { return id != 0; }
        void  destroy();
    };

    class GLSLDispatch {
    public:
        GLSLDispatch();
        ~GLSLDispatch();

        // Compile all five pass shaders from embedded source paths.
        // Called once during kOfxActionCreateInstance.
        bool initShaders(const std::string& resourceDir);

        // Per-render helpers — bind source texture, draw fullscreen quad, read result.
        // Optional srcTex2/tex2Name binds a second texture to GL_TEXTURE1 (for dual-input passes).
        bool renderPass(ShaderProgram& prog,
            GLuint srcTex, GLuint dstFBO,
            int width, int height,
            GLuint srcTex2 = 0, const char* tex2Name = nullptr);

        // Shader program accessors
        ShaderProgram& toneColorShader() { return mToneColor; }
        ShaderProgram& halationHShader() { return mHalationH; }
        ShaderProgram& halationVShader() { return mHalationV; }
        ShaderProgram& grainShader() { return mGrain; }
        ShaderProgram& acutanceShader() { return mAcutance; }

    private:
        ShaderProgram mToneColor;
        ShaderProgram mHalationH;
        ShaderProgram mHalationV;
        ShaderProgram mGrain;
        ShaderProgram mAcutance;

        GLuint mQuadVAO = 0;
        GLuint mQuadVBO = 0;

        // Compile vertex + fragment shader pair from file paths
        ShaderProgram compileProgram(const std::string& vertPath,
            const std::string& fragPath);
        // Shared fullscreen-quad vertex shader (same for all passes)
        std::string mFullscreenVertSrc;

        void buildFullscreenQuad();
        static std::string loadFile(const std::string& path);
        static GLuint compileShader(GLenum type, const std::string& src);
    };

} // namespace MasterFilm

#endif // MASTERFILM_ENABLE_OPENGL