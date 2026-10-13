#pragma once
#include "mc/platform.hpp"
#include <GL/gl.h>
#include <cstddef>
#include <stdexcept>

#ifndef GL_ARRAY_BUFFER
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW 0x88E4
#define GL_STREAM_DRAW 0x88E0
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE1 0x84C1
#define GL_TEXTURE2 0x84C2
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_CLAMP_TO_BORDER 0x812D
#define GL_TEXTURE_MAX_LEVEL 0x813D
#define GL_FRAMEBUFFER 0x8D40
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_RENDERBUFFER 0x8D41
#define GL_DEPTH_COMPONENT24 0x81A6
#define GL_RGBA16F 0x881A
#define GL_RGBA8 0x8058
#define GL_DEPTH_COMPONENT32F 0x8CAC
#define GL_MULTISAMPLE 0x809D
#endif

namespace mc::gl {
#define MC_GL_FUNCTIONS(X) \
X(void,GenBuffers,(GLsizei,GLuint*)) \
X(void,BindBuffer,(GLenum,GLuint)) \
X(void,BufferData,(GLenum,std::ptrdiff_t,const void*,GLenum)) \
X(void,BufferSubData,(GLenum,std::ptrdiff_t,std::ptrdiff_t,const void*)) \
X(void,DeleteBuffers,(GLsizei,const GLuint*)) \
X(void,GenVertexArrays,(GLsizei,GLuint*)) \
X(void,BindVertexArray,(GLuint)) \
X(void,DeleteVertexArrays,(GLsizei,const GLuint*)) \
X(void,EnableVertexAttribArray,(GLuint)) \
X(void,VertexAttribPointer,(GLuint,GLint,GLenum,GLboolean,GLsizei,const void*)) \
X(GLuint,CreateShader,(GLenum)) \
X(void,ShaderSource,(GLuint,GLsizei,const char*const*,const GLint*)) \
X(void,CompileShader,(GLuint)) \
X(void,GetShaderiv,(GLuint,GLenum,GLint*)) \
X(void,GetShaderInfoLog,(GLuint,GLsizei,GLsizei*,char*)) \
X(void,DeleteShader,(GLuint)) \
X(GLuint,CreateProgram,()) \
X(void,AttachShader,(GLuint,GLuint)) \
X(void,LinkProgram,(GLuint)) \
X(void,GetProgramiv,(GLuint,GLenum,GLint*)) \
X(void,GetProgramInfoLog,(GLuint,GLsizei,GLsizei*,char*)) \
X(void,DeleteProgram,(GLuint)) \
X(void,UseProgram,(GLuint)) \
X(GLint,GetUniformLocation,(GLuint,const char*)) \
X(void,Uniform1i,(GLint,GLint)) \
X(void,Uniform1f,(GLint,GLfloat)) \
X(void,Uniform2f,(GLint,GLfloat,GLfloat)) \
X(void,Uniform3f,(GLint,GLfloat,GLfloat,GLfloat)) \
X(void,Uniform4f,(GLint,GLfloat,GLfloat,GLfloat,GLfloat)) \
X(void,UniformMatrix4fv,(GLint,GLsizei,GLboolean,const GLfloat*)) \
X(void,ActiveTexture,(GLenum)) \
X(void,GenerateMipmap,(GLenum)) \
X(void,GenFramebuffers,(GLsizei,GLuint*)) \
X(void,BindFramebuffer,(GLenum,GLuint)) \
X(void,FramebufferTexture2D,(GLenum,GLenum,GLenum,GLuint,GLint)) \
X(GLenum,CheckFramebufferStatus,(GLenum)) \
X(void,DeleteFramebuffers,(GLsizei,const GLuint*)) \
X(void,GenRenderbuffers,(GLsizei,GLuint*)) \
X(void,BindRenderbuffer,(GLenum,GLuint)) \
X(void,RenderbufferStorage,(GLenum,GLenum,GLsizei,GLsizei)) \
X(void,FramebufferRenderbuffer,(GLenum,GLenum,GLenum,GLuint)) \
X(void,DeleteRenderbuffers,(GLsizei,const GLuint*)) \
X(void,BlitFramebuffer,(GLint,GLint,GLint,GLint,GLint,GLint,GLint,GLint,GLbitfield,GLenum))
#define MC_DECLARE_GL(ret,name,args) using name##Fn=ret(APIENTRY*)args; inline name##Fn name=nullptr;
MC_GL_FUNCTIONS(MC_DECLARE_GL)
#undef MC_DECLARE_GL
inline void load(){
#define MC_LOAD_GL(ret,name,args) name=reinterpret_cast<name##Fn>(Window::glProc("gl" #name));if(!name)throw std::runtime_error("OpenGL driver is missing gl" #name);
MC_GL_FUNCTIONS(MC_LOAD_GL)
#undef MC_LOAD_GL
}
}
