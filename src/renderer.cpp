#include "mc/renderer.hpp"
#include "mc/gl_api.hpp"
#include "mc/assets.hpp"
#include "mc/world.hpp"
#include "mc/entity_models.hpp"
#include "mc/registry.hpp"
#include "mc/render_assets.hpp"
#include "mc/item_entities.hpp"
#include "mc/render_fidelity.hpp"
#include "mc/entity_effects.hpp"
#include "mc/fov.hpp"
#include "mc/gui_json.hpp"
#include "mc/panorama.hpp"
#include "mc/block_geometry.hpp"
#include "mc/block_lighting.hpp"
#include "mc/game.hpp"
#include "mc/resource_packs.hpp"
#include "mc/sign_text.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <fstream>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <limits>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#ifdef MC_RENDER_PROFILE
#include <iostream>
#endif

namespace mc {
using namespace gfx;
using Clock = std::chrono::steady_clock;
namespace {
struct Program {
    GLuint id = 0;
    std::unordered_map<std::string, GLint> locations;
    GLint at(const char *n) {
        auto p = locations.find(n);
        if (p != locations.end())
            return p->second;
        return locations[n] = gl::GetUniformLocation(id, n);
    }
    void use() { gl::UseProgram(id); }
    void integer(const char *n, int v) { gl::Uniform1i(at(n), v); }
    void scalar(const char *n, float v) { gl::Uniform1f(at(n), v); }
    void vec2(const char *n, float x, float y) { gl::Uniform2f(at(n), x, y); }
    void vec3(const char *n, Vec3 v) { gl::Uniform3f(at(n), v.x, v.y, v.z); }
    void matrix(const char *n, const Mat4 &v) { gl::UniformMatrix4fv(at(n), 1, GL_FALSE, v.data()); }
    void build(const char *vs, const char *fs) {
        auto compile = [](GLenum type, const char *s) {
            GLuint shader = gl::CreateShader(type);
            gl::ShaderSource(shader, 1, &s, nullptr);
            gl::CompileShader(shader);
            GLint good;
            gl::GetShaderiv(shader, GL_COMPILE_STATUS, &good);
            if (!good) {
                std::array<char, 4096> b{};
                gl::GetShaderInfoLog(shader, GLsizei(b.size()), nullptr, b.data());
                gl::DeleteShader(shader);
                throw std::runtime_error(std::string("Shader compilation: ") + b.data());
            }
            return shader;
        };
        GLuint v = compile(GL_VERTEX_SHADER, vs), f = compile(GL_FRAGMENT_SHADER, fs);
        id = gl::CreateProgram();
        gl::AttachShader(id, v);
        gl::AttachShader(id, f);
        gl::LinkProgram(id);
        gl::DeleteShader(v);
        gl::DeleteShader(f);
        GLint good;
        gl::GetProgramiv(id, GL_LINK_STATUS, &good);
        if (!good) {
            std::array<char, 4096> b{};
            gl::GetProgramInfoLog(id, GLsizei(b.size()), nullptr, b.data());
            throw std::runtime_error(std::string("Shader link: ") + b.data());
        }
    }
    void destroy() {
        if (id)
            gl::DeleteProgram(id);
        id = 0;
    }
};
const char *terrainVS = R"GLSL(#version 330 core
layout(location=0) in vec3 aPosition;
layout(location=1) in vec2 aUV;
layout(location=2) in vec4 aColor;
layout(location=3) in vec4 aLight;
uniform mat4 uVP,uShadow;
uniform vec3 uOffset;
out vec2 uv;out vec4 color;out vec3 position;out vec4 shadowPosition;out vec2 lighting;
void main(){position=aPosition+uOffset;uv=aUV;color=aColor;lighting=aLight.xy;shadowPosition=uShadow*vec4(position,1);gl_Position=uVP*vec4(position,1);}
)GLSL";
const char *terrainFS = R"GLSL(#version 330 core
in vec2 uv;in vec4 color;in vec3 position;in vec4 shadowPosition;in vec2 lighting;
uniform sampler2D uAtlas,uLightmap,uShadowMap;uniform vec3 uCamera,uFog;uniform float uFogEnd,uFogDensity,uDay,uAlpha;uniform int uShadows,uTranslucent,uGlint,uHurt;
out vec4 result;
float shadow(){vec3 p=shadowPosition.xyz/shadowPosition.w*.5+.5;if(uShadows==0||p.z>=1||p.z<=0||p.x<=0||p.x>=1||p.y<=0||p.y>=1)return 1.;float s=0.;vec2 t=1./vec2(textureSize(uShadowMap,0));float bias=max(.00025,.0018*(1.-color.a));for(int x=-1;x<=1;++x)for(int y=-1;y<=1;++y)s+=p.z-bias<=texture(uShadowMap,p.xy+vec2(x,y)*t).r?1.:.25;return s/9.;}
void main(){vec4 tex=texture(uAtlas,uv);if(tex.a<.1)discard;
if(uGlint==1){result=vec4(tex.rgb*color.rgb,1);return;}
vec3 light=texture(uLightmap,lighting.yx*(15./16.)+vec2(1./32.)).rgb;
vec3 surface=tex.rgb*color.rgb;if(uHurt!=0)surface=mix(surface,vec3(1,0,0),.3);
vec3 rgb=surface*light;float distance=length(position-uCamera);float fog=uFogDensity>0.?1.-exp(-uFogDensity*distance):clamp((distance-uFogEnd*.75)/(uFogEnd*.25),0.,1.);rgb=mix(rgb,uFog,fog);
float alpha=uTranslucent!=0?tex.a:1.;result=vec4(rgb,alpha*uAlpha*color.a);}
)GLSL";
const char *shadowVS = R"GLSL(#version 330 core
layout(location=0) in vec3 aPosition;layout(location=1) in vec2 aUV;uniform mat4 uVP;uniform vec3 uOffset;out vec2 uv;void main(){uv=aUV;gl_Position=uVP*vec4(aPosition+uOffset,1);}
)GLSL";
const char *shadowFS = R"GLSL(#version 330 core
in vec2 uv;uniform sampler2D uAtlas;void main(){if(texture(uAtlas,uv).a<.5)discard;}
)GLSL";
const char *screenVS = R"GLSL(#version 330 core
out vec2 uv;void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);uv=p;gl_Position=vec4(p*2.-1.,0,1);}
)GLSL";
const char *skyFS = R"GLSL(#version 330 core
in vec2 uv;uniform vec3 uForward,uRight,uUp,uSky,uFog;uniform float uTan,uAspect,uAngle,uRain;uniform int uDimension,uPhase;
uniform sampler2D uSunTexture,uMoonTexture,uEndSkyTexture,uCloudTexture,uCustomSky;uniform float uCustomAlpha,uCameraY,uTime;uniform vec2 uCameraXZ;uniform int uCustomOnly;out vec4 result;
void main(){vec2 p=uv*2.-1.;vec3 ray=normalize(uForward+uRight*p.x*uTan*uAspect+uUp*p.y*uTan);
vec3 rgb=mix(uFog,uSky,clamp(ray.y*8.,0.,1.));
if(uDimension==1){vec2 t=vec2(atan(ray.z,ray.x)/6.2831853+.5,acos(clamp(ray.y,-1.,1.))/3.1415926);rgb=texture(uEndSkyTexture,t).rgb;}
if(uDimension==0){float c=cos(uAngle),s=sin(uAngle);vec3 r=vec3(ray.z,ray.y*c-ray.x*s,-ray.y*s-ray.x*c);
if(r.y>0.){vec2 t=r.xz/r.y/.6+.5;if(all(greaterThanEqual(t,vec2(0)))&&all(lessThanEqual(t,vec2(1)))){vec4 sun=texture(uSunTexture,t);rgb+=sun.rgb*sun.a*(1.-uRain);}}
if(r.y<0.){vec2 t=vec2(-r.x,-r.z)/r.y/.4+.5;if(all(greaterThanEqual(t,vec2(0)))&&all(lessThanEqual(t,vec2(1)))){vec4 moon=texture(uMoonTexture,(t+vec2(uPhase%4,uPhase/4))/vec2(4.,2.));rgb+=moon.rgb*moon.a*(1.-uRain);}}}
if(uDimension==0&&uCameraY<128.&&ray.y>.001){float reach=(128.-uCameraY)/ray.y;vec2 cloudUV=(uCameraXZ+ray.xz*reach+vec2(uTime*.6,0.))/2048.;vec4 cloud=texture(uCloudTexture,cloudUV);rgb=mix(rgb,cloud.rgb,cloud.a*.8*(1.-uRain));}
if(uCustomOnly!=0){vec3 a=abs(ray);vec2 face;int tile;if(a.y>=a.x&&a.y>=a.z){tile=ray.y>0.?0:1;face=vec2(ray.x,ray.z)/a.y;}else if(a.z>=a.x){tile=ray.z>0.?2:3;face=vec2(ray.x,ray.y)/a.z;}else{tile=ray.x>0.?4:5;face=vec2(ray.z,ray.y)/a.x;}vec2 tex=(vec2(float(tile%3),float(tile/3))+(face*.5+.5))/vec2(3.,2.);vec4 overlay=texture(uCustomSky,tex);result=vec4(overlay.rgb,overlay.a*uCustomAlpha);return;}
result=vec4(rgb,1);}
)GLSL";
const char *postFS = R"GLSL(#version 330 core
in vec2 uv;uniform sampler2D uScene;uniform vec2 uPixel;uniform float uExposure;uniform int uFXAA;out vec4 result;
float lum(vec3 c){return dot(c,vec3(.299,.587,.114));}
void main(){vec3 c=texture(uScene,uv).rgb;if(uFXAA==1){vec3 n=texture(uScene,uv+vec2(0,uPixel.y)).rgb,s=texture(uScene,uv-vec2(0,uPixel.y)).rgb,e=texture(uScene,uv+vec2(uPixel.x,0)).rgb,w=texture(uScene,uv-vec2(uPixel.x,0)).rgb;float hi=max(lum(c),max(max(lum(n),lum(s)),max(lum(e),lum(w))));float lo=min(lum(c),min(min(lum(n),lum(s)),min(lum(e),lum(w))));if(hi-lo>max(.05,hi*.18))c=mix(c,(n+s+e+w)*.25,.4);}result=vec4(c,1);}
)GLSL";
const char *panoramaVS = R"GLSL(#version 330 core
layout(location=0) in vec3 aPosition;layout(location=1) in vec2 aUV;
uniform mat4 uVP;out vec2 uv;
void main(){uv=aUV;gl_Position=uVP*vec4(aPosition,1);}
)GLSL";
const char *panoramaFS = R"GLSL(#version 330 core
in vec2 uv;uniform sampler2D uTexture;uniform float uAlpha;out vec4 result;
void main(){result=vec4(texture(uTexture,uv).rgb,uAlpha);}
)GLSL";
const char *panoramaBlitFS = R"GLSL(#version 330 core
in vec2 uv;uniform sampler2D uTexture;uniform vec2 uCrop;uniform float uOffset,uAlpha;uniform int uFinal;out vec4 result;
void main(){vec2 t=uFinal!=0?vec2(.5+(uv.y-.5)*uCrop.x,.5-(uv.x-.5)*uCrop.y):uv.yx+vec2(uOffset,0);result=vec4(texture(uTexture,t).rgb,uAlpha);}
)GLSL";
const char *uiVS = R"GLSL(#version 330 core
layout(location=0) in vec2 aPosition;layout(location=1) in vec2 aUV;layout(location=2) in vec4 aColor;uniform vec2 uSize;out vec2 uv;out vec2 pixel;out vec4 color;void main(){uv=aUV;pixel=aPosition;color=aColor;gl_Position=vec4(aPosition.x/uSize.x*2.-1.,1.-aPosition.y/uSize.y*2.,0,1);}
)GLSL";
const char *uiFS = R"GLSL(#version 330 core
in vec2 uv;in vec2 pixel;in vec4 color;uniform sampler2D uTexture,uGlint;uniform int uEffect;uniform float uTime;out vec4 result;
void main(){vec4 base=texture(uTexture,uv);result=base*color;if(result.a<.01)discard;
if(uEffect==1){vec2 p=pixel/128.;vec2 a=mat2(.707,-.707,.707,.707)*p+vec2(fract(uTime/3.),0);vec2 b=mat2(.866,.5,-.5,.866)*p-vec2(fract(uTime/4.873),0);result=vec4((texture(uGlint,a).rgb+texture(uGlint,b).rgb)*vec3(.5,.25,.8)*base.a,1);}}
)GLSL";
const char *lineVS = R"GLSL(#version 330 core
layout(location=0) in vec3 aPosition;uniform mat4 uVP;uniform vec3 uOffset;void main(){gl_Position=uVP*vec4(aPosition+uOffset,1);}
)GLSL";
const char *lineFS = R"GLSL(#version 330 core
uniform int uFishing;out vec4 result;void main(){result=uFishing!=0?vec4(0,0,0,1):vec4(.025,.025,.035,.85);}
)GLSL";
const char *unlitFS = R"GLSL(#version 330 core
in vec2 uv;in vec4 color;uniform sampler2D uAtlas;uniform float uAlpha;out vec4 result;
void main(){result=texture(uAtlas,uv)*color;result.a*=uAlpha;if(result.a<.003)discard;}
)GLSL";
GLuint makeTexture(unsigned w, unsigned h, const void *pixels, bool mip = false) {
    GLuint id;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, mip ? GL_NEAREST_MIPMAP_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    if (mip) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 1);
        gl::GenerateMipmap(GL_TEXTURE_2D);
    }
    return id;
}
struct Vertex {
    float x, y, z, u, v;
    std::uint8_t color[4];
    std::uint8_t light[4];
};
static_assert(sizeof(Vertex) == 28, "Packed terrain vertex layout");
struct UIVertex {
    float x, y, u, v;
    std::uint8_t color[4];
};
void setupTerrain(GLuint &vao, GLuint &vbo) {
    gl::GenVertexArrays(1, &vao);
    gl::GenBuffers(1, &vbo);
    gl::BindVertexArray(vao);
    gl::BindBuffer(GL_ARRAY_BUFFER, vbo);
    gl::EnableVertexAttribArray(0);
    gl::VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), nullptr);
    gl::EnableVertexAttribArray(1);
    gl::VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void *>(12));
    gl::EnableVertexAttribArray(2);
    gl::VertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), reinterpret_cast<void *>(20));
    gl::EnableVertexAttribArray(3);
    gl::VertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), reinterpret_cast<void *>(24));
}
std::uint64_t meshKey(int x, int sy, int z) {
    return ((std::uint64_t(std::uint32_t(x)) & 0x3ffffff) << 30) |
           ((std::uint64_t(std::uint32_t(z)) & 0x3ffffff) << 4) | unsigned(sy);
}
struct Snapshot {
    int cx = 0, sy = 0, cz = 0;
    std::uint64_t revision = 0, epoch = 0;
    bool ambientOcclusion=true,alternatives=true;
    std::array<BlockState, 20 * 20 * 20> blocks{};
    std::array<std::uint8_t, 20 * 20 * 20> light{};
    std::array<std::uint8_t, 20 * 20> biomes{};
    int index(int x, int y, int z) const { return (y + 2) * 20 * 20 + (z + 2) * 20 + (x + 2); }
    BlockState get(int x, int y, int z) const {
        if (x < -2 || x > 17 || y < -2 || y > 17 || z < -2 || z > 17)
            return 0;
        return blocks[index(x, y, z)];
    }
};
struct SignPlacement {
    int x, y, z;
    BlockState state;
};
struct MeshResult {
    int cx = 0, sy = 0, cz = 0;
    std::uint64_t revision = 0, epoch = 0;
    std::array<std::vector<Vertex>, 3> layers;
    std::vector<SignPlacement> signs, tiles;
    double milliseconds = 0;
};
struct GpuMesh {
    int cx = 0, sy = 0, cz = 0;
    std::uint64_t revision = 0;
    std::array<GLuint, 3> vao{}, vbo{};
    std::array<int, 3> count{};
    GLuint translucentElements = 0;
    std::vector<Vec3> translucentCenters;
    std::vector<std::uint32_t> translucentOrder;
    Vec3 sortedAt{1e20f,1e20f,1e20f};
    std::vector<SignPlacement> signs, tiles;
    void releaseLayer(int layer) {
        if(vbo[layer])gl::DeleteBuffers(1,&vbo[layer]);
        if(vao[layer])gl::DeleteVertexArrays(1,&vao[layer]);
        vbo[layer]=vao[layer]=0;count[layer]=0;
        if(layer==2){
            if(translucentElements)gl::DeleteBuffers(1,&translucentElements);
            translucentElements=0;
            std::vector<Vec3>().swap(translucentCenters);
            std::vector<std::uint32_t>().swap(translucentOrder);
        }
    }
    void destroy() {
        if(translucentElements)gl::DeleteBuffers(1,&translucentElements);
        translucentElements=0;
        for (int i = 0; i < 3; ++i) {
            if (vbo[i])
                gl::DeleteBuffers(1, &vbo[i]);
            if (vao[i])
                gl::DeleteVertexArrays(1, &vao[i]);
            vbo[i] = vao[i] = 0;
        }
    }
};
constexpr int faceX[] = {0, 0, 0, 0, -1, 1}, faceY[] = {-1, 1, 0, 0, 0, 0}, faceZ[] = {0, 0, -1, 1, 0, 0};
bool isFence(unsigned id) { return id == 85 || id == 113 || (id >= 188 && id <= 192); }
bool isPane(unsigned id) { return id == 101 || id == 102 || id == 160; }
bool isStair(unsigned id) {
    switch (id) {
    case 53:
    case 67:
    case 108:
    case 109:
    case 114:
    case 128:
    case 134:
    case 135:
    case 136:
    case 156:
    case 163:
    case 164:
    case 180:
        return true;
    default:
        return false;
    }
}
Vec3 tint(unsigned id, unsigned meta, unsigned biome) {
    if (id == 55) {
        float power = meta / 15.f;
        return {power * .6f + .4f, power * power * .7f - .5f > 0 ? power * power * .7f - .5f : 0.f,
                power * power * .6f - .7f > 0 ? power * power * .6f - .7f : 0.f};
    }
    if (id == 18 && (meta & 3) == 1)
        return {97 / 255.f, 153 / 255.f, 97 / 255.f};
    if (id == 18 && (meta & 3) == 2)
        return {128 / 255.f, 167 / 255.f, 85 / 255.f};
    const auto &b = registry::biome(biome);
    auto c = id == 8 || id == 9 ? b.waterColor : (id == 18 || id == 161 || id == 106) ? b.foliageColor : b.grassColor;
    return {((c >> 16) & 255) / 255.f, ((c >> 8) & 255) / 255.f, (c & 255) / 255.f};
}
const BlockModel &actualModel(const Assets &assets, const Snapshot &s, int x, int y, int z) {
    auto state = s.get(x, y, z);
    unsigned id = blockId(state);
    const int wx=s.alternatives?s.cx*16+x:0,wy=s.alternatives?s.sy*16+y:0,wz=s.alternatives?s.cz*16+z:0;
    if(id==175 && (blockMeta(state)&8))
        return assets.doublePlantModel(state,s.get(x,y-1,z),wx,wy,wz);
    std::vector<std::pair<std::string, std::string>> props;
    if (isFence(id) || isPane(id) || id == 139) {
        for (int f = 2; f < 6; ++f) {
            auto neighbor = s.get(x + faceX[f], y, z + faceZ[f]);
            unsigned n = blockId(neighbor);
            bool connects = id==139?visual::wallConnects(neighbor):(
                assets.model(neighbor).occludes ||
                (isFence(id)  ? ((isFence(n) && ((n == 113) == (id == 113))) || n == 107 || (n >= 183 && n <= 187))
                 : isPane(id) ? (isPane(n) || n == 20 || n == 95)
                              : n == 139));
            props.emplace_back(f == 2   ? "north"
                               : f == 3 ? "south"
                               : f == 4 ? "west"
                                        : "east",
                               connects ? "true" : "false");
        }
        if (id == 139)
            props.emplace_back("up",blockId(s.get(x,y+1,z))!=0?"true":"false");
    }
    if (isStair(id)) {
        const int fx[] = {1, -1, 0, 0}, fz[] = {0, 0, 1, -1};
        int facing = blockMeta(state) & 3;
        bool top = (blockMeta(state) & 4) != 0;
        auto same = [&](int dx, int dz) {
            auto other = s.get(x + dx, y, z + dz);
            return isStair(blockId(other)) && ((blockMeta(other) & 7) == (blockMeta(state) & 7));
        };
        std::string shape = "straight";
        for (int direction : {1, -1}) {
            auto other = s.get(x + fx[facing] * direction, y, z + fz[facing] * direction);
            int next = blockMeta(other) & 3;
            if (!isStair(blockId(other)) || ((blockMeta(other) & 4) != 0) != top || (facing < 2) == (next < 2))
                continue;
            int side = direction > 0 ? -1 : 1;
            if (same(fx[next] * side, fz[next] * side))
                continue;
            bool right = (fx[facing] * fz[next] - fz[facing] * fx[next] > 0) != top;
            shape = std::string(direction > 0 ? "outer_" : "inner_") + (right ? "right" : "left");
            break;
        }
        props.emplace_back("shape", shape);
    }
    if (id == 2 || id == 110) {
        unsigned above = blockId(s.get(x, y + 1, z));
        props.emplace_back("snowy", above == 78 || above == 80 ? "true" : "false");
    }
    if (id == 64 || id == 71 || (id >= 193 && id <= 197)) {
        unsigned meta = blockMeta(state);
        auto other = s.get(x, y + (meta & 8 ? -1 : 1), z);
        if (blockId(other) == id) {
            unsigned lower = meta & 8 ? blockMeta(other) : meta, upper = meta & 8 ? meta : blockMeta(other);
            const char *facing[] = {"east", "south", "west", "north"};
            props.emplace_back("facing", facing[lower & 3]);
            props.emplace_back("open", lower & 4 ? "true" : "false");
            props.emplace_back("hinge", upper & 1 ? "right" : "left");
        }
    }
    return props.empty() ? assets.model(state,wx,wy,wz):assets.modelWithProperties(state,props,wx,wy,wz);
}
MeshResult buildMesh(const Assets &assets, const Snapshot &s) {
    auto started = Clock::now();
    MeshResult result;
    result.cx = s.cx;
    result.sy = s.sy;
    result.cz = s.cz;
    result.revision = s.revision;
    result.epoch = s.epoch;
    auto get=[&](int x,int y,int z){return s.get(x,y,z);};
    auto light=[&](int x,int y,int z){return s.light[s.index(x,y,z)];};
    for (int y = 0; y < 16; ++y)
        for (int z = 0; z < 16; ++z)
            for (int x = 0; x < 16; ++x) {
                auto state = s.get(x, y, z);
                if (!state)
                    continue;
                unsigned id = blockId(state);
                if(id>=8&&id<=11){
                    const bool water=id==8||id==9;
                    const auto& still=assets.sprite(water?"minecraft:blocks/water_still":"minecraft:blocks/lava_still");
                    const auto& flow=assets.sprite(water?"minecraft:blocks/water_flow":"minecraft:blocks/lava_flow");
                    if(still.valid&&flow.valid){
                        auto& out=result.layers[water?2:0];
                        Vec3 tintColor{1,1,1};
                        if(water){unsigned red=0,green=0,blue=0;
                            for(int bx=-1;bx<=1;++bx)for(int bz=-1;bz<=1;++bz){
                                const auto color=registry::biome(s.biomes[(z+bz+2)*20+x+bx+2]).waterColor;
                                red+=color>>16&255;green+=color>>8&255;blue+=color&255;}
                            tintColor={float(red/9)/255.f,float(green/9)/255.f,float(blue/9)/255.f};
                        }
                        for(const auto& q:visual::fluidQuads(get,light,still,flow,x,y,z,water)){
                            const auto color=(q.tinted?tintColor:Vec3{1,1,1})*q.shade;
                            for(int i:{0,1,2,0,2,3}){const auto& v=q.vertices[i];
                                out.push_back({v.x,v.y,v.z,v.u,v.v,
                                    {std::uint8_t(color.x*255),std::uint8_t(color.y*255),std::uint8_t(color.z*255),255},
                                    {std::uint8_t((q.light>>4)*17),std::uint8_t((q.light&15)*17),0,0}});
                            }
                        }
                        continue;
                    }
                }
                if (id == 63 || id == 68) {
                    result.signs.push_back({s.cx * 16 + x, s.sy * 16 + y, s.cz * 16 + z, state});
                    continue;
                }
                if (TileEntityVisuals::handlesBlock(id)) {
                    result.tiles.push_back({s.cx * 16 + x, s.sy * 16 + y, s.cz * 16 + z, state});
                    continue;
                }
                const auto &model = actualModel(assets, s, x, y, z);
                auto &out = result.layers[unsigned(model.layer)];
                Vec3 blockTint = tint(id, blockMeta(state), s.biomes[(z + 2) * 20 + x + 2]);
                for (const auto &q : model.quads) {
                    int f = q.cullFace;
                    BlockState neighbor = 0;
                    if (f >= 0 && f < 6) {
                        neighbor = s.get(x + faceX[f], y + faceY[f], z + faceZ[f]);
                        const auto &nm = assets.model(neighbor);
                        if (nm.occludes)
                            continue;
                        if ((model.layer == RenderLayer::Translucent || id == 20 || id == 95 || id == 18 ||
                             id == 161) &&
                            blockId(neighbor) == id && (id != 95 || neighbor == state))
                            continue;
                    }
                    int face = q.face;
                    float shade = q.shade ? (face == 0                ? .5f
                                             : face == 2 || face == 3 ? .8f
                                             : face == 4 || face == 5 ? .6f
                                                                      : 1.f)
                                          : 1.f;
                    Vertex vv[4]{};
                    const auto vertexLight=visual::quadLighting(get,light,x,y,z,state,q,
                        s.ambientOcclusion&&model.ambientOcclusion&&model.layer!=RenderLayer::Translucent);
                    for (int i = 0; i < 4; ++i) {
                        const auto &v = q.vertices[i];
                        Vec3 c = q.tintIndex >= 0 ? blockTint : Vec3{1, 1, 1};
                        c = c * (shade * vertexLight[i].ao);
                        vv[i] = {v.x + x,
                                 v.y + y,
                                 v.z + z,
                                 v.u,
                                 v.v,
                                 {std::uint8_t(std::clamp(c.x, 0.f, 1.f) * 255),
                                  std::uint8_t(std::clamp(c.y, 0.f, 1.f) * 255),
                                  std::uint8_t(std::clamp(c.z, 0.f, 1.f) * 255), 255},
                                 {std::uint8_t(std::clamp(vertexLight[i].sky*17.f,0.f,255.f)),
                                  std::uint8_t(std::clamp(vertexLight[i].block*17.f,0.f,255.f)),0,0}};
                    }
                    const int indices[] = {0, 1, 2, 0, 2, 3};
                    for (int i : indices)
                        out.push_back(vv[i]);
                }
            }
    result.milliseconds = std::chrono::duration<double, std::milli>(Clock::now() - started).count();
    return result;
}
class MeshWorkers {
  public:
    explicit MeshWorkers(const Assets &a) : assets(a) {
        unsigned n = std::max(
            1u, std::min(6u, std::thread::hardware_concurrency() > 2 ? std::thread::hardware_concurrency() - 2 : 1));
        for (unsigned i = 0; i < n; ++i)
            threads.emplace_back([this] { run(); });
    }
    ~MeshWorkers() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stop = true;
        }
        wake.notify_all();
        for (auto &t : threads)
            t.join();
    }
    void submit(Snapshot &&s) {
        std::lock_guard<std::mutex> lock(mutex);
        jobs.push_back(std::move(s));
        wake.notify_one();
    }
    std::deque<MeshResult> take() {
        std::lock_guard<std::mutex> lock(mutex);
        std::deque<MeshResult> r;
        r.swap(done);
        return r;
    }
    void clear() {
        std::lock_guard<std::mutex> lock(mutex);
        jobs.clear();
        done.clear();
    }

  private:
    const Assets &assets;
    std::mutex mutex;
    std::condition_variable wake;
    bool stop = false;
    std::deque<Snapshot> jobs;
    std::deque<MeshResult> done;
    std::vector<std::thread> threads;
    void run() {
        for (;;) {
            Snapshot s;
            {
                std::unique_lock<std::mutex> lock(mutex);
                wake.wait(lock, [&] { return stop || !jobs.empty(); });
                if (stop)
                    return;
                s = std::move(jobs.front());
                jobs.pop_front();
            }
            auto result = buildMesh(assets, s);
            {
                std::lock_guard<std::mutex> lock(mutex);
                done.push_back(std::move(result));
            }
        }
    }
};
struct UIDraw {
    GLuint texture = 0;
    std::vector<UIVertex> vertices;
    bool glint = false;
};
struct RuntimeTexture {
    GLuint id = 0;
    unsigned width = 0, height = 0;
};
RuntimeTexture readRuntimeTexture(const std::string &path) {
    std::ifstream f(path, std::ios::binary);
    char magic[4]{};
    std::uint32_t w = 0, h = 0;
    f.read(magic, 4);
    f.read(reinterpret_cast<char *>(&w), 4);
    f.read(reinterpret_cast<char *>(&h), 4);
    if (!f || std::memcmp(magic, "NVTX", 4) || w == 0 || h == 0 || w > 8192 || h > 8192)
        return {};
    std::vector<std::uint8_t> pixels(std::size_t(w) * h * 4);
    f.read(reinterpret_cast<char *>(pixels.data()), std::streamsize(pixels.size()));
    if (!f)
        return {};
    return {makeTexture(w, h, pixels.data()), w, h};
}
std::vector<char32_t> decodeText(const std::string &s) {
    std::vector<char32_t> out;
    for (std::size_t i = 0; i < s.size();) {
        unsigned c = std::uint8_t(s[i++]);
        if (c < 128) {
            out.push_back(c);
            continue;
        }
        int n = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : 0;
        char32_t code = c & ((1u << (6 - n)) - 1);
        bool valid = n > 0 && i + std::size_t(n) <= s.size();
        for (int j = 0; j < n && valid; ++j) {
            unsigned tail = std::uint8_t(s[i++]);
            if ((tail & 0xc0) != 0x80) {
                valid = false;
                break;
            }
            code = (code << 6) | (tail & 63);
        }
        out.push_back(valid ? code : 0xfffd);
    }
    return out;
}
#include "secret_shaders.inc"
} // namespace

