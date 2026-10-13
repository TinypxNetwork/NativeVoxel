#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace mc::gfx {
struct Vec3 {
    float x=0, y=0, z=0;
    Vec3 operator+(Vec3 b) const { return {x+b.x,y+b.y,z+b.z}; }
    Vec3 operator-(Vec3 b) const { return {x-b.x,y-b.y,z-b.z}; }
    Vec3 operator*(float s) const { return {x*s,y*s,z*s}; }
};
inline float dot(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
inline Vec3 cross(Vec3 a,Vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline Vec3 normalize(Vec3 v){float n=std::sqrt(dot(v,v));return n>1e-8f?v*(1/n):Vec3{};}
struct Mat4 {
    std::array<float,16> v{};
    const float* data() const {return v.data();}
    static Mat4 identity(){Mat4 m; m.v[0]=m.v[5]=m.v[10]=m.v[15]=1; return m;}
    Mat4 operator*(const Mat4& b) const {Mat4 c;for(int j=0;j<4;++j)for(int i=0;i<4;++i)for(int k=0;k<4;++k)c.v[j*4+i]+=v[k*4+i]*b.v[j*4+k];return c;}
};
inline Mat4 perspective(float fov,float aspect,float nearZ,float farZ){
    Mat4 m;float f=1/std::tan(fov*0.008726646259971648f);m.v[0]=f/aspect;m.v[5]=f;
    m.v[10]=(farZ+nearZ)/(nearZ-farZ);m.v[11]=-1;m.v[14]=2*farZ*nearZ/(nearZ-farZ);return m;
}
inline Mat4 ortho(float l,float r,float b,float t,float n,float f){
    Mat4 m=Mat4::identity();m.v[0]=2/(r-l);m.v[5]=2/(t-b);m.v[10]=-2/(f-n);
    m.v[12]=-(r+l)/(r-l);m.v[13]=-(t+b)/(t-b);m.v[14]=-(f+n)/(f-n);return m;
}
inline Mat4 lookAt(Vec3 eye,Vec3 target,Vec3 up){
    Vec3 f=normalize(target-eye),s=normalize(cross(f,up)),u=cross(s,f);Mat4 m=Mat4::identity();
    m.v[0]=s.x;m.v[4]=s.y;m.v[8]=s.z;m.v[1]=u.x;m.v[5]=u.y;m.v[9]=u.z;
    m.v[2]=-f.x;m.v[6]=-f.y;m.v[10]=-f.z;m.v[12]=-dot(s,eye);m.v[13]=-dot(u,eye);m.v[14]=dot(f,eye);return m;
}
struct Frustum {
    std::array<std::array<float,4>,6> planes{};
    explicit Frustum(const Mat4& m){for(int a=0;a<3;++a)for(int sign=0;sign<2;++sign){auto& p=planes[a*2+sign];float s=sign?1.f:-1.f;for(int i=0;i<4;++i)p[i]=m.v[i*4+3]+s*m.v[i*4+a];float l=std::sqrt(p[0]*p[0]+p[1]*p[1]+p[2]*p[2]);for(float& q:p)q/=l;}}
    bool visible(Vec3 lo,Vec3 hi)const{for(const auto& p:planes){float d=p[3]+p[0]*(p[0]>=0?hi.x:lo.x)+p[1]*(p[1]>=0?hi.y:lo.y)+p[2]*(p[2]>=0?hi.z:lo.z);if(d<0)return false;}return true;}
};
inline Vec3 viewDirection(float yaw,float pitch){float y=yaw*0.017453292519943295f,p=pitch*0.017453292519943295f;return {-std::sin(y)*std::cos(p),-std::sin(p),std::cos(y)*std::cos(p)};}
// Build orientation from the angles, not lookAt(eye, eye + forward, worldUp).
// At +/-90 degrees worldUp is parallel to forward; adding a tiny horizontal
// component to a nonzero float eye also loses it entirely.
struct CameraBasis { Vec3 forward, right, up; };
inline CameraBasis cameraBasis(float yaw, float pitch) {
    const float y=yaw*0.017453292519943295f;
    CameraBasis b{viewDirection(yaw,pitch),{-std::cos(y),0,-std::sin(y)}, {}};
    b.up=cross(b.right,b.forward);
    return b;
}
inline Mat4 cameraView(Vec3 eye, const CameraBasis& b) {
    Mat4 m=Mat4::identity();
    m.v[0]=b.right.x;m.v[4]=b.right.y;m.v[8]=b.right.z;
    m.v[1]=b.up.x;m.v[5]=b.up.y;m.v[9]=b.up.z;
    m.v[2]=-b.forward.x;m.v[6]=-b.forward.y;m.v[10]=-b.forward.z;
    m.v[12]=-dot(b.right,eye);m.v[13]=-dot(b.up,eye);m.v[14]=dot(b.forward,eye);
    return m;
}
} // namespace mc::gfx
