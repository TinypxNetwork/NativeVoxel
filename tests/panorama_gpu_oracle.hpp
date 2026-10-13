#pragma once
#include "mc/gl_api.hpp"
#include "mc/renderer.hpp"
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

inline void verifyPanoramaJava(mc::Renderer& renderer,const std::string& path) {
    std::ifstream trace(path);if(!trace)throw std::runtime_error("Missing Java panorama trace: "+path);
    char command;int width,height;double seconds;trace>>command>>width>>height>>seconds;
    if(command!='P')throw std::runtime_error("Invalid panorama trace header");
    auto readPixels=[&](){std::vector<unsigned char> result(width*height*3);glPixelStorei(GL_PACK_ALIGNMENT,1);
        glReadPixels(0,0,width,height,GL_RGB,GL_UNSIGNED_BYTE,result.data());return result;};
    renderer.menuBackground(width,height,seconds);const auto actual=readPixels();
    std::array<GLuint,7> textures{};glGenTextures(7,textures.data());mc::gl::ActiveTexture(GL_TEXTURE0);
    for(int i=0;i<7;++i){
        unsigned w=256,h=256;std::vector<unsigned char> pixels;
        if(i<6){std::ifstream image("assets/runtime/gui/panorama_"+std::to_string(i)+".rgba",std::ios::binary);char magic[4];image.read(magic,4);
            image.read(reinterpret_cast<char*>(&w),4);image.read(reinterpret_cast<char*>(&h),4);if(!image||w>8192||h>8192)throw std::runtime_error("Panorama reference texture");
            pixels.resize(w*h*4);image.read(reinterpret_cast<char*>(pixels.data()),pixels.size());if(!image)throw std::runtime_error("Truncated panorama reference texture");}
        glBindTexture(GL_TEXTURE_2D,textures[i]);glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.empty()?nullptr:pixels.data());
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
    }
    const char* vs="#version 330 core\nlayout(location=0) in vec4 clip;layout(location=1) in vec2 tex;layout(location=2) in vec4 rgba;out vec2 uv;out vec4 color;void main(){gl_Position=clip;uv=tex;color=rgba;}";
    const char* fs="#version 330 core\nin vec2 uv;in vec4 color;uniform sampler2D image;out vec4 result;void main(){result=texture(image,uv)*color;}";
    auto shader=[](GLenum type,const char* source){const auto id=mc::gl::CreateShader(type);mc::gl::ShaderSource(id,1,&source,nullptr);mc::gl::CompileShader(id);GLint ok=0;mc::gl::GetShaderiv(id,GL_COMPILE_STATUS,&ok);if(!ok)throw std::runtime_error("Oracle shader compilation");return id;};
    const auto vertex=shader(GL_VERTEX_SHADER,vs),fragment=shader(GL_FRAGMENT_SHADER,fs),program=mc::gl::CreateProgram();
    mc::gl::AttachShader(program,vertex);mc::gl::AttachShader(program,fragment);mc::gl::LinkProgram(program);mc::gl::UseProgram(program);
    mc::gl::Uniform1i(mc::gl::GetUniformLocation(program,"image"),0);
    struct Vertex {float clip[4],uv[2];unsigned char color[4];};
    GLuint vao,vbo;mc::gl::GenVertexArrays(1,&vao);mc::gl::GenBuffers(1,&vbo);mc::gl::BindVertexArray(vao);mc::gl::BindBuffer(GL_ARRAY_BUFFER,vbo);
    mc::gl::EnableVertexAttribArray(0);mc::gl::VertexAttribPointer(0,4,GL_FLOAT,GL_FALSE,sizeof(Vertex),nullptr);
    mc::gl::EnableVertexAttribArray(1);mc::gl::VertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,sizeof(Vertex),reinterpret_cast<void*>(16));
    mc::gl::EnableVertexAttribArray(2);mc::gl::VertexAttribPointer(2,4,GL_UNSIGNED_BYTE,GL_TRUE,sizeof(Vertex),reinterpret_cast<void*>(24));
    mc::gl::BindFramebuffer(GL_FRAMEBUFFER,0);glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    unsigned draws=0,copies=0;
    while(trace>>command){
        if(command=='V'){int w,h;trace>>w>>h;glViewport(0,0,w,h);}
        else if(command=='B'){int id;trace>>id;glBindTexture(GL_TEXTURE_2D,textures.at(id));}
        else if(command=='F'){int parameter,value;trace>>parameter>>value;glTexParameteri(GL_TEXTURE_2D,parameter,value);}
        else if(command=='C'){glCopyTexSubImage2D(GL_TEXTURE_2D,0,0,0,0,0,256,256);++copies;}
        else if(command=='D'){
            int count;trace>>count;if(count%4||count>12)throw std::runtime_error("Invalid reference quad count");std::vector<Vertex> vertices;
            for(int q=0;q<count/4;++q){std::array<Vertex,4> quad;for(auto& v:quad){for(float& p:v.clip)trace>>p;for(float& p:v.uv)trace>>p;for(auto& c:v.color){float value;trace>>value;c=static_cast<unsigned char>(value);}}
                for(int i:{0,1,2,0,2,3})vertices.push_back(quad[i]);}
            mc::gl::BufferData(GL_ARRAY_BUFFER,vertices.size()*sizeof(Vertex),vertices.data(),GL_STREAM_DRAW);glDrawArrays(GL_TRIANGLES,0,int(vertices.size()));++draws;
        }else throw std::runtime_error("Unknown panorama command");
    }
    const auto reference=readPixels();unsigned maximum=0,significant=0;double sum=0;
    for(std::size_t i=0;i<actual.size();i+=3){unsigned error=0;for(int c=0;c<3;++c){const auto d=unsigned(std::abs(int(actual[i+c])-int(reference[i+c])));sum+=d;maximum=std::max(maximum,d);error=std::max(error,d);}if(error>6)++significant;}
    glDisable(GL_BLEND);glDepthMask(GL_TRUE);mc::gl::UseProgram(0);mc::gl::DeleteBuffers(1,&vbo);mc::gl::DeleteVertexArrays(1,&vao);
    mc::gl::DeleteProgram(program);mc::gl::DeleteShader(vertex);mc::gl::DeleteShader(fragment);glDeleteTextures(7,textures.data());
    const double mean=sum/actual.size();
    std::cout<<"Java panorama "<<width<<'x'<<height<<" t="<<seconds<<" draws="<<draws<<" copies="<<copies<<" mean byte error="<<mean<<" max="<<maximum<<" pixels>6="<<significant<<'\n';
    if(draws!=392||copies!=7||mean>1.5||significant>unsigned(width*height/100))throw std::runtime_error("Panorama differs from original Java draw command replay");
}