struct Renderer::Impl {
#ifdef MC_RENDER_PROFILE
    std::array<double, 4> profileTotals{};
    std::size_t profileSamples = 0;
#endif
    const Assets *assets = nullptr;
    std::unique_ptr<MeshWorkers> workers;
    std::uint64_t epoch = 1;
    std::unordered_map<std::uint64_t, GpuMesh> meshes;
    std::unordered_map<std::uint64_t, std::uint64_t> pending;
    Program terrain, shadow, sky, post, panorama, panoramaBlit, ui, line, label;
    SecretShaders secretShaders;
    bool meshAmbientOcclusion=true,meshAlternatives=true;
    int appliedMipmapLevels=-1;
    GLuint labelVAO=0,labelVBO=0;
    std::array<GLuint,2> panoramaFBO{}, panoramaTexture{};
    GLuint lightmapTexture = 0;
    std::array<std::uint8_t,1024> previousLightmap{};
    GLuint atlas = 0, font = 0, white = 0, emptyVAO = 0, uiVAO = 0, uiVBO = 0, lineVAO = 0, lineVBO = 0;
    GLuint sceneFBO = 0, sceneTexture = 0, sceneDepth = 0, shadowFBO = 0, shadowTexture = 0;
    int width = 0, height = 0, uiWidth = 0, uiHeight = 0;
    RenderStats stats;
    std::array<int, 256> glyphWidth{};
    std::vector<UIDraw> uiDraws;
    struct SkyLayer{GLuint texture=0;int in0=0,in1=0,out0=0,out1=0;};
    std::vector<SkyLayer> skyLayers;
    Mat4 lastVP;
    Mat4 viewBobbing=Mat4::identity();
    double originX = 0, originY = 0, originZ = 0;
    Camera lastCamera;
    bool hasSelection = false;
    std::array<double, 6> selectionBox{};
    std::vector<RenderBreak> blockBreaks;
    std::vector<ParticleSprite> particles;
#include "renderer_effects.inc"
    void drawSelection() {
        if (!hasSelection)
            return;
        const auto &b = selectionBox;
        float a = -.002f;
        Vec3 ext{float(b[3] - b[0]) + .002f, float(b[4] - b[1]) + .002f, float(b[5] - b[2]) + .002f};
        Vec3 corners[8] = {{a, a, a},     {ext.x, a, a},     {ext.x, a, ext.z},     {a, a, ext.z},
                           {a, ext.y, a}, {ext.x, ext.y, a}, {ext.x, ext.y, ext.z}, {a, ext.y, ext.z}};
        const int edges[] = {0, 1, 1, 2, 2, 3, 3, 0, 4, 5, 5, 6, 6, 7, 7, 4, 0, 4, 1, 5, 2, 6, 3, 7};
        std::array<Vec3, 24> v{};
        for (int i = 0; i < 24; ++i)
            v[i] = corners[edges[i]];
        line.use();
        line.matrix("uVP", lastVP);
        line.vec3("uOffset", {float(b[0] - originX), float(b[1] - originY), float(b[2] - originZ)});
        gl::BindVertexArray(lineVAO);
        gl::BindBuffer(GL_ARRAY_BUFFER, lineVBO);
        gl::BufferData(GL_ARRAY_BUFFER, sizeof(v), v.data(), GL_STREAM_DRAW);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        glDrawArrays(GL_LINES, 0, 24);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }
    std::unordered_map<std::string, RuntimeTexture> runtimeTextures;
    std::string assetDirectory;
    UnicodeFont originalFont;
    std::array<GLuint, 256> unicodePages{};
    bool forceUnicode = false;
    bool renderingTextShadow = false;
    ItemIconAtlas itemIcons;
    GLuint itemAtlas = 0;
    AnimationAtlasCache atlasAnimation, itemAnimation;
    std::uint64_t lastAnimationTick = ~std::uint64_t(0);
    GLuint fontPage(int page) {
        if (page < 0)
            return font;
        if (page > 255)
            return 0;
        if (!unicodePages[page]) {
            std::ostringstream hex;
            hex << std::hex << std::setw(2) << std::setfill('0') << page;
            unicodePages[page] =
                readRuntimeTexture(assetDirectory + "/runtime/font/unicode_page_" + hex.str() + ".rgba").id;
        }
        return unicodePages[page];
    }
    void animate(std::uint64_t tick) {
        if (tick == lastAnimationTick)
            return;
        lastAnimationTick = tick;
        gl::ActiveTexture(GL_TEXTURE0);
        auto update = [&](AnimationAtlasCache &animation, GLuint texture, bool mip) {
            const auto &changes = animation.sample(tick);
            if (changes.empty())
                return;
            glBindTexture(GL_TEXTURE_2D, texture);
            for (const auto &frame : changes)
                glTexSubImage2D(GL_TEXTURE_2D, 0, frame.x, frame.y, frame.width, frame.height, GL_RGBA,
                                GL_UNSIGNED_BYTE, frame.pixels.data());
            if (mip)
                gl::GenerateMipmap(GL_TEXTURE_2D);
        };
        update(atlasAnimation, atlas, true);
        update(itemAnimation, itemAtlas, false);
        update(firstAnimation, firstAtlas, false);
    }
    EntityModels entityModels;
    std::vector<GLuint> entityTextures;
    GLuint entityVAO = 0, entityVBO = 0;
    GLuint firstVAO = 0, firstVBO = 0;
    FirstPersonAssets firstPersonAssets;
    TileEntityVisuals tileVisuals;
    GLuint firstAtlas = 0, xpTexture = 0;
    bool firstPersonVisible = false;
    FirstPersonPose firstPose;
    ItemStack firstItem;
    std::shared_ptr<const PlayerSkin> localSkin;
    SkinCache* skinCache = nullptr;
    std::array<ItemStack,5> previewEquipment{};
    double timeSeconds=0;
    AnimationAtlasCache firstAnimation;
    struct CachedTexture {
        GLuint id = 0;
        std::uint64_t revision = 0;
        std::uint64_t lastUseFrame = 0;
    };
    std::unordered_map<std::string, CachedTexture> extraTextures;
    std::uint64_t textureFrame = 0;
    void pruneTextures() {
        ++textureFrame;
        if(extraTextures.size()<=512&&textureFrame%60!=0)return;
        std::vector<std::pair<std::uint64_t,std::string>> candidates;
        for(const auto& pair:extraTextures)if(textureFrame-pair.second.lastUseFrame>2)candidates.push_back({pair.second.lastUseFrame,pair.first});
        std::sort(candidates.begin(),candidates.end());
        for(const auto& candidate:candidates){
            if(extraTextures.size()<=512&&textureFrame-candidate.first<=240)break;
            const auto found=extraTextures.find(candidate.second);
            if(found->second.id)glDeleteTextures(1,&found->second.id);
            extraTextures.erase(found);
        }
    }
    struct EntityDraw {
        int first, count;
        GLuint texture;
        float alpha = 1;
        bool cull = true;
        bool noDepthWrite = false;
        bool glint = false;
        bool hurt = false;
    };
    std::vector<EntityDraw> entityDraws;
    std::vector<Vertex> entityVertices;
    GLuint cachedTexture(const std::string &key, std::uint64_t revision, unsigned w, unsigned h,
                         const std::vector<std::uint8_t> &data, bool repeat = false) {
        auto &entry = extraTextures[key];
        entry.lastUseFrame=textureFrame;
        if (!entry.id || entry.revision != revision) {
            if (entry.id)
                glDeleteTextures(1, &entry.id);
            entry.id = makeTexture(w, h, data.data());
            entry.revision = revision;
            if (repeat) {
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            }
        }
        return entry.id;
    }
    GLuint skinTexture(const std::shared_ptr<const PlayerSkin> &skin) {
        if (skin)
            return cachedTexture(skin->textureKey, skin->revision, skin->image.width, skin->image.height,
                                 skin->image.pixels);
        int index = entityModels.textureIndex("minecraft:entity/steve");
        return index >= 0 ? entityTextures[index] : font;
    }
    static std::uint32_t lightAt(const World &world, int x, int y, int z) {
        return (std::uint32_t(world.getSkyLight(x, y, z)) * 16u << 16) | (world.getBlockLight(x, y, z) * 16u);
    }
    void appendMesh(const FirstPersonMesh &mesh, const FirstPersonMatrix &m, GLuint texture, std::uint32_t color,
                    std::uint32_t packed, bool cull = true, const FirstPersonMatrix *uvMatrix = nullptr,
                    bool noDepthWrite = false,bool glint=false) {
        int first = int(entityVertices.size());
        Vec3 ax{m[0], m[1], m[2]}, ay{m[4], m[5], m[6]}, az{m[8], m[9], m[10]};
        float det = dot(ax, cross(ay, az));
        for (const auto &v : mesh.vertices) {
            Vec3 position{m[0] * v.x + m[4] * v.y + m[8] * v.z + m[12], m[1] * v.x + m[5] * v.y + m[9] * v.z + m[13],
                          m[2] * v.x + m[6] * v.y + m[10] * v.z + m[14]};
            Vec3 normal = normalize((cross(ay, az) * v.nx + cross(az, ax) * v.ny + cross(ax, ay) * v.nz) *
                                    (det < 0 ? -1.f : 1.f));
            float shade = glint?1.f:std::clamp(.4f+.6f*std::max(0.f,dot(normal,Vec3{.2f,1,-.7f})/1.2369317f)+.6f*std::max(0.f,dot(normal,Vec3{-.2f,1,.7f})/1.2369317f),.4f,1.f);
            float u = v.u, t = v.v;
            if (uvMatrix) {
                u = (*uvMatrix)[0] * v.u + (*uvMatrix)[4] * v.v + (*uvMatrix)[12];
                t = (*uvMatrix)[1] * v.u + (*uvMatrix)[5] * v.v + (*uvMatrix)[13];
            }
            entityVertices.push_back(
                {position.x,
                 position.y,
                 position.z,
                 u,
                 t,
                 {std::uint8_t(((color >> 16) & 255) * shade), std::uint8_t(((color >> 8) & 255) * shade),
                  std::uint8_t((color & 255) * shade), 255},
                 {std::uint8_t(std::min(15u, (packed >> 20) & 15u) * 17),
                  std::uint8_t(std::min(15u, (packed >> 4) & 15u) * 17), 0, 0}});
        }
        entityDraws.push_back(
            {first, int(entityVertices.size()) - first, texture, float(color >> 24) / 255.f, cull, noDepthWrite||glint,glint});
    }
    void prepareEntities(const World &world, const std::vector<RenderEntity> &entities) {
        pruneTextures();
        entityVertices.clear();
        entityDraws.clear();
        const Frustum frustum(lastVP);
        for (const auto &entity : entities) {
            // EntityPickupFX moves only the render position. Its entity (and
            // therefore its brightness sample) stays at the collection site.
            const double lightX=entity.pickup?entity.pickupSourceX:entity.x;
            const double lightZ=entity.pickup?entity.pickupSourceZ:entity.z;
            const double pickupLightY=entity.pickupSourceY+static_cast<double>((entity.kind==3?.5f:.2f)*.85f);
            if (std::abs(entity.x - lastCamera.x) > 200 || std::abs(entity.z - lastCamera.z) > 200)
                continue;
            Vec3 lo{float(entity.x-originX)-entity.width-1,float(entity.y-originY)-1,float(entity.z-originZ)-entity.width-1};
            if(!frustum.visible(lo,lo+Vec3{entity.width*2+2,entity.height+2,entity.width*2+2}))continue;
            if(entity.kind==2&&(entity.type==61||entity.type==62||entity.type==65||entity.type==73||entity.type==75||entity.type==90)){
                ThrownItemPose pose;pose.itemId=entity.type==61?332:entity.type==62?344:entity.type==65?368:entity.type==73?373:384;
                pose.metadata=entity.itemMetadata;pose.fishingHook=entity.type==90;pose.nbt=entity.equipment[0].nbt;
                pose.x=entity.x-originX;pose.y=entity.y-originY;pose.z=entity.z-originZ;pose.viewYaw=lastCamera.yaw;pose.viewPitch=lastCamera.pitch;
                pose.renderTimeMillis=std::int64_t(timeSeconds*1000);pose.packedLight=lightAt(world,floorBlock(entity.x),floorBlock(entity.y+.21250000596046448),floorBlock(entity.z));
                const auto frame=ItemEntityVisuals::thrown(firstPersonAssets,pose);
                for(const auto& draw:frame.draws)if(draw.mesh){const GLuint texture=draw.glint?runtimeTextures["glint"].id:draw.texture==ItemEntityTexture::ParticleAtlas?runtimeTextures["particles"].id:firstAtlas;
                    appendMesh(*draw.mesh,draw.matrix,texture,draw.color,frame.packedLight,false,&draw.textureMatrix,false,draw.glint);}
                continue;
            }
            if(entity.kind==2&&entity.type==60) {
                ArrowPose pose;pose.x=entity.x-originX;pose.y=entity.y-originY;pose.z=entity.z-originZ;
                pose.yaw=pose.previousYaw=entity.yaw;pose.pitch=pose.previousPitch=entity.pitch;
                pose.shakeTicks=entity.projectileShake;pose.partialTicks=entity.pose.partialTick;
                const auto frame=EntityVisuals::arrow(pose);
                int texture=entityModels.textureIndex("minecraft:arrow");
                if(texture<0)texture=entityModels.textureIndex("minecraft:entity/arrow");
                if(texture>=0)for(const auto& draw:frame.draws)if(draw.mesh)
                    appendMesh(*draw.mesh,draw.matrix,entityTextures[texture],draw.color,lightAt(world,floorBlock(lightX),floorBlock(entity.pickup?pickupLightY:entity.y),floorBlock(lightZ)),false);
                continue;
            }
            if (entity.itemId >= 0 || entity.kind == 3) {
                auto packed = lightAt(world, floorBlock(lightX), floorBlock(entity.pickup?pickupLightY:entity.y+.25), floorBlock(lightZ));
                ItemEntityFrame frame;
                if (entity.kind == 3) {
                    XPOrbPose pose;
                    pose.amount = entity.xpValue;
                    pose.xpColor = int(entity.ageTicks);
                    pose.partialTicks = entity.ageTicks - std::floor(entity.ageTicks);
                    pose.viewYaw = lastCamera.yaw;
                    pose.viewPitch = lastCamera.pitch;
                    pose.x = entity.x - originX;
                    pose.y = entity.y - originY;
                    pose.z = entity.z - originZ;
                    pose.packedLight = packed;
                    frame = ItemEntityVisuals::orb(pose);
                } else {
                    DroppedItemPose pose;
                    pose.itemId = entity.itemId;
                    pose.metadata = entity.itemMetadata;
                    pose.count = entity.itemCount;
                    pose.ageTicks = int(entity.ageTicks);
                    pose.partialTicks = entity.ageTicks - std::floor(entity.ageTicks);
                    pose.hoverStart = entity.hoverStart;
                    pose.x = entity.x - originX;
                    pose.y = entity.y - originY;
                    pose.z = entity.z - originZ;
                    pose.packedLight = packed;
                    pose.nbt=&entity.itemNbt;
                    pose.renderTimeMillis=std::int64_t(timeSeconds*1000);
                    frame = ItemEntityVisuals::dropped(firstPersonAssets, pose, &tileVisuals, skinCache);
                }
                for (const auto &draw : frame.draws)
                    if (draw.mesh) {
                        GLuint texture=draw.glint?runtimeTextures["glint"].id:
                            draw.texture==ItemEntityTexture::ExperienceOrb?xpTexture:firstAtlas;
                        if(draw.dynamicTexture) {
                            const auto& t=*draw.dynamicTexture;
                            texture=cachedTexture(t.key,t.revision,t.width,t.height,t.pixels,t.repeat);
                        }
                        appendMesh(*draw.mesh,draw.matrix,texture,draw.color,frame.packedLight,
                                   draw.texture!=ItemEntityTexture::ExperienceOrb,&draw.textureMatrix,false,draw.glint);
                    }
                continue;
            }
            EntityPose pose = entity.pose;
            pose.headYaw = (entity.headYaw - entity.yaw) * .01745329252f;
            pose.headPitch = entity.pitch * .01745329252f;
            pose.limbSwing = entity.limbSwing;
            pose.limbSwingAmount = entity.limbSwingAmount;
            pose.ageTicks = entity.ageTicks;
            pose.sneaking = entity.sneaking;
            pose.child = entity.child;
            if (entity.player && entity.skin)
                pose.variant = entity.skin->slim ? 1 : 0;
            if(entity.player || (entity.kind==1&&(entity.type==30||entity.type==51||entity.type==53||entity.type==54||entity.type==57)) || (entity.kind==2&&entity.type==78)) {
                PlayerVisualInput input;input.pose=pose;input.equipment=entity.equipment;input.usingItem=entity.usingItem;
                input.kind=entity.kind;input.type=entity.type;
                input.useAction=registry::item(entity.equipment[0].id).useAction;
                input.maxUseTicks=registry::item(entity.equipment[0].id).useDuration;
                input.useTicksRemaining=entity.useTicksRemaining;
                input.renderTimeMillis=std::int64_t(timeSeconds*1000);
                auto frame=EntityVisuals::player(entityModels,firstPersonAssets,input,&tileVisuals,skinCache);
                const auto packed=lightAt(world,floorBlock(entity.x),floorBlock(entity.y+1),floorBlock(entity.z));
                const std::uint8_t sky=std::uint8_t(((packed>>20)&15)*17),block=std::uint8_t(((packed>>4)&15)*17);
                gfx::Mat4 worldMatrix=visual::livingOrientation(entity.yaw,entity.kind,entity.type,entity.deathTime,pose.partialTick);
                worldMatrix.v[12]=float(entity.x-originX);worldMatrix.v[13]=float(entity.y-originY);worldMatrix.v[14]=float(entity.z-originZ);
                std::size_t layerIndex=0;
                for(const auto& layer:frame.layers){
                    const bool body=layerIndex++==0;
                    if(body&&entity.invisible&&!entity.translucentBody)continue;
                    const auto& mesh=layer.mesh;if(!mesh.supported||mesh.texture<0)continue;
                    GLuint texture=layer.playerSkin?skinTexture(entity.skin):entityTextures[mesh.texture];
                    const auto first=int(entityVertices.size());
                    for(const auto& v:mesh.vertices){
                        const auto& m=worldMatrix.v;
                        const float nx=m[0]*v.nx+m[4]*v.ny+m[8]*v.nz,ny=m[1]*v.nx+m[5]*v.ny+m[9]*v.nz,nz=m[2]*v.nx+m[6]*v.ny+m[10]*v.nz;
                        const float shade=layer.glint?1.f:std::clamp(.4f+.6f*std::max(0.f,(nx*.2f+ny-nz*.7f)/1.2369317f)+.6f*std::max(0.f,(-nx*.2f+ny+nz*.7f)/1.2369317f),.4f,1.f);
                        const auto& u=layer.textureMatrix;
                        entityVertices.push_back({m[0]*v.x+m[4]*v.y+m[8]*v.z+m[12],m[1]*v.x+m[5]*v.y+m[9]*v.z+m[13],m[2]*v.x+m[6]*v.y+m[10]*v.z+m[14],u[0]*v.u+u[4]*v.v+u[12],u[1]*v.u+u[5]*v.v+u[13],{std::uint8_t(layer.color[0]*shade*255),std::uint8_t(layer.color[1]*shade*255),std::uint8_t(layer.color[2]*shade*255),255},{sky,block,0,0}});
                    }
                    entityDraws.push_back({first,int(entityVertices.size())-first,texture,body&&entity.invisible?.15f:layer.color[3],true,layer.glint||(body&&entity.invisible),layer.glint,body&&(entity.hurtTime>0||entity.deathTime>0)});
                }
                for(const auto& draw:frame.held)if(draw.mesh){gfx::Mat4 matrix;matrix.v=draw.matrix;
                    GLuint texture=draw.glint?runtimeTextures["glint"].id:firstAtlas;
                    if(draw.dynamicTexture){const auto& t=*draw.dynamicTexture;texture=cachedTexture(t.key,t.revision,t.width,t.height,t.pixels,t.repeat);}
                    appendMesh(*draw.mesh,(worldMatrix*matrix).v,texture,draw.color,packed,true,&draw.textureMatrix,draw.translucent,draw.glint);}
                for(const auto& draw:frame.customHead)if(draw.mesh&&draw.texture){gfx::Mat4 matrix;matrix.v=draw.matrix;const auto& t=*draw.texture;
                    appendMesh(*draw.mesh,(worldMatrix*matrix).v,cachedTexture(t.key,t.revision,t.width,t.height,t.pixels),draw.color,packed,draw.cull,&draw.textureMatrix);}
                continue;
            }
            if(entity.invisible)continue;
            auto mesh = entityModels.build(entity.player ? 0 : entity.kind, entity.type, pose);
            if (!mesh.supported || mesh.texture < 0 || std::size_t(mesh.texture) >= entityTextures.size())
                continue;
            int bx = floorBlock(entity.x), by = floorBlock(entity.y + 1), bz = floorBlock(entity.z);
            std::uint8_t sky = world.getSkyLight(bx, by, bz) * 17, light = world.getBlockLight(bx, by, bz) * 17;
            const auto orientation=visual::livingOrientation(entity.yaw,entity.kind,entity.type,entity.deathTime,pose.partialTick);
            const auto& m=orientation.v;
            entityDraws.push_back(
                {int(entityVertices.size()), int(mesh.vertices.size()),
                 entity.player && entity.skin ? skinTexture(entity.skin) : entityTextures[mesh.texture],1,true,false,false,entity.hurtTime>0||entity.deathTime>0});
            for (const auto &v : mesh.vertices) {
                float nx = m[0]*v.nx+m[4]*v.ny+m[8]*v.nz,ny=m[1]*v.nx+m[5]*v.ny+m[9]*v.nz,nz=m[2]*v.nx+m[6]*v.ny+m[10]*v.nz;
                float shade = std::clamp(.62f + std::max(0.f, ny) * .38f - std::max(0.f, -ny) * .12f +
                                             std::max(0.f, nx * .35f + nz * .3f) * .15f,
                                         .4f, 1.f);
                auto color = std::uint8_t(shade * 255);
                entityVertices.push_back({float(m[0]*v.x+m[4]*v.y+m[8]*v.z + entity.x - originX),
                                          float(m[1]*v.x+m[5]*v.y+m[9]*v.z + entity.y - originY),
                                          float(m[2]*v.x+m[6]*v.y+m[10]*v.z + entity.z - originZ),
                                          v.u,
                                          v.v,
                                          {color, color, color, 255},
                                          {sky, light, 0, 0}});
            }
        }
    }
    void prepareParticles(const World& world) {
        const auto basis=cameraBasis(lastCamera.yaw,lastCamera.pitch);
        for(int layer=0;layer<5;++layer){
            const int first=int(entityVertices.size());GLuint texture=layer==0?runtimeTextures["particles"].id:layer==1?atlas:layer==2?itemAtlas:layer==3?runtimeTextures["explosion"].id:runtimeTextures["footprint"].id;
            if(!texture)continue;
            for(const auto& p:particles){
                if(p.texture!=layer||std::abs(p.x-lastCamera.x)>256||std::abs(p.z-lastCamera.z)>256)continue;
                float u0=float(p.index%16)/16,v0=float(p.index/16)/16,u1=u0+.0624375f,v1=v0+.0624375f;
                Vec3 rgb{p.r,p.g,p.b};
                if(layer==3){u0=float(p.index%4)/4;v0=float(p.index/4)/4;u1=u0+.24975f;v1=v0+.24975f;}
                if(layer==4){u0=v0=0;u1=v1=1;}
                if(layer==1){
                    const auto& model=assets->model(p.block);if(model.quads.empty())continue;
                    const auto& q=model.quads.front();u0=v0=1;u1=v1=0;
                    for(const auto& v:q.vertices){u0=std::min(u0,v.u);u1=std::max(u1,v.u);v0=std::min(v0,v.v);v1=std::max(v1,v.v);}
                    if(q.tintIndex>=0){const auto t=tint(blockId(p.block),blockMeta(p.block),world.getBiome(floorBlock(p.x),floorBlock(p.z)));rgb={rgb.x*t.x,rgb.y*t.y,rgb.z*t.z};}
                } else if(layer==2){auto icon=itemIcons.icon(p.itemId,p.metadata);if(icon.layers.empty())continue;
                    const auto& q=icon.layers.front();u0=q.u0;v0=q.v0;u1=q.u1;v1=q.v1;
                }
                if((layer==1||layer==2)&&p.jitterX>=0){float du=(u1-u0)/4,dv=(v1-v0)/4;u0+=p.jitterX*du;v0+=p.jitterY*dv;u1=u0+du;v1=v0+dv;}
                const Vec3 center{float(p.x-originX),float(p.y-originY),float(p.z-originZ)},right=(layer==4?Vec3{1,0,0}:basis.right)*p.size,up=(layer==4?Vec3{0,0,1}:basis.up)*p.size;
                const Vec3 points[]{center-right+up,center+right+up,center+right-up,center-right-up};
                const float uvs[4][2]={{u0,v0},{u1,v0},{u1,v1},{u0,v1}};
                const auto light=p.fullBright?std::uint8_t(255):visual::combinedLight(world,floorBlock(p.x),floorBlock(p.y),floorBlock(p.z));
                for(int i:{0,1,2,0,2,3})entityVertices.push_back({points[i].x,points[i].y,points[i].z,uvs[i][0],uvs[i][1],{std::uint8_t(std::clamp(rgb.x,0.f,1.f)*255),std::uint8_t(std::clamp(rgb.y,0.f,1.f)*255),std::uint8_t(std::clamp(rgb.z,0.f,1.f)*255),std::uint8_t(std::clamp(p.a,0.f,1.f)*255)},{std::uint8_t((light>>4)*17),std::uint8_t((light&15)*17),0,0}});
            }
            const int end=int(entityVertices.size());
            if(end>first)entityDraws.push_back({first,end-first,texture,1,false,true});
        }
    }
    void uploadEntities(GLuint targetBuffer = 0) {
        // CPU-built meshes share one VBO. Adjacent ranges with identical GL
        // state can be drawn together without reordering transparent surfaces.
        std::size_t count = 0;
        for (const auto &draw : entityDraws) {
            if (count) {
                auto &previous = entityDraws[count - 1];
                if (previous.first + previous.count == draw.first && previous.texture == draw.texture &&
                    previous.alpha == draw.alpha && previous.cull == draw.cull &&
                    previous.noDepthWrite == draw.noDepthWrite && previous.glint==draw.glint && previous.hurt==draw.hurt) {
                    previous.count += draw.count;
                    continue;
                }
            }
            entityDraws[count++] = draw;
        }
        entityDraws.resize(count);
        if (!entityVertices.empty()) {
            gl::BindBuffer(GL_ARRAY_BUFFER, targetBuffer ? targetBuffer : entityVBO);
            gl::BufferData(GL_ARRAY_BUFFER, std::ptrdiff_t(entityVertices.size() * sizeof(Vertex)),
                           entityVertices.data(), GL_STREAM_DRAW);
        }
    }
    void drawEntities(Program &program, bool shadowPass = false, GLuint targetVAO = 0) {
        program.vec3("uOffset", {});
        if (!shadowPass)
            program.integer("uTranslucent", 2);
        if (shadowPass)
            glDisable(GL_BLEND);
        else {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        }
        glDepthMask(GL_TRUE);
        glEnable(GL_CULL_FACE);
        bool culling = true, depthWrite = true;
        GLuint texture = 0;
        float alpha = -1;
        gl::BindVertexArray(targetVAO ? targetVAO : entityVAO);
        for (const auto &draw : entityDraws) {
            if(!shadowPass){program.integer("uGlint",draw.glint?1:0);program.integer("uHurt",draw.hurt?1:0);glDepthFunc(draw.glint?GL_EQUAL:GL_LEQUAL);
                glBlendFunc(draw.glint?GL_SRC_COLOR:GL_SRC_ALPHA,draw.glint?GL_ONE:GL_ONE_MINUS_SRC_ALPHA);}
            if (shadowPass && (draw.alpha < .999f || draw.noDepthWrite))
                continue;
            if (alpha != draw.alpha) {
                program.scalar("uAlpha", draw.alpha);
                alpha = draw.alpha;
            }
            if (culling != draw.cull) {
                if (draw.cull)
                    glEnable(GL_CULL_FACE);
                else
                    glDisable(GL_CULL_FACE);
                culling = draw.cull;
            }
            const bool nextDepthWrite = shadowPass || !draw.noDepthWrite;
            if (depthWrite != nextDepthWrite) {
                glDepthMask(nextDepthWrite ? GL_TRUE : GL_FALSE);
                depthWrite = nextDepthWrite;
            }
            if (texture != draw.texture) {
                glBindTexture(GL_TEXTURE_2D, draw.texture);
                texture = draw.texture;
            }
            glDrawArrays(GL_TRIANGLES, draw.first, draw.count);
        }
        program.scalar("uAlpha", 1);
        if(!shadowPass){program.integer("uGlint",0);program.integer("uHurt",0);}
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glEnable(GL_CULL_FACE);
        glBindTexture(GL_TEXTURE_2D, atlas);
    }
    void prepareTiles(const World &world, const std::vector<RenderTile> &tiles, std::int64_t worldTime, float partial,
                      SkinCache *skins) {
        auto key = [](int x, int y, int z) {
            return (std::uint64_t(std::uint32_t(x) & 0x3ffffff) << 38) |
                   (std::uint64_t(std::uint32_t(y) & 4095) << 26) | (std::uint32_t(z) & 0x3ffffff);
        };
        std::unordered_map<std::uint64_t, const RenderTile *> data;
        for (const auto &t : tiles)
            data[key(t.x, t.y, t.z)] = &t;
        for (const auto &entry : meshes)
            for (const auto &placement : entry.second.tiles) {
                if (std::hypot(placement.x - lastCamera.x, placement.z - lastCamera.z) > 80)
                    continue;
                TileEntityPose pose;
                pose.x = placement.x;
                pose.y = placement.y;
                pose.z = placement.z;
                pose.renderX = placement.x - originX;
                pose.renderY = placement.y - originY;
                pose.renderZ = placement.z - originZ;
                pose.worldTime = worldTime;
                pose.partialTicks = partial;
                auto found = data.find(key(pose.x, pose.y, pose.z));
                if (found != data.end()) {
                    pose.nbt = found->second->nbt;
                    pose.lid = found->second->lid;
                }
                auto frame = tileVisuals.build(world, pose, skins);
                auto packed = lightAt(world, pose.x, pose.y, pose.z);
                for (const auto &draw : frame.draws)
                    if (draw.mesh && draw.texture) {
                        const auto &t = *draw.texture;
                        auto texture = cachedTexture(t.key, t.revision, t.width, t.height, t.pixels, t.repeat);
                        appendMesh(*draw.mesh, draw.matrix, texture, draw.color, packed, draw.cull,
                                   &draw.textureMatrix);
                    }
            }
    }
    void drawFirstPerson(const World &world, const Camera &camera, int w, int h, float fieldOfView) {
        if (!firstPersonVisible)
            return;
        firstPose.renderTimeMillis=std::int64_t(timeSeconds*1000);
        auto frame = EntityVisuals::firstPerson(firstPersonAssets, firstPose,
            {firstPose.itemId,firstPose.metadata,firstItem.empty()?1:int(firstItem.count),&firstItem.nbt}, &tileVisuals, skinCache);
        if (!frame.supported)
            return;
        entityDraws.clear();
        entityVertices.clear();
        auto packed = lightAt(world, floorBlock(camera.x), floorBlock(camera.y), floorBlock(camera.z));
        for (const auto &draw : frame.draws)
            if (draw.mesh && draw.texture != FirstPersonTexture::MapData) {
                GLuint texture=draw.glint?runtimeTextures["glint"].id:
                    draw.texture==FirstPersonTexture::PlayerSkin?skinTexture(localSkin):firstAtlas;
                if(draw.dynamicTexture){const auto& t=*draw.dynamicTexture;texture=cachedTexture(t.key,t.revision,t.width,t.height,t.pixels,t.repeat);}
                appendMesh(*draw.mesh,draw.matrix,texture,draw.color,packed,true,&draw.textureMatrix,draw.translucent,draw.glint);
            }
        uploadEntities(firstVBO);
        glClear(GL_DEPTH_BUFFER_BIT);
        glEnable(GL_DEPTH_TEST);
        terrain.use();
        terrain.matrix("uVP", perspective(fieldOfView, float(w) / h, .05f, 32)*viewBobbing);
        terrain.vec3("uCamera", {});
        terrain.scalar("uFogEnd", 1000);
        terrain.scalar("uFogDensity",0);
        terrain.integer("uShadows", 0);
        terrain.integer("uTranslucent", 0);
        drawEntities(terrain, false, firstVAO);
    }
    void prepareSigns(const World &world, const std::vector<RenderSign> &text) {
        int textureIndex = entityModels.textureIndex("minecraft:entity/sign");
        if (textureIndex < 0)
            return;
        auto packed = [](int x, int y, int z) {
            return (std::uint64_t(std::uint32_t(x) & 0x3ffffff) << 38) |
                   (std::uint64_t(std::uint32_t(y) & 4095) << 26) | (std::uint32_t(z) & 0x3ffffff);
        };
        std::unordered_map<std::uint64_t, const RenderSign *> lines;
        for (const auto &sign : text)
            lines[packed(sign.x, sign.y, sign.z)] = &sign;
        for (const auto &entry : meshes)
            for (const auto &sign : entry.second.signs) {
                if (std::hypot(sign.x - lastCamera.x, sign.z - lastCamera.z) > 80)
                    continue;
                const bool standing = blockId(sign.state) == 63;
                int meta = blockMeta(sign.state);
                float degrees = standing    ? meta * 22.5f
                                : meta == 2 ? 180.f
                                : meta == 4 ? 90.f
                                : meta == 5 ? -90.f
                                            : 0.f;
                float angle = -degrees * .01745329252f, c = std::cos(angle), s = std::sin(angle);
                auto point = [&](float x, float y, float z) {
                    if (!standing) {
                        y -= .3125f;
                        z -= .4375f;
                    }
                    return Vec3{float(sign.x + .5 - originX) + x * c + z * s, float(sign.y + .5 - originY) + y,
                                float(sign.z + .5 - originZ) - x * s + z * c};
                };
                auto skyLight = std::uint8_t(world.getSkyLight(sign.x, sign.y, sign.z) * 17),
                     blockLight = std::uint8_t(world.getBlockLight(sign.x, sign.y, sign.z) * 17);
                auto addVertex = [&](Vec3 v, float u, float vtex, std::uint32_t color) {
                    entityVertices.push_back(
                        {v.x, v.y, v.z, u, vtex, {std::uint8_t(color>>16),std::uint8_t(color>>8),std::uint8_t(color),255}, {skyLight, blockLight, 0, 0}});
                };
                auto box = [&](float x, float y, float z, float dx, float dy, float dz, float u, float v) {
                    Vec3 corners[8] = {
                        {x, y, z},      {x + dx, y, z},      {x + dx, y + dy, z},      {x, y + dy, z},
                        {x, y, z + dz}, {x + dx, y, z + dz}, {x + dx, y + dy, z + dz}, {x, y + dy, z + dz}};
                    const int faces[6][4] = {{0, 1, 5, 4}, {3, 7, 6, 2}, {1, 0, 3, 2},
                                             {4, 5, 6, 7}, {0, 4, 7, 3}, {5, 1, 2, 6}};
                    const float uv[6][4] = {{u + dz + dx, v, u + dz + dx + dx, v + dz},
                                            {u + dz, v, u + dz + dx, v + dz},
                                            {u + dz, v + dz, u + dz + dx, v + dz + dy},
                                            {u + dz + dx + dz, v + dz, u + dz + dx + dz + dx, v + dz + dy},
                                            {u, v + dz, u + dz, v + dz + dy},
                                            {u + dz + dx, v + dz, u + dz + dx + dz, v + dz + dy}};
                    for (int face = 0; face < 6; ++face) {
                        Vertex vv[4];
                        for (int k = 0; k < 4; ++k) {
                            auto a = corners[faces[face][k]];
                            auto p = point(a.x / 24, -a.y / 24, -a.z / 24);
                            float tu = (k == 0 || k == 3 ? uv[face][2] : uv[face][0]) / 64.f,
                                  tv = (k < 2 ? uv[face][1] : uv[face][3]) / 32.f;
                            vv[k] = {p.x, p.y, p.z, tu, tv, {230, 230, 230, 255}, {skyLight, blockLight, 0, 0}};
                        }
                        for (int k : {0, 1, 2, 0, 2, 3})
                            entityVertices.push_back(vv[k]);
                    }
                };
                int first = int(entityVertices.size());
                box(-12, -14, -1, 24, 12, 2, 0, 0);
                if (standing)
                    box(-1, -2, -1, 2, 14, 2, 0, 14);
                entityDraws.push_back({first, int(entityVertices.size()) - first, entityTextures[textureIndex]});
                auto found = lines.find(packed(sign.x, sign.y, sign.z));
                if (found == lines.end())
                    continue;
                for (int row = 0; row < 4; ++row) {
                    const auto line=visual::signLine(found->second->lines[row],[&](char32_t cp){return std::max(0,originalFont.charWidth(std::uint16_t(cp),forceUnicode));});
                    const float gy=float(row*10-20),factor=.010416667f;
                    auto quad=[&](float left,float top,float right,float bottom,float shear,std::uint32_t color,GLuint texture,float u0,float v0,float u1,float v1){
                        const Vec3 corners[]={point((left+shear)*factor,.33333334f-top*factor,.047f),point((right+shear)*factor,.33333334f-top*factor,.047f),point((right-shear)*factor,.33333334f-bottom*factor,.047f),point((left-shear)*factor,.33333334f-bottom*factor,.047f)};
                        const float uv[4][2]={{u0,v0},{u1,v0},{u1,v1},{u0,v1}};const int begin=int(entityVertices.size());
                        for(int k:{0,2,1,0,3,2})addVertex(corners[k],uv[k][0],uv[k][1],color);
                        entityDraws.push_back({begin,6,texture});
                    };
                    for(std::size_t i=0;i<line.glyphs.size();++i){
                        const auto& character=line.glyphs[i];auto cp=character.codepoint;const auto& style=character.format;
                        if(style.obfuscated&&cp!=' '){const int width=originalFont.charWidth(std::uint16_t(cp),forceUnicode);unsigned hash=unsigned(lastAnimationTick)*1664525u+unsigned(i)*1013904223u;
                            for(int n=0;n<94;++n){const auto replacement=std::uint16_t(33+(hash+unsigned(n))%94);if(originalFont.charWidth(replacement,forceUnicode)==width){cp=replacement;break;}}}
                        const float px=character.x-line.width*.5f;const auto glyph=originalFont.glyph(std::uint16_t(cp),forceUnicode);
                        if(glyph.page>=-1)for(int pass=0;pass<(style.bold?2:1);++pass){const float offset=pass*(forceUnicode?.5f:1.f);
                            quad(px+offset,gy,px+offset+glyph.width,gy+glyph.height,style.italic?1.f:0.f,style.color,fontPage(glyph.page),glyph.u0,glyph.v0,glyph.u1,glyph.v1);}
                        if(style.underline)quad(px-1,gy+8,px+character.advance,gy+9,0,style.color,white,0,0,1,1);
                        if(style.strike)quad(px,gy+3,px+character.advance,gy+4,0,style.color,white,0,0,1,1);
                    }
                }
            }
    }
    struct Glyph {
        unsigned slot;
        float advance;
    };
    std::unordered_map<char32_t, Glyph> glyphs;
    GLuint unicodeTexture = 0;
    HDC glyphDC = nullptr;
    HBITMAP glyphBitmap = nullptr;
    HFONT glyphFont = nullptr;
    HGDIOBJ oldFont = nullptr, oldBitmap = nullptr;
    void *glyphPixels = nullptr;
    Glyph glyph(char32_t c) {
        auto found = glyphs.find(c);
        if (found != glyphs.end())
            return found->second;
        if (glyphs.size() >= 4096)
            return {0, 8};
        wchar_t chars[2];
        int count = 1;
        if (c > 0xffff) {
            chars[0] = wchar_t(0xd800 + ((c - 0x10000) >> 10));
            chars[1] = wchar_t(0xdc00 + ((c - 0x10000) & 1023));
            count = 2;
        } else
            chars[0] = wchar_t(c);
        std::memset(glyphPixels, 0, 32 * 32 * 4);
        TextOutW(glyphDC, 0, 0, chars, count);
        GdiFlush();
        SIZE size{};
        GetTextExtentPoint32W(glyphDC, chars, count, &size);
        std::array<std::uint8_t, 32 * 32 * 4> pixels{};
        auto *source = static_cast<std::uint8_t *>(glyphPixels);
        for (int i = 0; i < 32 * 32; ++i) {
            pixels[i * 4] = pixels[i * 4 + 1] = pixels[i * 4 + 2] = 255;
            pixels[i * 4 + 3] = std::max({source[i * 4], source[i * 4 + 1], source[i * 4 + 2]});
        }
        unsigned slot = unsigned(glyphs.size());
        glBindTexture(GL_TEXTURE_2D, unicodeTexture);
        glTexSubImage2D(GL_TEXTURE_2D, 0, (slot % 64) * 32, (slot / 64) * 32, 32, 32, GL_RGBA, GL_UNSIGNED_BYTE,
                        pixels.data());
        Glyph g{slot, std::min(32, static_cast<int>(size.cx)) / 3.f};
        glyphs.emplace(c, g);
        return g;
    }
    void destroy() {
        workers.reset();
#ifdef MC_RENDER_PROFILE
        if (profileSamples) {
            std::cerr << "Render CPU profile samples=" << profileSamples;
            const char* names[] = {"entities", "signs", "tiles", "upload"};
            for (int i = 0; i < 4; ++i) std::cerr << ' ' << names[i] << "_ms=" << profileTotals[i] / profileSamples;
            std::cerr << '\n';
        }
#endif
        for (auto &t : extraTextures)
            if (t.second.id)
                glDeleteTextures(1, &t.second.id);
        if (firstAtlas)
            glDeleteTextures(1, &firstAtlas);
        if (xpTexture)
            glDeleteTextures(1, &xpTexture);
        for (auto &pair : meshes)
            pair.second.destroy();
        for (auto &t : runtimeTextures)
            if (t.second.id)
                glDeleteTextures(1, &t.second.id);
        for(const auto& layer:skyLayers)if(layer.texture)glDeleteTextures(1,&layer.texture);
        for (auto t : entityTextures)
            glDeleteTextures(1, &t);
        for (auto t : unicodePages)
            if (t)
                glDeleteTextures(1, &t);
        if (itemAtlas)
            glDeleteTextures(1, &itemAtlas);
        if (entityVAO)
            gl::DeleteVertexArrays(1, &entityVAO);
        if (entityVBO)
            gl::DeleteBuffers(1, &entityVBO);
        if (firstVAO) gl::DeleteVertexArrays(1, &firstVAO);
        if (firstVBO) gl::DeleteBuffers(1, &firstVBO);
        if (unicodeTexture)
            glDeleteTextures(1, &unicodeTexture);
        if (glyphDC) {
            SelectObject(glyphDC, oldFont);
            SelectObject(glyphDC, oldBitmap);
            DeleteObject(glyphFont);
            DeleteObject(glyphBitmap);
            DeleteDC(glyphDC);
        }
        if (atlas)
            glDeleteTextures(1, &atlas);
        if (font)
            glDeleteTextures(1, &font);
        if (white)
            glDeleteTextures(1, &white);
        if (lightmapTexture) glDeleteTextures(1, &lightmapTexture);
        if(labelVAO)gl::DeleteVertexArrays(1,&labelVAO);
        if(labelVBO)gl::DeleteBuffers(1,&labelVBO);
        glDeleteTextures(2, panoramaTexture.data());
        gl::DeleteFramebuffers(2, panoramaFBO.data());
        if (sceneTexture)
            glDeleteTextures(1, &sceneTexture);
        if (sceneDepth)
            gl::DeleteRenderbuffers(1, &sceneDepth);
        if (sceneFBO)
            gl::DeleteFramebuffers(1, &sceneFBO);
        if (shadowTexture)
            glDeleteTextures(1, &shadowTexture);
        if (shadowFBO)
            gl::DeleteFramebuffers(1, &shadowFBO);
        if (uiVBO)
            gl::DeleteBuffers(1, &uiVBO);
        if (lineVBO)
            gl::DeleteBuffers(1, &lineVBO);
        if (uiVAO)
            gl::DeleteVertexArrays(1, &uiVAO);
        if (lineVAO)
            gl::DeleteVertexArrays(1, &lineVAO);
        if (emptyVAO)
            gl::DeleteVertexArrays(1, &emptyVAO);
        terrain.destroy();
        shadow.destroy();
        sky.destroy();
        post.destroy();
        panorama.destroy();
        panoramaBlit.destroy();
        secretShaders.destroy();
        ui.destroy();
        line.destroy();
        label.destroy();
    }
    void resize(int w, int h) {
        if (width == w && height == h)
            return;
        width = w;
        height = h;
        if (!sceneFBO)
            gl::GenFramebuffers(1, &sceneFBO);
        if (!sceneTexture)
            glGenTextures(1, &sceneTexture);
        if (!sceneDepth)
            gl::GenRenderbuffers(1, &sceneDepth);
        glBindTexture(GL_TEXTURE_2D, sceneTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        gl::BindRenderbuffer(GL_RENDERBUFFER, sceneDepth);
        gl::RenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
        gl::BindFramebuffer(GL_FRAMEBUFFER, sceneFBO);
        gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sceneTexture, 0);
        gl::FramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, sceneDepth);
        if (gl::CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("Scene framebuffer is incomplete");
        gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    void updateMeshes(const World &world, const Camera &camera, int distance) {
        const int cameraX=floorDiv16(floorBlock(camera.x)),cameraZ=floorDiv16(floorBlock(camera.z));
        for (auto &result : workers->take()) {
            auto key = meshKey(result.cx, result.sy, result.cz);
            if (result.epoch != epoch)
                continue;
            pending.erase(key);
            if(std::abs(result.cx-cameraX)>distance+2||std::abs(result.cz-cameraZ)>distance+2)continue;
            auto chunk = world.chunk(result.cx, result.cz);
            if (!chunk || chunk->revision != result.revision)
                continue;
            auto &mesh = meshes[key];
            mesh.cx = result.cx;
            mesh.sy = result.sy;
            mesh.cz = result.cz;
            mesh.revision = result.revision;
            mesh.signs = std::move(result.signs);
            mesh.tiles = std::move(result.tiles);
            stats.meshMs = result.milliseconds;
            for (int layer = 0; layer < 3; ++layer) {
                auto &data = result.layers[layer];
                if (data.empty()) {
                    mesh.releaseLayer(layer);
                    continue;
                }
                if (!mesh.vao[layer])
                    setupTerrain(mesh.vao[layer], mesh.vbo[layer]);
                gl::BindBuffer(GL_ARRAY_BUFFER, mesh.vbo[layer]);
                gl::BufferData(GL_ARRAY_BUFFER, std::ptrdiff_t(data.size() * sizeof(Vertex)), data.data(),
                               GL_STATIC_DRAW);
                mesh.count[layer] = int(data.size());
                if(layer==2){
                    mesh.translucentCenters.clear();
                    for(std::size_t v=0;v+5<data.size();v+=6){
                        const auto& a=data[v];const auto& b=data[v+2];
                        mesh.translucentCenters.push_back({(a.x+b.x)*.5f,(a.y+b.y)*.5f,(a.z+b.z)*.5f});
                    }
                    mesh.translucentOrder.resize(data.size());
                    mesh.sortedAt={1e20f,1e20f,1e20f};
                    if(!mesh.translucentElements)gl::GenBuffers(1,&mesh.translucentElements);
                }
            }
        }
        for (auto it = meshes.begin(); it != meshes.end();) {
            const auto *chunk = world.chunk(it->second.cx, it->second.cz);
            if (!chunk || !chunk->sections[it->second.sy]||std::abs(it->second.cx-cameraX)>distance+2||std::abs(it->second.cz-cameraZ)>distance+2) {
                it->second.destroy();
                it = meshes.erase(it);
            } else
                ++it;
        }
        struct Candidate {
            int x, y, z;
            float distance;
            std::uint64_t revision;
        };
        std::vector<Candidate> candidates;
        for (const auto &entry : world.chunks()) {
            const auto &c = entry.second;
            if (std::abs(c.x - cameraX) > distance + 1 || std::abs(c.z - cameraZ) > distance + 1)
                continue;
            for (int y = 0; y < 16; ++y) {
                if (!c.sections[y])
                    continue;
                auto key = meshKey(c.x, y, c.z);
                auto m = meshes.find(key);
                if (pending.count(key) || (m != meshes.end() && m->second.revision == c.revision))
                    continue;
                float dx = float(c.x * 16 + 8 - camera.x), dz = float(c.z * 16 + 8 - camera.z),
                      dy = float(y * 16 + 8 - camera.y);
                candidates.push_back({c.x, y, c.z, dx * dx + dz * dz + dy * dy, c.revision});
            }
        }
        std::sort(candidates.begin(), candidates.end(), [](auto &a, auto &b) { return a.distance < b.distance; });
        int scheduled = 0;
        for (const auto &c : candidates) {
            if (scheduled >= 10 || pending.size() >= 12)
                break;
            Snapshot s;
            s.cx = c.x;
            s.sy = c.y;
            s.cz = c.z;
            s.revision = c.revision;
            s.epoch = epoch;
            s.ambientOcclusion=meshAmbientOcclusion;s.alternatives=meshAlternatives;
            for (int z = -2; z <= 17; ++z)
                for (int x = -2; x <= 17; ++x) {
                    int wx = c.x * 16 + x, wz = c.z * 16 + z;
                    s.biomes[(z + 2) * 20 + x + 2] = world.getBiome(wx, wz);
                    for (int y = -2; y <= 17; ++y) {
                        int wy = c.y * 16 + y, k = s.index(x, y, z);
                        s.blocks[k] = world.getBlock(wx, wy, wz);
                        s.light[k] = visual::combinedLight(world, wx, wy, wz);
                    }
                }
            pending[meshKey(c.x, c.y, c.z)] = c.revision;
            workers->submit(std::move(s));
            ++scheduled;
        }
        stats.pendingMeshes = pending.size() + candidates.size() - scheduled;
        stats.loadedSections = meshes.size();
    }
    void addQuad(GLuint tex, float x, float y, float w, float h, float u0, float v0, float u1, float v1,
                 std::uint32_t color) {
        if (uiDraws.empty() || uiDraws.back().texture != tex || uiDraws.back().glint)
            uiDraws.push_back({tex, {}});
        auto &data = uiDraws.back().vertices;
        std::uint8_t r = color >> 24, g = color >> 16, b = color >> 8, a = color;
        UIVertex v[4] = {{x, y, u0, v0, {r, g, b, a}},
                         {x + w, y, u1, v0, {r, g, b, a}},
                         {x + w, y + h, u1, v1, {r, g, b, a}},
                         {x, y + h, u0, v1, {r, g, b, a}}};
        for (int i : {0, 1, 2, 0, 2, 3})
            data.push_back(v[i]);
    }
    void addUiMesh(const FirstPersonMesh& mesh,const FirstPersonMatrix& m,GLuint texture,std::uint32_t rgba=0xffffffffu) {
        struct Triangle{float depth=0;std::array<UIVertex,3> v;};
        std::vector<Triangle> triangles;triangles.reserve(mesh.vertices.size()/3);
        for(std::size_t i=0;i+2<mesh.vertices.size();i+=3){
            Triangle t;
            for(int j=0;j<3;++j){const auto& v=mesh.vertices[i+j];
                const float xx=m[0]*v.x+m[4]*v.y+m[8]*v.z+m[12], yy=m[1]*v.x+m[5]*v.y+m[9]*v.z+m[13],zz=m[2]*v.x+m[6]*v.y+m[10]*v.z+m[14];
                t.depth+=zz;
                const float shade=std::clamp(.65f+v.ny*.25f+v.nz*.1f,.4f,1.f);
                t.v[j]={xx,yy,v.u,v.v,{std::uint8_t((rgba>>24)*shade),std::uint8_t((rgba>>16&255)*shade),std::uint8_t((rgba>>8&255)*shade),std::uint8_t(rgba)}};
            }
            triangles.push_back(t);
        }
        std::stable_sort(triangles.begin(),triangles.end(),[](const auto& a,const auto& b){return a.depth<b.depth;});
        if(uiDraws.empty()||uiDraws.back().texture!=texture||uiDraws.back().glint)uiDraws.push_back({texture,{}});
        for(const auto& t:triangles)uiDraws.back().vertices.insert(uiDraws.back().vertices.end(),t.v.begin(),t.v.end());
    }
};
Renderer::Renderer() : impl_(std::make_unique<Impl>()) {}
void Renderer::swap(Renderer& other) noexcept {impl_.swap(other.impl_);std::swap(options,other.options);}
Renderer::~Renderer() {
    if (impl_->assets)
        impl_->destroy();
}
bool Renderer::initialize(const Assets &assets, std::string &error, const std::string &assetDirectory) {
    try {
        if (impl_->assets) { impl_->destroy(); impl_ = std::make_unique<Impl>(); }
        gl::load();
        auto &p = *impl_;
        p.assets = &assets;
        p.terrain.build(terrainVS, terrainFS);
        p.shadow.build(shadowVS, shadowFS);
        p.sky.build(screenVS, skyFS);
        p.post.build(screenVS, postFS);
        p.panorama.build(panoramaVS, panoramaFS);
        p.panoramaBlit.build(screenVS, panoramaBlitFS);
        p.secretShaders.directory(assetDirectory);
        p.ui.build(uiVS, uiFS);
        p.line.build(lineVS, lineFS);
        p.label.build(terrainVS,unlitFS);
        setupTerrain(p.labelVAO,p.labelVBO);
        p.atlas = makeTexture(assets.atlasWidth(), assets.atlasHeight(), assets.atlasPixels().data(), true);
        p.font = makeTexture(assets.fontWidth(), assets.fontHeight(), assets.fontPixels().data());
        std::uint8_t pixel[] = {255, 255, 255, 255};
        p.white = makeTexture(1, 1, pixel);
        p.previousLightmap = visual::lightmap(1,0);
        p.lightmapTexture = makeTexture(16,16,p.previousLightmap.data());
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        gl::GenFramebuffers(2,p.panoramaFBO.data());
        for(int i=0;i<2;++i){
            p.panoramaTexture[i]=makeTexture(256,256,nullptr);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
            gl::BindFramebuffer(GL_FRAMEBUFFER,p.panoramaFBO[i]);
            gl::FramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,p.panoramaTexture[i],0);
        }
        gl::BindFramebuffer(GL_FRAMEBUFFER,0);
        for (const char *name :
             {"icons",      "widgets",    "inventory",  "minecraft",  "mojang",     "options_background",
              "panorama_0", "panorama_1", "panorama_2", "panorama_3", "panorama_4", "panorama_5",
              "crafting",   "chest",      "furnace",    "dispenser",  "hopper",     "anvil",
              "brewing",    "enchanting", "beacon",     "horse",      "villager",
              "sun", "moon_phases", "end_sky", "clouds", "resource_packs", "unknown_pack", "particles", "glint", "explosion", "server_selection", "unknown_server",
              "destroy_stage_0","destroy_stage_1","destroy_stage_2","destroy_stage_3","destroy_stage_4",
              "destroy_stage_5","destroy_stage_6","destroy_stage_7","destroy_stage_8","destroy_stage_9"})
            p.runtimeTextures[name] = readRuntimeTexture(assetDirectory + "/runtime/gui/" + name + ".rgba");
        std::vector<std::pair<std::string,std::string>> originals{{"sun","environment/sun.png"},{"moon_phases","environment/moon_phases.png"},{"end_sky","environment/end_sky.png"},{"clouds","environment/clouds.png"},{"resource_packs","gui/resource_packs.png"},{"unknown_pack","misc/unknown_pack.png"},{"particles","particle/particles.png"},{"glint","misc/enchanted_item_glint.png"},{"explosion","entity/explosion.png"},{"footprint","particle/footprint.png"},{"server_selection","gui/server_selection.png"},{"unknown_server","misc/unknown_server.png"},{"underwater","misc/underwater.png"}};
        for(int i=0;i<10;++i)originals.emplace_back("destroy_stage_"+std::to_string(i),"blocks/destroy_stage_"+std::to_string(i)+".png");
        for(const auto& resource:originals)if(!p.runtimeTextures[resource.first].id){
            ResourceImage image;std::string why;
            if(readResourceImage(assetDirectory,"assets/minecraft/textures/"+resource.second,image,why))
                p.runtimeTextures[resource.first]={makeTexture(image.width,image.height,image.pixels.data()),image.width,image.height};
        }
        p.assetDirectory = assetDirectory;
        std::ifstream skyIndex(assetDirectory+"/runtime/sky/index.txt");std::string skyLine;
        while(std::getline(skyIndex,skyLine)&&p.skyLayers.size()<4){std::istringstream values(skyLine);std::string name;Impl::SkyLayer layer;if(!(values>>name>>layer.in0>>layer.in1>>layer.out0>>layer.out1))continue;auto image=readRuntimeTexture(assetDirectory+"/runtime/sky/"+name+".rgba");if(image.id)layer.texture=image.id,p.skyLayers.push_back(layer);}
        p.loadEffectSprites();
        if(p.runtimeTextures["underwater"].id){glBindTexture(GL_TEXTURE_2D,p.runtimeTextures["underwater"].id);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);}
        if(p.runtimeTextures["clouds"].id){glBindTexture(GL_TEXTURE_2D,p.runtimeTextures["clouds"].id);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);}
        if (!p.originalFont.load(assetDirectory, error))
            throw std::runtime_error(error);
        if (!p.itemIcons.load(assetDirectory, error))
            throw std::runtime_error(error);
        p.itemAtlas =
            makeTexture(p.itemIcons.atlasWidth(), p.itemIcons.atlasHeight(), p.itemIcons.atlasPixels().data());
        if (!p.atlasAnimation.load(assetDirectory, error))
            throw std::runtime_error(error);
        std::string optionalAnimationError;
        p.itemAnimation.load(assetDirectory, optionalAnimationError, "items");
        if (!p.entityModels.load(assetDirectory, error))
            throw std::runtime_error(error);
        for (const auto &texture : p.entityModels.textures())
            p.entityTextures.push_back(makeTexture(texture.width, texture.height, texture.pixels.data()));
        setupTerrain(p.entityVAO, p.entityVBO);
        setupTerrain(p.firstVAO, p.firstVBO);
        if (!p.firstPersonAssets.load(assetDirectory, error) || !p.tileVisuals.load(assetDirectory, error))
            throw std::runtime_error(error);
        p.firstAtlas = makeTexture(p.firstPersonAssets.atlasWidth(), p.firstPersonAssets.atlasHeight(),
                                   p.firstPersonAssets.atlasPixels().data());
        p.xpTexture = readRuntimeTexture(assetDirectory + "/" + ItemEntityVisuals::orbTexturePath()).id;
        if (!p.xpTexture)
            throw std::runtime_error("Experience-orb texture is missing");
        if (!p.firstAnimation.load(assetDirectory, error, "first_person"))
            throw std::runtime_error(error);
        std::vector<std::uint8_t> blank(2048 * 2048 * 4);
        p.unicodeTexture = makeTexture(2048, 2048, blank.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        p.glyphDC = CreateCompatibleDC(nullptr);
        BITMAPINFO bitmap{};
        bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bitmap.bmiHeader.biWidth = 32;
        bitmap.bmiHeader.biHeight = -32;
        bitmap.bmiHeader.biPlanes = 1;
        bitmap.bmiHeader.biBitCount = 32;
        bitmap.bmiHeader.biCompression = BI_RGB;
        p.glyphBitmap = CreateDIBSection(p.glyphDC, &bitmap, DIB_RGB_COLORS, &p.glyphPixels, nullptr, 0);
        p.glyphFont = CreateFontW(-24, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                  CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
        if (!p.glyphDC || !p.glyphBitmap || !p.glyphFont)
            throw std::runtime_error("Cannot initialize Unicode font");
        p.oldBitmap = SelectObject(p.glyphDC, p.glyphBitmap);
        p.oldFont = SelectObject(p.glyphDC, p.glyphFont);
        SetBkColor(p.glyphDC, RGB(0, 0, 0));
        SetTextColor(p.glyphDC, RGB(255, 255, 255));
        gl::GenVertexArrays(1, &p.emptyVAO);
        gl::GenVertexArrays(1, &p.uiVAO);
        gl::GenBuffers(1, &p.uiVBO);
        gl::BindVertexArray(p.uiVAO);
        gl::BindBuffer(GL_ARRAY_BUFFER, p.uiVBO);
        gl::EnableVertexAttribArray(0);
        gl::VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(UIVertex), nullptr);
        gl::EnableVertexAttribArray(1);
        gl::VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(UIVertex), reinterpret_cast<void *>(8));
        gl::EnableVertexAttribArray(2);
        gl::VertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(UIVertex), reinterpret_cast<void *>(16));
        gl::GenVertexArrays(1, &p.lineVAO);
        gl::GenBuffers(1, &p.lineVBO);
        gl::BindVertexArray(p.lineVAO);
        gl::BindBuffer(GL_ARRAY_BUFFER, p.lineVBO);
        gl::EnableVertexAttribArray(0);
        gl::VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vec3), nullptr);
        glGenTextures(1, &p.shadowTexture);
        glBindTexture(GL_TEXTURE_2D, p.shadowTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, 2048, 2048, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        float border[] = {1, 1, 1, 1};
        glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
        gl::GenFramebuffers(1, &p.shadowFBO);
        gl::BindFramebuffer(GL_FRAMEBUFFER, p.shadowFBO);
        gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, p.shadowTexture, 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        if (gl::CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("Shadow framebuffer is incomplete");
        gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
        for (int ch = 0; ch < 256; ++ch) {
            int cellW = int(assets.fontWidth() / 16), cellH = int(assets.fontHeight() / 16), right = 0;
            for (int x = 0; x < cellW; ++x)
                for (int y = 0; y < cellH; ++y)
                    if (assets.fontPixels()[((ch / 16 * cellH + y) * assets.fontWidth() + ch % 16 * cellW + x) * 4 +
                                            3] > 16)
                        right = std::max(right, x + 1);
            p.glyphWidth[ch] = right ? right + 1 : 4;
        }
        p.glyphWidth[32] = 4;
        const char *gpu = reinterpret_cast<const char *>(glGetString(GL_RENDERER));
        const char *version = reinterpret_cast<const char *>(glGetString(GL_VERSION));
        p.stats.gpu = std::string(gpu ? gpu : "unknown") + " / " + (version ? version : "");
        p.workers = std::make_unique<MeshWorkers>(assets);
        return true;
    } catch (const std::exception &ex) {
        error = ex.what();
        return false;
    }
}
void Renderer::resetWorld() {
    auto &p = *impl_;
    ++p.epoch;
    p.workers->clear();
    p.pending.clear();
    for (auto &pair : p.meshes)
        pair.second.destroy();
    p.meshes.clear();
    for (auto &entry : p.extraTextures)
        if (entry.second.id)
            glDeleteTextures(1, &entry.second.id);
    p.extraTextures.clear();
}
void Renderer::menuBackground(int w, int h, double seconds) {
    auto &p = *impl_;
    gl::BindFramebuffer(GL_FRAMEBUFFER, p.panoramaFBO[0]);
    glViewport(0, 0, 256, 256);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    p.panorama.use();
    p.panorama.integer("uTexture",0);
    gl::ActiveTexture(GL_TEXTURE0);
    std::array<GLuint,6> textures{};
    for (int i = 0; i < 6; ++i) {
        auto texture = p.runtimeTextures.find("panorama_" + std::to_string(i));
        textures[i]=texture==p.runtimeTextures.end()?p.white:texture->second.id;
        glBindTexture(GL_TEXTURE_2D,textures[i]);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    }
    const Vertex corners[4]{{-1,-1,1,0,0,{},{}},{1,-1,1,1,0,{},{}},
                             {1,1,1,1,1,{},{}},{-1,1,1,0,1,{},{}}};
    Vertex vertices[6];int n=0;for(int i:{0,1,2,0,2,3})vertices[n++]=corners[i];
    gl::BindVertexArray(p.labelVAO);
    gl::BindBuffer(GL_ARRAY_BUFFER,p.labelVBO);
    gl::BufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STREAM_DRAW);
    const float ticks=float(std::max(0.,seconds)*20.);
    for(int sample=0;sample<64;++sample){
        p.panorama.scalar("uAlpha",float(255/(sample+1))/255.f);
        for(int face=0;face<6;++face){
            p.panorama.matrix("uVP",visual::panoramaMatrix(ticks,sample,face));
            glBindTexture(GL_TEXTURE_2D,textures[face]);
            glDrawArrays(GL_TRIANGLES,0,6);
        }
        glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_FALSE);
    }
    gl::BindVertexArray(p.emptyVAO);
    p.panoramaBlit.use();
    p.panoramaBlit.integer("uTexture",0);
    p.panoramaBlit.integer("uFinal",0);
    for(int i=0;i<7;++i){
        gl::BindFramebuffer(GL_FRAMEBUFFER,p.panoramaFBO[(i+1)&1]);
        glBindTexture(GL_TEXTURE_2D,p.panoramaTexture[i&1]);
        // Original quads transpose texture coordinates on EVERY pass. Keep
        // the three draws and byte alpha values, including their RGBA8 rounding.
        for(int tap=0;tap<3;++tap){
            p.panoramaBlit.scalar("uOffset",float(tap-1)/256.f);
            p.panoramaBlit.scalar("uAlpha",float(int(255.f/float(tap+1)))/255.f);
            glDrawArrays(GL_TRIANGLES,0,3);
        }
    }
    glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    gl::BindFramebuffer(GL_FRAMEBUFFER,0);
    glViewport(0,0,w,h);
    // rotateAndBlurSkybox copies BEFORE drawing. After its seventh call the
    // background texture sampled by renderSkybox contains the sixth result.
    glBindTexture(GL_TEXTURE_2D,p.panoramaTexture[0]);
    p.panoramaBlit.integer("uFinal",1);
    p.panoramaBlit.scalar("uAlpha",1);
    p.panoramaBlit.vec2("uCrop",float(h)/std::max(w,h)*240.f/256.f,float(w)/std::max(w,h)*240.f/256.f);
    glDrawArrays(GL_TRIANGLES,0,3);
}
void Renderer::render(const World &world, const Camera &camera, int w, int h, double timeSeconds,
                      std::int64_t worldTime, const std::vector<RenderEntity> &entities,
                      const std::vector<RenderSign> &signs, const std::vector<RenderTile> &tiles, SkinCache *skins) {
    auto start = Clock::now();
    auto &p = *impl_;
    p.resize(w, h);
    if(p.meshAmbientOcclusion!=options.ambientOcclusion||p.meshAlternatives!=options.blockAlternatives){
        resetWorld();p.meshAmbientOcclusion=options.ambientOcclusion;p.meshAlternatives=options.blockAlternatives;
    }
    if(p.appliedMipmapLevels!=options.mipmapLevels){
        gl::ActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,p.atlas);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,options.mipmapLevels>0?GL_NEAREST_MIPMAP_LINEAR:GL_NEAREST);
        // The prepared atlas declares one safe level with its 2px gutters.
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,options.mipmapLevels>0?1:0);
        p.appliedMipmapLevels=options.mipmapLevels;
    }
    p.skinCache=skins;p.timeSeconds=timeSeconds;
    p.animate(std::uint64_t(std::max(0., timeSeconds) * 20));
    p.updateMeshes(world, camera, options.distance);
    const auto preparedMeshes = Clock::now();
    p.stats.meshUpdateMs = std::chrono::duration<double, std::milli>(preparedMeshes - start).count();
    p.lastCamera = camera;
    p.originX = std::floor(camera.x / 16) * 16;
    p.originY = std::floor(camera.y / 16) * 16;
    p.originZ = std::floor(camera.z / 16) * 16;
    Vec3 eye{float(camera.x - p.originX), float(camera.y - p.originY), float(camera.z - p.originZ)};
    const auto basis = cameraBasis(camera.yaw,camera.pitch);
    Vec3 forward = basis.forward, right = basis.right, up = basis.up;
    p.lastVP=perspective(camera.fov,float(w)/h,.05f,float(options.distance*16+40))*p.viewBobbing*cameraView(eye,basis);
    p.prepareEntities(world, entities);
    p.prepareEntityFire(entities);
