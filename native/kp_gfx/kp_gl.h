/*
 * The GL functions kp_gfx calls (plan phase S, stage S4; ADR 0012): GLES 3.0 on Android, iOS and the web (WebGL2),
 * GL 4.1 core on macOS, GL 3.3 core on Windows and Linux. The ES platforms and macOS link their GL directly; Windows
 * and Linux load every function past GL 1.1 through SDL_GL_GetProcAddress into the table below (kp_gl_load), so no
 * third-party loader is needed.
 */
#ifndef KP_GL_H
#define KP_GL_H

#include <SDL3/SDL.h>

#if defined(SDL_PLATFORM_IOS)
#define KP_GLES 1
#define GLES_SILENCE_DEPRECATION 1
#include <OpenGLES/ES3/gl.h>
#elif defined(__EMSCRIPTEN__) || defined(SDL_PLATFORM_ANDROID)
#define KP_GLES 1
#include <GLES3/gl3.h>
#elif defined(SDL_PLATFORM_MACOS)
#define GL_SILENCE_DEPRECATION 1
#include <OpenGL/gl3.h>
#else
#define KP_GL_TABLE 1
#include <SDL3/SDL_opengl.h>
#endif

/* S3TC (EXT_texture_compression_s3tc, WEBGL_compressed_texture_s3tc): the HD art's BC1 and BC3 blocks (ADR 0013). */
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#endif
/* ETC2 and EAC (core in GLES 3.0 and GL 4.3, WEBGL_compressed_texture_etc): the phones' HD pages (stage R4f). */
#ifndef GL_TEXTURE_SWIZZLE_G
#define GL_TEXTURE_SWIZZLE_G 0x8E43
#endif
#ifndef GL_TEXTURE_SWIZZLE_A
#define GL_TEXTURE_SWIZZLE_A 0x8E45
#endif
#ifndef GL_COMPRESSED_R11_EAC
#define GL_COMPRESSED_R11_EAC 0x9270
#endif
#ifndef GL_COMPRESSED_RG11_EAC
#define GL_COMPRESSED_RG11_EAC 0x9272
#endif
#ifndef GL_COMPRESSED_RGB8_ETC2
#define GL_COMPRESSED_RGB8_ETC2 0x9274
#endif
#ifndef GL_COMPRESSED_RGBA8_ETC2_EAC
#define GL_COMPRESSED_RGBA8_ETC2_EAC 0x9278
#endif

/* ASTC (KHR_texture_compression_astc_ldr; core in GLES 3.2, nearly every phone of GLES 3.0): the phones' HD pages. */
#ifndef GL_COMPRESSED_RGBA_ASTC_4x4_KHR
#define GL_COMPRESSED_RGBA_ASTC_4x4_KHR 0x93B0
#endif
#ifndef GL_COMPRESSED_RGBA_ASTC_6x6_KHR
#define GL_COMPRESSED_RGBA_ASTC_6x6_KHR 0x93B4
#endif
#ifndef GL_COMPRESSED_RGBA_ASTC_8x8_KHR
#define GL_COMPRESSED_RGBA_ASTC_8x8_KHR 0x93B7
#endif