#ifdef MC_RENDER_PROFILE
    const auto afterEntities = Clock::now();
#endif
    p.prepareSigns(world, signs);
#ifdef MC_RENDER_PROFILE
    const auto afterSigns = Clock::now();
#endif
    p.prepareTiles(world, tiles, worldTime, options.partialTicks, skins);
    p.prepareParticles(world);
#ifdef MC_RENDER_PROFILE
    const auto afterTiles = Clock::now();
#endif
    p.uploadEntities();
#ifdef MC_RENDER_PROFILE
    if (p.pending.empty()) {
        p.profileTotals[0] += std::chrono::duration<double, std::milli>(afterEntities - preparedMeshes).count();
        p.profileTotals[1] += std::chrono::duration<double, std::milli>(afterSigns - afterEntities).count();
        p.profileTotals[2] += std::chrono::duration<double, std::milli>(afterTiles - afterSigns).count();
        p.profileTotals[3] += std::chrono::duration<double, std::milli>(Clock::now() - afterTiles).count();
        ++p.profileSamples;
    }
#endif
    p.stats.dynamicPrepareMs = std::chrono::duration<double, std::milli>(Clock::now() - preparedMeshes).count();
    p.stats.dynamicDraws = p.entityDraws.size();
    p.stats.dynamicVertices = p.entityVertices.size();
    auto timeOfDay = worldTime % 24000;
    if (timeOfDay < 0)
        timeOfDay = -timeOfDay;
    float fogEnd = float(options.distance * 16);
    const float angle = visual::celestialAngle(timeOfDay);
    const float day = visual::sunBrightness(angle,options.rain,options.thunder);
    const float skyFactor=std::clamp(std::cos(angle*6.28318530718f)*2.f+.5f,0.f,1.f);
    const auto skyRGB=visual::skyColor(registry::biome(world.getBiome(floorBlock(camera.x),floorBlock(camera.z))).temperature);
    Vec3 skyColor{float(skyRGB>>16&255)/255.f*skyFactor,float(skyRGB>>8&255)/255.f*skyFactor,float(skyRGB&255)/255.f*skyFactor};
    Vec3 sun{ -std::sin(angle*6.28318530718f),std::cos(angle*6.28318530718f),0};
    Vec3 fog{.7529412f*(skyFactor*.94f+.06f),.84705883f*(skyFactor*.94f+.06f),skyFactor*.91f+.09f};
    const float skyMix=1.f-std::pow(.25f+.75f*float(options.distance)/32.f,.25f);
    fog=fog*(1.f-skyMix)+skyColor*skyMix;
    if(options.dimension==-1)skyColor=fog={.2f,.03f,.03f};
    if(options.dimension==1)skyColor=fog={.075f,.075f,.094f};
    const int medium=visual::viewpointMedium(world,camera.x,camera.y,camera.z);
    const auto mediumFog=visual::mediumFog(medium,options.respiration,options.waterBreathing);
    if(medium)skyColor=fog=mediumFog.color;
    auto lightmap=visual::lightmap(day,options.gamma,options.dimension,0,options.nightVision);
    gl::ActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D,p.lightmapTexture);
    if(lightmap!=p.previousLightmap){glTexSubImage2D(GL_TEXTURE_2D,0,0,0,16,16,GL_RGBA,GL_UNSIGNED_BYTE,lightmap.data());p.previousLightmap=lightmap;}
    gl::ActiveTexture(GL_TEXTURE0);
    Mat4 view = p.viewBobbing*cameraView(eye,basis),
         vp = perspective(camera.fov, float(w) / h, .05f, fogEnd + 40) * view;
    p.lastVP = vp;
    Frustum frustum(vp);
    Vec3 shadowCenter = eye + Vec3{forward.x, 0, forward.z} * 24.f;
    shadowCenter.x = std::floor(shadowCenter.x * 8) / 8;
    shadowCenter.y = std::floor(shadowCenter.y * 8) / 8;
    shadowCenter.z = std::floor(shadowCenter.z * 8) / 8;
    Mat4 shadowVP = ortho(-80, 80, -80, 80, 1, 320) * lookAt(shadowCenter + sun * 160, shadowCenter, {0, 1, 0});
    std::vector<GpuMesh *> visible;
    for (auto &pair : p.meshes) {
        auto &m = pair.second;
        Vec3 lo{float(m.cx * 16 - p.originX), float(m.sy * 16 - p.originY), float(m.cz * 16 - p.originZ)},
            hi = lo + Vec3{16, 16, 16};
        if (std::abs(lo.x - eye.x) > fogEnd + 16 || std::abs(lo.z - eye.z) > fogEnd + 16)
            continue;
        if (frustum.visible(lo, hi))
            visible.push_back(&m);
    }
    std::sort(visible.begin(), visible.end(), [&](auto *a, auto *b) {
        double ax = a->cx * 16 + 8 - camera.x, ay=a->sy*16+8-camera.y,az = a->cz * 16 + 8 - camera.z,
               bx = b->cx * 16 + 8 - camera.x,by=b->sy*16+8-camera.y,bz = b->cz * 16 + 8 - camera.z;
        return ax * ax + ay*ay+az * az < bx * bx + by*by+bz * bz;
    });
    auto draw = [&](Program &prog, GpuMesh &m, int layer) {
        if (!m.count[layer])
            return;
        prog.vec3("uOffset",
                  {float(m.cx * 16 - p.originX), float(m.sy * 16 - p.originY), float(m.cz * 16 - p.originZ)});
        gl::BindVertexArray(m.vao[layer]);
        if(layer==2){
            Vec3 local{float(camera.x-m.cx*16),float(camera.y-m.sy*16),float(camera.z-m.cz*16)};
            const Vec3 moved=local-m.sortedAt;
            if(dot(moved,moved)>1.f){
                std::vector<std::uint32_t> order(m.translucentCenters.size());
                for(std::size_t i=0;i<order.size();++i)order[i]=std::uint32_t(i);
                std::stable_sort(order.begin(),order.end(),[&](auto a,auto b){
                    auto da=m.translucentCenters[a]-local,db=m.translucentCenters[b]-local;
                    return dot(da,da)>dot(db,db);
                });
                for(std::size_t i=0;i<order.size();++i)for(int j=0;j<6;++j)m.translucentOrder[i*6+j]=order[i]*6+j;
                gl::BindBuffer(GL_ELEMENT_ARRAY_BUFFER,m.translucentElements);
                gl::BufferData(GL_ELEMENT_ARRAY_BUFFER,std::ptrdiff_t(m.translucentOrder.size()*sizeof(std::uint32_t)),m.translucentOrder.data(),GL_DYNAMIC_DRAW);
                m.sortedAt=local;
            } else gl::BindBuffer(GL_ELEMENT_ARRAY_BUFFER,m.translucentElements);
            glDrawElements(GL_TRIANGLES,m.count[layer],GL_UNSIGNED_INT,nullptr);
        } else glDrawArrays(GL_TRIANGLES, 0, m.count[layer]);
    };
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    gl::ActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, p.atlas);
    if (false && options.shadows) {
        gl::BindFramebuffer(GL_FRAMEBUFFER, p.shadowFBO);
        glViewport(0, 0, 2048, 2048);
        glClear(GL_DEPTH_BUFFER_BIT);
        p.shadow.use();
        p.shadow.matrix("uVP", shadowVP);
        p.shadow.integer("uAtlas", 0);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(1.4f, 2.f);
        Frustum sf(shadowVP);
        for (auto &pair : p.meshes) {
            auto &m = pair.second;
            Vec3 lo{float(m.cx * 16 - p.originX), float(m.sy * 16 - p.originY), float(m.cz * 16 - p.originZ)};
            if (sf.visible(lo, lo + Vec3{16, 16, 16})) {
                draw(p.shadow, m, 0);
                draw(p.shadow, m, 1);
            }
        }
        p.drawEntities(p.shadow, true);
        glDisable(GL_POLYGON_OFFSET_FILL);
    }
    gl::BindFramebuffer(GL_FRAMEBUFFER, p.sceneFBO);
    glViewport(0, 0, w, h);
    glClearColor(fog.x, fog.y, fog.z, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    p.sky.use();
    const auto& bob=p.viewBobbing.v;
    p.sky.vec3("uForward", right*(-bob[2])+up*(-bob[6])+forward*bob[10]);
    p.sky.vec3("uRight", right*bob[0]+up*bob[4]-forward*bob[8]);
    p.sky.vec3("uUp", right*bob[1]+up*bob[5]-forward*bob[9]);
    p.sky.vec3("uSky",skyColor);
    p.sky.vec3("uFog",fog);
    p.sky.scalar("uAngle",angle*6.28318530718f);
    p.sky.scalar("uRain",options.rain);
    p.sky.integer("uDimension",medium?1:options.dimension);
    p.sky.integer("uPhase",int((std::abs(worldTime)/24000)%8));
    p.sky.integer("uSunTexture",0);p.sky.integer("uMoonTexture",1);
    p.sky.integer("uEndSkyTexture",2);
    p.sky.integer("uCloudTexture",3);p.sky.integer("uCustomSky",4);p.sky.integer("uCustomOnly",0);p.sky.scalar("uCustomAlpha",0);
    glBindTexture(GL_TEXTURE_2D,p.runtimeTextures["sun"].id?p.runtimeTextures["sun"].id:p.white);
    gl::ActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D,p.runtimeTextures["moon_phases"].id?p.runtimeTextures["moon_phases"].id:p.white);
    gl::ActiveTexture(GL_TEXTURE0);
    gl::ActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D,p.runtimeTextures["end_sky"].id?p.runtimeTextures["end_sky"].id:p.white);
    gl::ActiveTexture(GL_TEXTURE0+3);
    glBindTexture(GL_TEXTURE_2D,p.runtimeTextures["clouds"].id?p.runtimeTextures["clouds"].id:p.white);
    gl::ActiveTexture(GL_TEXTURE0);
    p.sky.scalar("uTan", std::tan(camera.fov * .00872664626f));
    p.sky.scalar("uAspect", float(w) / h);
    p.sky.scalar("uDay", day);
    p.sky.scalar("uCameraY",float(camera.y));p.sky.vec2("uCameraXZ",float(std::fmod(camera.x,2048.)),float(std::fmod(camera.z,2048.)));p.sky.scalar("uTime",float(std::fmod(timeSeconds,100000.)));
    gl::BindVertexArray(p.emptyVAO);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    if(options.dimension==0&&!p.skyLayers.empty()){
        glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);p.sky.integer("uCustomOnly",1);
        for(const auto& layer:p.skyLayers){
            auto distance=[](int now,int start){return (now-start+24000)%24000;};const int tick=int(timeOfDay);
            const int fadeIn=distance(layer.in1,layer.in0),fadeOut=distance(layer.out1,layer.out0);
            float alpha=0;
            if(distance(tick,layer.in0)<fadeIn)alpha=fadeIn?float(distance(tick,layer.in0))/fadeIn:1.f;
            else if(distance(tick,layer.in1)<distance(layer.out0,layer.in1))alpha=1.f;
            else if(distance(tick,layer.out0)<fadeOut)alpha=fadeOut?1.f-float(distance(tick,layer.out0))/fadeOut:0.f;
            if(alpha<=0)continue;
            p.sky.scalar("uCustomAlpha",std::clamp(alpha,0.f,1.f));
            gl::ActiveTexture(GL_TEXTURE0+4);glBindTexture(GL_TEXTURE_2D,layer.texture);gl::ActiveTexture(GL_TEXTURE0);
            glDrawArrays(GL_TRIANGLES,0,3);
        }
        p.sky.integer("uCustomOnly",0);glDisable(GL_BLEND);
    }
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    p.terrain.use();
    p.terrain.matrix("uVP", vp);
    p.terrain.matrix("uShadow", shadowVP);
    p.terrain.vec3("uCamera", eye);
    p.terrain.vec3("uFog", fog);
    p.terrain.scalar("uFogEnd", fogEnd);
    p.terrain.scalar("uFogDensity",mediumFog.density);
    p.terrain.integer("uHurt",0);
    p.terrain.scalar("uDay", day);
    p.terrain.integer("uAtlas", 0);
    p.terrain.integer("uLightmap",2);
    p.terrain.integer("uShadowMap", 1);
    p.terrain.integer("uShadows", 0);
    p.terrain.integer("uTranslucent", 0);
    p.terrain.integer("uGlint",0);
    p.terrain.scalar("uAlpha", 1);
    gl::ActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, p.shadowTexture);
    gl::ActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, p.atlas);
    if (options.wireframe)
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    p.stats.triangles = p.entityVertices.size() / 3;
    for (auto *m : visible) {
        draw(p.terrain, *m, 0);
        draw(p.terrain, *m, 1);
        p.stats.triangles += (m->count[0] + m->count[1] + m->count[2]) / 3;
    }
    p.drawEntities(p.terrain);
    // Render.RenderLivingLabel uses actual world depth. Normal names have a
    // faint occluded pass; sneaking names only have the depth-tested pass.
    p.label.use();p.label.matrix("uVP",vp);p.label.vec3("uOffset",{});p.label.integer("uAtlas",0);
    gl::BindVertexArray(p.labelVAO);gl::BindBuffer(GL_ARRAY_BUFFER,p.labelVBO);
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);glDisable(GL_CULL_FACE);
    for(const auto& entity:entities) {
        const double dx=entity.x-camera.x,dy=entity.y-camera.y,dz=entity.z-camera.z;
        const float range=entity.sneaking?32.f:64.f;
        if(!entity.showName||entity.name.empty()||dx*dx+dy*dy+dz*dz>=range*range)continue;
        const float scale=.02666667f;
        const float offset=entity.height+.5f-(entity.child?entity.height*.5f:0.f)-(entity.sneaking?.25f:0.f);
        const Vec3 center{float(entity.x-p.originX),float(entity.y-p.originY)+offset,float(entity.z-p.originZ)};
        p.uiDraws.clear();
        const float half=std::floor(textWidth(entity.name,1)*.5f),textY=entity.name=="deadmau5"?-10.f:0;
        rect(-half-1,textY-1,half*2+2,9,0x00000040);
        text(entity.name,-half,textY,1,0xffffffff,false);
        for(int pass=0;pass<(entity.sneaking?1:2);++pass) {
            if(entity.sneaking||pass==1)glEnable(GL_DEPTH_TEST);else glDisable(GL_DEPTH_TEST);
            glDepthMask(pass==1?GL_TRUE:GL_FALSE);
            for(const auto& batch:p.uiDraws) {
                const bool background=batch.texture==p.white;
                if(pass==1&&background)continue;
                if(entity.sneaking)glDepthMask(background?GL_FALSE:GL_TRUE);
                p.label.scalar("uAlpha",background||pass==1?1.f:32.f/255.f);
                std::vector<Vertex> vertices;vertices.reserve(batch.vertices.size());
                for(const auto& v:batch.vertices) {
                    const auto pos=center+right*(v.x*scale)-up*(v.y*scale);
                    vertices.push_back({pos.x,pos.y,pos.z,v.u,v.v,{v.color[0],v.color[1],v.color[2],v.color[3]},{255,255,0,0}});
                }
                glBindTexture(GL_TEXTURE_2D,batch.texture);
                gl::BufferData(GL_ARRAY_BUFFER,std::ptrdiff_t(vertices.size()*sizeof(Vertex)),vertices.data(),GL_STREAM_DRAW);
                glDrawArrays(GL_TRIANGLES,0,GLsizei(vertices.size()));
            }
        }
    }
    p.uiDraws.clear();
    glEnable(GL_DEPTH_TEST);glDepthMask(GL_TRUE);
    p.terrain.use();glBindTexture(GL_TEXTURE_2D,p.atlas);
    p.terrain.integer("uTranslucent", 1);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glEnable(GL_CULL_FACE);
    for (auto it = visible.rbegin(); it != visible.rend(); ++it)
        draw(p.terrain, **it, 2);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    if(!p.blockBreaks.empty()) {
        p.label.use();p.label.matrix("uVP",vp);p.label.vec3("uOffset",{});p.label.integer("uAtlas",0);p.label.scalar("uAlpha",.5f);
        gl::BindVertexArray(p.labelVAO);gl::BindBuffer(GL_ARRAY_BUFFER,p.labelVBO);
        glEnable(GL_BLEND);glBlendFunc(GL_DST_COLOR,GL_SRC_COLOR);
        glEnable(GL_POLYGON_OFFSET_FILL);glPolygonOffset(-3,-3);glDepthMask(GL_FALSE);
        auto submit=[&](const std::vector<Vertex>& vertices,GLuint texture,bool cull){
            if(vertices.empty()||!texture)return;
            if(cull)glEnable(GL_CULL_FACE);else glDisable(GL_CULL_FACE);
            glBindTexture(GL_TEXTURE_2D,texture);
            gl::BufferData(GL_ARRAY_BUFFER,std::ptrdiff_t(vertices.size()*sizeof(Vertex)),vertices.data(),GL_STREAM_DRAW);
            glDrawArrays(GL_TRIANGLES,0,GLsizei(vertices.size()));
        };
        for(const auto& breaking:p.blockBreaks) {
            if(breaking.stage<0||breaking.stage>9)continue;
            const auto state=world.getBlock(breaking.x,breaking.y,breaking.z);
            if(!blockId(state))continue;
            const Vec3 base{float(breaking.x-p.originX),float(breaking.y-p.originY),float(breaking.z-p.originZ)};
            if(TileEntityVisuals::handlesBlock(blockId(state))) {
                TileEntityPose pose;pose.x=breaking.x;pose.y=breaking.y;pose.z=breaking.z;
                pose.renderX=base.x;pose.renderY=base.y;pose.renderZ=base.z;pose.destroyStage=breaking.stage;
                pose.worldTime=worldTime;pose.partialTicks=options.partialTicks;
                for(const auto& tile:tiles)if(tile.x==pose.x&&tile.y==pose.y&&tile.z==pose.z){pose.nbt=tile.nbt;pose.lid=tile.lid;break;}
                auto frame=p.tileVisuals.build(world,pose,skins);
                for(const auto& draw:frame.draws)if(draw.texture&&draw.mesh) {
                    const auto& t=*draw.texture;const auto& m=draw.matrix;const auto& u=draw.textureMatrix;
                    std::vector<Vertex> vertices;vertices.reserve(draw.mesh->vertices.size());
                    for(const auto& v:draw.mesh->vertices)vertices.push_back({m[0]*v.x+m[4]*v.y+m[8]*v.z+m[12],m[1]*v.x+m[5]*v.y+m[9]*v.z+m[13],m[2]*v.x+m[6]*v.y+m[10]*v.z+m[14],u[0]*v.u+u[4]*v.v+u[12],u[1]*v.u+u[5]*v.v+u[13],{255,255,255,255},{255,255,0,0}});
                    submit(vertices,p.cachedTexture(t.key,t.revision,t.width,t.height,t.pixels,t.repeat),draw.cull);
                }
                continue;
            }
            Snapshot snap;snap.cx=floorDiv16(breaking.x);
            snap.sy=breaking.y>>4;snap.cz=floorDiv16(breaking.z);
            const int x=breaking.x&15,y=breaking.y&15,z=breaking.z&15;
            for(int dy=-1;dy<=1;++dy)for(int dz=-1;dz<=1;++dz)for(int dx=-1;dx<=1;++dx)
                snap.blocks[snap.index(x+dx,y+dy,z+dz)]=world.getBlock(breaking.x+dx,breaking.y+dy,breaking.z+dz);
            const auto& model=actualModel(*p.assets,snap,x,y,z);
            std::vector<Vertex> vertices;
            for(const auto& q:model.quads)for(int i:{0,1,2,0,2,3}) {
                const auto& v=q.vertices[i];float u=v.x,t=1-v.y;
                if(q.face<2){u=v.x;t=q.face==0?1-v.z:v.z;}
                else if(q.face==2)u=1-v.x;
                else if(q.face==4)u=v.z;
                else if(q.face==5)u=1-v.z;
                vertices.push_back({base.x+v.x,base.y+v.y,base.z+v.z,u,t,{255,255,255,255},{255,255,0,0}});
            }
            auto texture=p.runtimeTextures["destroy_stage_"+std::to_string(breaking.stage)].id;
            submit(vertices,texture,true);
        }
        glDisable(GL_POLYGON_OFFSET_FILL);glPolygonOffset(0,0);glDepthMask(GL_TRUE);glDisable(GL_BLEND);
    }
    p.drawSelection();
    p.drawFishingLines(entities,options.thirdPerson);
    p.drawFirstPerson(world, camera, w, h, options.firstPersonFov);
    p.drawEnvironmentOverlay(world,camera,w,h,options,day);
    p.secretShaders.apply(options.secretShader,p.sceneFBO,p.sceneTexture,w,h,options.partialTicks);
    p.stats.secretShaderError=p.secretShaders.error;p.stats.secretShaderPasses=p.secretShaders.passCount();
    gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, w, h);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    p.post.use();
    p.post.integer("uScene", 0);
    p.post.vec2("uPixel", 1.f / w, 1.f / h);
    p.post.scalar("uExposure", options.exposure);
    p.post.integer("uFXAA", options.fxaa);
    glBindTexture(GL_TEXTURE_2D, p.sceneTexture);
    gl::BindVertexArray(p.emptyVAO);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    p.stats.visibleSections = visible.size();
    p.stats.frameMs = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