#ifdef KP_GL_TABLE
/* The functions past GL 1.1 (return type, name, parameters). */
#define KP_GL_FUNCTIONS(X) \
	X(void, glActiveTexture, (GLenum texture)) \
	X(void, glBlendFuncSeparate, (GLenum sfactorRGB, GLenum dfactorRGB, GLenum sfactorAlpha, GLenum dfactorAlpha)) \
	X(void, glTexImage3D, (GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, \
		GLsizei depth, GLint border, GLenum format, GLenum type, const void *pixels)) \
	X(void, glTexSubImage3D, (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, GLsizei width, \
		GLsizei height, GLsizei depth, GLenum format, GLenum type, const void *pixels)) \
	X(void, glCompressedTexImage3D, (GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, \
		GLsizei depth, GLint border, GLsizei imageSize, const void *data)) \
	X(void, glCompressedTexSubImage3D, (GLenum target, GLint level, GLint xoffset, GLint yoffset, GLint zoffset, \
		GLsizei width, GLsizei height, GLsizei depth, GLenum format, GLsizei imageSize, const void *data)) \
	X(GLuint, glCreateShader, (GLenum type)) \
	X(void, glShaderSource, (GLuint shader, GLsizei count, const GLchar *const *string, const GLint *length)) \
	X(void, glCompileShader, (GLuint shader)) \
	X(void, glGetShaderiv, (GLuint shader, GLenum pname, GLint *params)) \
	X(void, glGetShaderInfoLog, (GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog)) \
	X(void, glDeleteShader, (GLuint shader)) \
	X(GLuint, glCreateProgram, (void)) \
	X(void, glAttachShader, (GLuint program, GLuint shader)) \
	X(void, glLinkProgram, (GLuint program)) \
	X(void, glGetProgramiv, (GLuint program, GLenum pname, GLint *params)) \
	X(void, glGetProgramInfoLog, (GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog)) \
	X(void, glDeleteProgram, (GLuint program)) \
	X(GLboolean, glIsProgram, (GLuint program)) \
	X(void, glUseProgram, (GLuint program)) \
	X(GLint, glGetUniformLocation, (GLuint program, const GLchar *name)) \
	X(void, glGetActiveUniform, (GLuint program, GLuint index, GLsizei bufSize, GLsizei *length, GLint *size, \
		GLenum *type, GLchar *name)) \
	X(void, glUniform1i, (GLint location, GLint v0)) \
	X(void, glUniform4f, (GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3)) \
	X(void, glGenVertexArrays, (GLsizei n, GLuint *arrays)) \
	X(void, glBindVertexArray, (GLuint array)) \
	X(void, glDeleteVertexArrays, (GLsizei n, const GLuint *arrays)) \
	X(void, glGenFramebuffers, (GLsizei n, GLuint *framebuffers)) \
	X(void, glBindFramebuffer, (GLenum target, GLuint framebuffer)) \
	X(void, glDeleteFramebuffers, (GLsizei n, const GLuint *framebuffers)) \
	X(void, glFramebufferTexture2D, (GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level)) \
	X(void, glFramebufferRenderbuffer, (GLenum target, GLenum attachment, GLenum renderbuffertarget, \
		GLuint renderbuffer)) \
	X(GLenum, glCheckFramebufferStatus, (GLenum target)) \
	X(void, glGenRenderbuffers, (GLsizei n, GLuint *renderbuffers)) \
	X(void, glBindRenderbuffer, (GLenum target, GLuint renderbuffer)) \
	X(void, glRenderbufferStorage, (GLenum target, GLenum internalformat, GLsizei width, GLsizei height)) \
	X(void, glDeleteRenderbuffers, (GLsizei n, const GLuint *renderbuffers)) \
	X(void, glBlitFramebuffer, (GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, \
		GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter))

#define KP_GL_DECLARE(ret, name, params) typedef ret(APIENTRY *kp_pfn_##name) params; extern kp_pfn_##name kp_##name;
KP_GL_FUNCTIONS(KP_GL_DECLARE)
#undef KP_GL_DECLARE

#define glActiveTexture kp_glActiveTexture
#define glBlendFuncSeparate kp_glBlendFuncSeparate
#define glTexImage3D kp_glTexImage3D
#define glTexSubImage3D kp_glTexSubImage3D
#define glCompressedTexImage3D kp_glCompressedTexImage3D
#define glCompressedTexSubImage3D kp_glCompressedTexSubImage3D
#define glCreateShader kp_glCreateShader
#define glShaderSource kp_glShaderSource
#define glCompileShader kp_glCompileShader
#define glGetShaderiv kp_glGetShaderiv
#define glGetShaderInfoLog kp_glGetShaderInfoLog
#define glDeleteShader kp_glDeleteShader
#define glCreateProgram kp_glCreateProgram
#define glAttachShader kp_glAttachShader
#define glLinkProgram kp_glLinkProgram
#define glGetProgramiv kp_glGetProgramiv
#define glGetProgramInfoLog kp_glGetProgramInfoLog
#define glDeleteProgram kp_glDeleteProgram
#define glIsProgram kp_glIsProgram
#define glUseProgram kp_glUseProgram
#define glGetUniformLocation kp_glGetUniformLocation
#define glGetActiveUniform kp_glGetActiveUniform
#define glUniform1i kp_glUniform1i
#define glUniform4f kp_glUniform4f
#define glGenVertexArrays kp_glGenVertexArrays
#define glBindVertexArray kp_glBindVertexArray
#define glDeleteVertexArrays kp_glDeleteVertexArrays
#define glGenFramebuffers kp_glGenFramebuffers
#define glBindFramebuffer kp_glBindFramebuffer
#define glDeleteFramebuffers kp_glDeleteFramebuffers
#define glFramebufferTexture2D kp_glFramebufferTexture2D
#define glFramebufferRenderbuffer kp_glFramebufferRenderbuffer
#define glCheckFramebufferStatus kp_glCheckFramebufferStatus
#define glGenRenderbuffers kp_glGenRenderbuffers
#define glBindRenderbuffer kp_glBindRenderbuffer
#define glRenderbufferStorage kp_glRenderbufferStorage
#define glDeleteRenderbuffers kp_glDeleteRenderbuffers
#define glBlitFramebuffer kp_glBlitFramebuffer
#endif

/* Loads the table (a no-op where GL links directly); the name of the first missing function, or NULL. */
const char *kp_gl_load(void);

#endif