void Renderer::selection(int x, int y, int z) {
    selection(double(x), double(y), double(z), double(x + 1), double(y + 1), double(z + 1));
}
void Renderer::selection(double x, double y, double z, double maxX, double maxY, double maxZ) {
    impl_->hasSelection = true;
    impl_->selectionBox = {x, y, z, maxX, maxY, maxZ};
}
void Renderer::clearSelection() { impl_->hasSelection = false; }
void Renderer::setFirstPerson(const FirstPersonPose &pose, std::shared_ptr<const PlayerSkin> skin, const ItemStack* item) {
    impl_->firstPose = pose;
    impl_->firstItem = item ? *item : ItemStack{};
    impl_->localSkin = std::move(skin);
    impl_->firstPersonVisible = true;
}
void Renderer::hideFirstPerson() { impl_->firstPersonVisible = false; }
void Renderer::setBlockBreaks(std::vector<RenderBreak> value){impl_->blockBreaks=std::move(value);}
void Renderer::setParticles(std::vector<ParticleSprite> value){impl_->particles=std::move(value);}
void Renderer::setViewBobbing(const gfx::Mat4& value){impl_->viewBobbing=value;}
void Renderer::beginUI(int w, int h) {
    impl_->uiWidth = w;
    impl_->uiHeight = h;
    impl_->uiDraws.clear();
}
void Renderer::setUnicode(bool enabled) { impl_->forceUnicode = enabled; }
void Renderer::rect(float x, float y, float w, float h, std::uint32_t color) {
    impl_->addQuad(impl_->white, x, y, w, h, 0, 0, 1, 1, color);
}
void Renderer::gradient(float x, float y, float w, float h, std::uint32_t top, std::uint32_t bottom) {
    auto &p = *impl_;
    p.addQuad(p.white, x, y, w, h, 0, 0, 1, 1, top);
    auto &v = p.uiDraws.back().vertices;
    std::size_t start = v.size() - 6;
    for (int i : {2, 4, 5}) {
        v[start + i].color[0] = bottom >> 24;
        v[start + i].color[1] = bottom >> 16;
        v[start + i].color[2] = bottom >> 8;
        v[start + i].color[3] = bottom;
    }
}
float Renderer::textWidth(const std::string &text, float scale) const {
    float width = 0, maxWidth = 0;
    bool bold = false;
    auto chars = decodeText(text);
    for (std::size_t i = 0; i < chars.size(); ++i) {
        auto c = chars[i];
        if (c == 0xa7 && i + 1 < chars.size()) {
            auto code = chars[++i];
            if (code == 'l')
                bold = true;
            else if (code == 'r' || (code >= '0' && code <= '9') || (code >= 'a' && code <= 'f'))
                bold = false;
            continue;
        }
        if (c == '\n') {
            maxWidth = std::max(maxWidth, width);
            width = 0;
            continue;
        }
        int advance = c <= 0xffff ? impl_->originalFont.charWidth(std::uint16_t(c), impl_->forceUnicode) : 8;
        width += (std::max(0, advance) + (bold ? 1 : 0)) * scale;
    }
    return std::max(maxWidth, width);
}
void Renderer::text(const std::string &value, float x, float y, float scale, std::uint32_t rgba, bool shadow) {
    auto &p = *impl_;
    if (shadow) {
        p.renderingTextShadow=true;
        text(value, x + scale, y + scale, scale, rgba, false);
        p.renderingTextShadow=false;
    }
    float start = x;
    auto chars = decodeText(value);
    const std::uint32_t colors[] = {0x000000, 0x0000aa, 0x00aa00, 0x00aaaa, 0xaa0000, 0xaa00aa, 0xffaa00, 0xaaaaaa,
                                    0x555555, 0x5555ff, 0x55ff55, 0x55ffff, 0xff5555, 0xff55ff, 0xffff55, 0xffffff};
    const bool isShadow=p.renderingTextShadow;
    const auto dim=[](std::uint32_t color){return ((color&0xfcfcfc00u)>>2)|(color&255u);};
    bool bold = false, italic = false, underline = false, strike = false, obfuscated=false;
    std::uint32_t color = isShadow?dim(rgba):rgba;
    for (std::size_t i = 0; i < chars.size(); ++i) {
        auto c = chars[i];
        if (c == 0xa7 && i + 1 < chars.size()) {
            auto code = chars[++i];
            if(code>='A'&&code<='Z')code+=32;
            int index = code >= '0' && code <= '9'   ? int(code - '0')
                        : code >= 'a' && code <= 'f' ? int(code - 'a' + 10)
                                                     : -1;
            if (index >= 0) {
                color = (colors[index] << 8) | (rgba & 255);
                if(isShadow)color=dim(color);
                bold = italic = underline = strike = obfuscated = false;
            } else if (code == 'r') {
                color = isShadow?dim(rgba):rgba;
                bold = italic = underline = strike = obfuscated = false;
            } else if (code == 'l')
                bold = true;
            else if (code == 'o')
                italic = true;
            else if (code == 'n')
                underline = true;
            else if (code == 'm')
                strike = true;
            else if(code=='k')obfuscated=true;
            continue;
        }
        if (c == '\n') {
            x = start;
            y += 9 * scale;
            continue;
        }
        float advance = 8;
        if (c <= 0xffff) {
            if(obfuscated && c!=' ') {
                const auto width=p.originalFont.charWidth(std::uint16_t(c),p.forceUnicode);
                unsigned hash=unsigned(p.lastAnimationTick)*1664525u+unsigned(i)*1013904223u;
                for(int n=0;n<94;++n){auto replacement=std::uint16_t(33+(hash+unsigned(n))%94);
                    if(p.originalFont.charWidth(replacement,p.forceUnicode)==width){c=replacement;break;}}
            }
            auto g = p.originalFont.glyph(std::uint16_t(c), p.forceUnicode);
            advance = float(int(g.advance));
            if (g.page >= -1) {
                GLuint texture = p.fontPage(g.page);
                for (int pass = 0; pass < (bold ? 2 : 1); ++pass) {
                    const float offset=(p.forceUnicode?.5f:1.f)*scale;
                    const float shadowOffset=isShadow && (g.page>=0||p.forceUnicode)?offset:0;
                    p.addQuad(texture, x + pass * offset-shadowOffset, y-shadowOffset, g.width * scale, g.height * scale, g.u0, g.v0, g.u1, g.v1,
                              color);
                    if (italic) {
                        auto &vertices = p.uiDraws.back().vertices;
                        auto first = vertices.size() - 6;
                        for (int k : {0, 1, 3})
                            vertices[first + k].x += scale;
                        for (int k : {2, 4, 5})
                            vertices[first + k].x -= scale;
                    }
                }
            }
        } else {
            auto g = p.glyph(c);
            float u = (g.slot % 64) / 64.f, v = (g.slot / 64) / 64.f;
            p.addQuad(p.unicodeTexture, x, y, 32.f / 3 * scale, 32.f / 3 * scale, u, v, u + 1 / 64.f, v + 1 / 64.f,
                      color);
            advance = g.advance;
        }
        if (bold)
            advance += 1;
        if (underline)
            rect(x-scale, y + 8 * scale, (advance+1) * scale, scale, color);
        if (strike)
            rect(x, y + 3 * scale, advance * scale, scale, color);
        x += advance * scale;
    }
}
void Renderer::blockIcon(std::uint16_t state, float x, float y, float size) {
    const auto &model = impl_->assets->model(state);
    if (model.quads.empty())
        return;
    const ModelQuad *q = &model.quads.front();
    for (const auto &face : model.quads)
        if (face.face == 3) {
            q = &face;
            break;
        }
    float u0 = 1, v0 = 1, u1 = 0, v1 = 0;
    for (auto &v : q->vertices) {
        u0 = std::min(u0, v.u);
        v0 = std::min(v0, v.v);
        u1 = std::max(u1, v.u);
        v1 = std::max(v1, v.v);
    }
    std::uint32_t color = q->tintIndex >= 0 ? 0x91bb62ff : 0xffffffff;
    impl_->addQuad(impl_->atlas, x, y, size, size, u0, v0, u1, v1, color);
}
void Renderer::sprite(const std::string &name, float x, float y, float w, float h, float sx, float sy, float sw,
                      float sh, std::uint32_t color) {
    auto found = impl_->runtimeTextures.find(name);
    if (found == impl_->runtimeTextures.end() || !found->second.id)
        return;
    const auto &t = found->second;
    static const std::unordered_set<std::string> original256{"icons","widgets","hud_icons","hud_widgets","inventory","minecraft","mojang","crafting","chest","furnace","dispenser","hopper","anvil","brewing","enchanting","beacon","horse","villager","book","server_selection","creative_tabs","creative_tab_inventory","creative_tab_item_search","creative_tab_items"};
    const float tw=name=="options_background"?16.f:original256.count(name)?256.f:float(t.width);
    const float th=name=="options_background"?16.f:original256.count(name)?256.f:float(t.height);
    impl_->addQuad(t.id, x, y, w, h, sx / tw, sy / th, (sx + sw) / tw, (sy + sh) / th, color);
}
void Renderer::itemIcon(int id, int metadata, float x, float y, float size, int tintColor) {
    auto icon = impl_->itemIcons.icon(id, metadata, ItemAppearance{tintColor, -1});
    if (!icon.valid || icon.layers.empty()) {
        if (id >= 0 && id < 256)
            blockIcon(blockState(id, metadata), x, y, size);
        return;
    }
    for (const auto &layer : icon.layers) {
        std::uint32_t argb = layer.color, color = ((argb & 0xffffff) << 8) | (argb >> 24);
        impl_->addQuad(impl_->itemAtlas, x, y, size, size, layer.u0, layer.v0, layer.u1, layer.v1, color);
    }
}
void Renderer::itemIcon(const ItemStack& item,float x,float y,float size,int tintColor) {
    if(item.empty())return;
    auto& p=*impl_;
    const std::size_t oldBatch=p.uiDraws.size(),oldVertices=oldBatch?p.uiDraws.back().vertices.size():0;
    bool drawn=false;
    if(item.id==397) {
        auto frame=p.tileVisuals.skullItem(item.damage,item.nbt,p.skinCache,0);
        auto base=EntityVisuals::inventoryTransform(x,y,size,true);
        gfx::Mat4 a;a.v=base;
        auto itemModel=p.firstPersonAssets.itemModel(item.id,item.damage,ItemModelTransform::Gui);
        gfx::Mat4 builtin=gfx::Mat4::identity();
        if(!itemModel.draws.empty())builtin.v=itemModel.draws.front().matrix;
        for(const auto& draw:frame.draws)if(draw.mesh&&draw.texture){
            gfx::Mat4 local;local.v=draw.matrix;
            const auto& t=*draw.texture;
            p.addUiMesh(*draw.mesh,(a*builtin*local).v,p.cachedTexture(t.key,t.revision,t.width,t.height,t.pixels));
            drawn=true;
        }
    }
    if(!drawn) {
        const VisualItem visual{item.id,item.damage,item.count,&item.nbt};
        itemIcon(item.id,item.damage,x,y,size,tintColor>=0?tintColor:EntityVisuals::primaryItemColor(visual));
    }
    if(EntityVisuals::hasEffect({item.id,item.damage,item.count,&item.nbt})&&p.runtimeTextures["glint"].id) {
        std::vector<UIDraw> effects;
        const std::size_t first=oldBatch?oldBatch-1:0,last=p.uiDraws.size();
        for(std::size_t i=first;i<last;++i){
            const auto& batch=p.uiDraws[i];const auto begin=i==oldBatch-1&&oldBatch?oldVertices:0;
            if(begin<batch.vertices.size())effects.push_back({batch.texture,{batch.vertices.begin()+std::ptrdiff_t(begin),batch.vertices.end()},true});
        }
        for(auto& effect:effects)p.uiDraws.push_back(std::move(effect));
    }
}
bool Renderer::registerUITexture(const std::string &name, const std::string &path) {
    auto texture = readRuntimeTexture(path);
    if (!texture.id)
        return false;
    auto old = impl_->runtimeTextures.find(name);
    if (old != impl_->runtimeTextures.end() && old->second.id)
        glDeleteTextures(1, &old->second.id);
    impl_->runtimeTextures[name] = texture;
    return true;
}
bool Renderer::registerImage(const std::string& name,unsigned width,unsigned height,const std::vector<std::uint8_t>& rgba) {
    if(!width||!height||width>8192||height>8192||rgba.size()!=std::size_t(width)*height*4)return false;
    auto& old=impl_->runtimeTextures[name];if(old.id)glDeleteTextures(1,&old.id);
    old={makeTexture(width,height,rgba.data()),width,height};return true;
}
void Renderer::setPlayerPreview(std::shared_ptr<const PlayerSkin> skin,const std::array<ItemStack,5>& equipment){
    impl_->localSkin=std::move(skin);impl_->previewEquipment=equipment;
}
void Renderer::playerFace(std::shared_ptr<const PlayerSkin> skin,float x,float y,float size,bool hat){
    auto& p=*impl_;auto texture=p.skinTexture(skin);
    p.addQuad(texture,x,y,size,size,8.f/64,8.f/64,16.f/64,16.f/64,0xffffffff);
    if(hat)p.addQuad(texture,x,y,size,size,40.f/64,8.f/64,48.f/64,16.f/64,0xffffffff);
}
void Renderer::previewEntity(float x, float bottom, float height, float lookYaw, float lookPitch) {
    auto &p = *impl_;
    EntityPose pose;
    pose.headYaw = lookYaw * .01745329252f;
    pose.headPitch = lookPitch * .01745329252f;
    pose.variant=p.localSkin&&p.localSkin->slim?1:0;
    pose.skinParts=options.playerSkinParts;
    PlayerVisualInput input;input.pose=pose;input.renderTimeMillis=std::int64_t(p.timeSeconds*1000);
    for(int slot=0;slot<5;++slot){const auto& item=p.previewEquipment[slot];input.equipment[slot]={item.id,item.damage,item.count,&item.nbt};}
    auto frame=EntityVisuals::player(p.entityModels,p.firstPersonAssets,input,&p.tileVisuals,p.skinCache);
    if(!frame.supported)return;
    auto& layers=frame.layers;
    struct Triangle {
        float depth;
        std::array<UIVertex, 3> vertices;
        GLuint texture=0;
        bool glint=false;
    };
    std::vector<Triangle> triangles;
    const float angle = -lookYaw*.01745329252f, c = std::cos(angle), s = std::sin(angle), scale = height / 1.8f;
    const float tilt=lookPitch*.01745329252f, cp=std::cos(tilt),sp=std::sin(tilt);
    for(const auto& layer:layers) {
      const auto& model=layer.mesh;
      const GLuint texture=layer.playerSkin?p.skinTexture(p.localSkin):(model.texture>=0?p.entityTextures[model.texture]:0);
      if(!texture)continue;
      for (std::size_t i = 0; i + 2 < model.vertices.size(); i += 3) {
        Triangle t{};
        t.texture=texture;t.glint=layer.glint;
        float facing = 0;
        for (int j = 0; j < 3; ++j) {
            const auto &v = model.vertices[i + j];
            float xx = v.x * c + v.z * s, zz = -v.x * s + v.z * c;
            const float yy=v.y*cp-zz*sp;
            zz=v.y*sp+zz*cp;
            float normal = v.ny*sp+(-v.nx*s+v.nz*c)*cp;
            facing += normal;
            t.depth += zz;
            auto color = std::uint8_t(
                std::clamp(.68f + std::max(0.f, v.ny) * .25f + std::max(0.f, normal) * .12f, .3f, 1.f) * 255);
            t.vertices[j] = {x + xx * scale, bottom - yy * scale, v.u, v.v, {std::uint8_t(color*layer.color[0]),std::uint8_t(color*layer.color[1]),std::uint8_t(color*layer.color[2]),255}};
        }
        if (facing > -.001f)
            triangles.push_back(t);
      }
    }
    auto addItem=[&](const FirstPersonMesh& mesh,const FirstPersonMatrix& m,GLuint texture,std::uint32_t color,bool glint){
        for(std::size_t i=0;i+2<mesh.vertices.size();i+=3){
            Triangle t{};t.texture=texture;t.glint=glint;
            for(int j=0;j<3;++j){const auto& v=mesh.vertices[i+j];
                const float vx=m[0]*v.x+m[4]*v.y+m[8]*v.z+m[12],vy=m[1]*v.x+m[5]*v.y+m[9]*v.z+m[13],vz=m[2]*v.x+m[6]*v.y+m[10]*v.z+m[14];
                const float xx=vx*c+vz*s,zz=-vx*s+vz*c,yy=vy*cp-zz*sp;
                t.depth+=vy*sp+zz*cp;
                t.vertices[j]={x+xx*scale,bottom-yy*scale,v.u,v.v,{std::uint8_t(color>>16),std::uint8_t(color>>8),std::uint8_t(color),std::uint8_t(color>>24)}};
            }
            triangles.push_back(std::move(t));
        }
    };
    for(const auto& draw:frame.held)if(draw.mesh){
        GLuint texture=draw.glint?p.runtimeTextures["glint"].id:p.firstAtlas;
        if(draw.dynamicTexture){const auto& t=*draw.dynamicTexture;texture=p.cachedTexture(t.key,t.revision,t.width,t.height,t.pixels,t.repeat);}
        addItem(*draw.mesh,draw.matrix,texture,draw.color,draw.glint);
    }
    for(const auto& draw:frame.customHead)if(draw.mesh&&draw.texture){const auto& t=*draw.texture;
        addItem(*draw.mesh,draw.matrix,p.cachedTexture(t.key,t.revision,t.width,t.height,t.pixels,t.repeat),draw.color,false);
    }
    std::stable_sort(triangles.begin(), triangles.end(), [](const auto &a, const auto &b) { return a.depth < b.depth; });
    for (const auto &triangle : triangles) {
        if(p.uiDraws.empty()||p.uiDraws.back().texture!=triangle.texture||p.uiDraws.back().glint!=triangle.glint)
            p.uiDraws.push_back({triangle.texture,{},triangle.glint});
        auto& vertices=p.uiDraws.back().vertices;
        vertices.insert(vertices.end(),triangle.vertices.begin(),triangle.vertices.end());
    }
}
void Renderer::rotatedText(const std::string &value, float cx, float cy, float scale, float degrees,
                           std::uint32_t color) {
    auto &p = *impl_;
    std::size_t firstBatch = p.uiDraws.size(), firstVertex = 0;
    if (firstBatch) {
        --firstBatch;
        firstVertex = p.uiDraws.back().vertices.size();
    }
    text(value, -textWidth(value, scale) * .5f, -8 * scale, scale, color);
    float c = std::cos(degrees * .01745329252f), s = std::sin(degrees * .01745329252f);
    for (std::size_t i = firstBatch; i < p.uiDraws.size(); ++i) {
        auto &verts = p.uiDraws[i].vertices;
        for (std::size_t j = i == firstBatch ? firstVertex : 0; j < verts.size(); ++j) {
            float x = verts[j].x, y = verts[j].y;
            verts[j].x = cx + x * c - y * s;
            verts[j].y = cy + x * s + y * c;
        }
    }
}
void Renderer::worldText(const std::string &value, double x, double y, double z, float worldScale) {
    auto &p = *impl_;
    float v[4] = {float(x - p.originX), float(y - p.originY), float(z - p.originZ), 1}, clip[4]{};
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col)
            clip[row] += p.lastVP.v[col * 4 + row] * v[col];
    if (clip[3] <= 0 || clip[2] < -clip[3] || clip[2] > clip[3])
        return;
    float scale = worldScale * p.uiHeight / (2 * std::tan(p.lastCamera.fov * .00872664626f) * clip[3]);
    float sx = (clip[0] / clip[3] * .5f + .5f) * p.uiWidth, sy = (.5f - clip[1] / clip[3] * .5f) * p.uiHeight;
    float width = textWidth(value, scale);
    rect(sx - width * .5f - scale, sy - scale, width + 2 * scale, 9 * scale, 0x00000055);
    text(value, sx - width * .5f, sy, scale, 0xffffffff, false);
}
void Renderer::endUI() {
    auto &p = *impl_;
    gl::BindFramebuffer(GL_FRAMEBUFFER,0);glViewport(0,0,p.uiWidth,p.uiHeight);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    p.ui.use();
    p.ui.vec2("uSize", float(p.uiWidth), float(p.uiHeight));
    p.ui.integer("uTexture", 0);
    p.ui.integer("uGlint",1);p.ui.scalar("uTime",float(p.timeSeconds));
    gl::ActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,p.runtimeTextures["glint"].id);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
    gl::ActiveTexture(GL_TEXTURE0);
    gl::BindVertexArray(p.uiVAO);
    gl::BindBuffer(GL_ARRAY_BUFFER, p.uiVBO);
    for (auto &batch : p.uiDraws) {
        p.ui.integer("uEffect",batch.glint?1:0);
        glBlendFunc(batch.glint?GL_SRC_COLOR:GL_SRC_ALPHA,batch.glint?GL_ONE:GL_ONE_MINUS_SRC_ALPHA);
        glBindTexture(GL_TEXTURE_2D, batch.texture);
        gl::BufferData(GL_ARRAY_BUFFER, std::ptrdiff_t(batch.vertices.size() * sizeof(UIVertex)), batch.vertices.data(),
                       GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, GLsizei(batch.vertices.size()));
    }
    glDisable(GL_BLEND);
}
bool Renderer::screenshot(const std::string &path, int w, int h, std::string &error) {
    std::vector<std::uint8_t> pixels(std::size_t(w) * h * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    std::ofstream f(path, std::ios::binary);
    if (!f) {
        error = "Cannot write screenshot: " + path;
        return false;
    }
    f << "P6\n" << w << ' ' << h << "\n255\n";
    for (int y = h - 1; y >= 0; --y)
        f.write(reinterpret_cast<char *>(pixels.data() + std::size_t(y) * w * 3), w * 3);
    if (!f) {
        error = "Screenshot write failed";
        return false;
    }
    return true;
}
const RenderStats &Renderer::stats() const { return impl_->stats; }
} // namespace mc
