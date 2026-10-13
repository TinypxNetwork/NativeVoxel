// Included inside renderer.cpp's anonymous namespace. Shader programs, pass
// graphs and uniforms come directly from the active 1.8.9 resource pack.
class SecretShaders {
    using Json=gui_json::Json;
    struct Target {GLuint texture=0,fbo=0;int width=0,height=0;bool resize=true;};
    struct Uniform {std::vector<float> values;bool integer=false;};
    struct Aux {std::string name,target;GLuint texture=0;int width=0,height=0;};
    struct Pass {Program program;std::string input,output;std::map<std::string,Uniform> uniforms;std::vector<Aux> auxiliary;};
    std::map<std::string,Target> targets_;
    std::vector<Pass> passes_;
    std::string directory_;
    int selected_=-1,width_=0,height_=0;
    float previousPartial_=0,time_=0;
    GLuint vao_=0,vbo_=0;
    static const Json& field(const Json& object,const std::string& name){static const Json absent;const auto* p=object.get(name);return p?*p:absent;}
    static std::string string(const Json& object,const std::string& name){return field(object,name).value;}
    static int integer(const Json& object,const std::string& name,int fallback){const auto& v=field(object,name);return v.kind==Json::Kind::Number?std::stoi(v.value):fallback;}
    std::string read(const std::string& path){
        if(path.find("..")!=std::string::npos||path.find('\\')!=std::string::npos||path.find(':')!=std::string::npos||path.empty()||path[0]=='/')throw std::runtime_error("Invalid shader resource path");
        std::ifstream file(std::filesystem::u8path(directory_+"/resources/assets/minecraft/"+path),std::ios::binary|std::ios::ate);
        if(!file)throw std::runtime_error("Missing shader resource: "+path);
        const auto size=file.tellg();if(size<0||size>1024*1024)throw std::runtime_error("Shader resource size limit");
        std::string result(static_cast<std::size_t>(size),'\0');file.seekg(0);file.read(result.data(),std::streamsize(result.size()));
        if(!file)throw std::runtime_error("Truncated shader resource");return result;
    }
    Json json(const std::string& path){const auto text=read(path);return gui_json::JsonParser(text,1024*1024).parse();}
    static void replace(std::string& source,const std::string& from,const std::string& to){std::size_t at=0;while((at=source.find(from,at))!=std::string::npos){source.replace(at,from.size(),to);at+=to.size();}}
    static std::string coreSource(std::string source,bool vertex){
        // Preserve the original arithmetic; only GLSL interface syntax changes.
        const auto version=source.find("#version");
        if(version!=std::string::npos){const auto end=source.find('\n',version);source.erase(version,end==std::string::npos?source.size()-version:end-version+1);}
        replace(source,"#extension GL_EXT_gpu_shader4 : enable","");
        source=std::string(vertex?"#version 330 core\n":"#version 330 core\nout vec4 nativeResult;\n")+source;
        replace(source,"attribute vec4 Position;","layout(location=0) in vec4 Position;");
        replace(source,"varying ",vertex?"out ":"in ");
        replace(source,"texture2D(","texture(");replace(source,"gl_FragColor","nativeResult");
        replace(source,"texture2DLod(","textureLod(");
        return source;
    }
    static void addUniforms(Pass& pass,const Json& object){
        for(const auto& u:field(object,"uniforms").array){auto& value=pass.uniforms[string(u,"name")];
            value.integer=string(u,"type")=="int";value.values.clear();
            for(const auto& v:field(u,"values").array){const float number=std::stof(v.value);if(!std::isfinite(number))throw std::runtime_error("Invalid shader uniform");value.values.push_back(number);}
            const int count=integer(u,"count",int(value.values.size()));
            if(value.values.size()==1&&count>1&&count<=16)value.values.resize(count,value.values.front());
            if(value.values.empty()||value.values.size()>16)throw std::runtime_error("Invalid shader uniform size");
        }
    }
    static void allocate(Target& target,int width,int height){
        if(target.width==width&&target.height==height&&target.texture)return;
        target.width=width;target.height=height;
        if(target.texture)glDeleteTextures(1,&target.texture);
        target.texture=makeTexture(unsigned(width),unsigned(height),nullptr);
        if(!target.fbo)gl::GenFramebuffers(1,&target.fbo);
        gl::BindFramebuffer(GL_FRAMEBUFFER,target.fbo);
        gl::FramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,target.texture,0);
        if(gl::CheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)throw std::runtime_error("Shader framebuffer incomplete");
        glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);
    }
    void load(int index,int width,int height){
        clear();selected_=index;width_=width;height_=height;
        if(index<0||index>=24)return;
        static const char* names[]{"notch","fxaa","art","bumpy","blobs2","pencil","color_convolve","deconverge","flip","invert","ntsc","outline","phosphor","scan_pincushion","sobel","bits","desaturate","green","blur","wobble","blobs","antialias","creeper","spider"};
        const auto group=json(std::string("shaders/post/")+names[index]+".json");
        for(const auto& entry:field(group,"targets").array){
            const auto name=entry.kind==Json::Kind::String?entry.value:string(entry,"name");
            if(name.empty()||name=="minecraft:main"||targets_.count(name)||targets_.size()>=32)throw std::runtime_error("Invalid shader target");
            auto& target=targets_[name];const int w=integer(entry,"width",width),h=integer(entry,"height",height);
            if(w<1||h<1||w>8192||h>8192)throw std::runtime_error("Shader target dimensions exceed limit");
            target.resize=w==width&&h==height;allocate(target,w,h);
        }
        for(const auto& entry:field(group,"passes").array){
            if(passes_.size()>=64)throw std::runtime_error("Shader pass count exceeds limit");
            passes_.emplace_back();auto& pass=passes_.back();
            const auto description=json("shaders/program/"+string(entry,"name")+".json");
            const auto vertex=coreSource(read("shaders/program/"+string(description,"vertex")+".vsh"),true);
            const auto fragment=coreSource(read("shaders/program/"+string(description,"fragment")+".fsh"),false);
            pass.program.build(vertex.c_str(),fragment.c_str());
            pass.input=string(entry,"intarget");pass.output=string(entry,"outtarget");
            for(const auto& name:{pass.input,pass.output})if(name!="minecraft:main"&&!targets_.count(name))throw std::runtime_error("Missing shader framebuffer");
            if(pass.input==pass.output)throw std::runtime_error("Shader framebuffer reads its own output");
            addUniforms(pass,description);addUniforms(pass,entry);
            for(const auto& source:field(entry,"auxtargets").array){
                if(pass.auxiliary.size()>=7)throw std::runtime_error("Shader texture unit limit");
                pass.auxiliary.emplace_back();auto& aux=pass.auxiliary.back();aux.name=string(source,"name");aux.target=string(source,"id");
                if(!targets_.count(aux.target)&&aux.target!="minecraft:main"){
                    if(aux.target.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_-")!=std::string::npos)throw std::runtime_error("Invalid shader auxiliary texture");
                    ResourceImage image;std::string problem;
                    if(!readResourceImage(directory_,"assets/minecraft/textures/effect/"+aux.target+".png",image,problem))throw std::runtime_error(problem);
                    aux.texture=makeTexture(image.width,image.height,image.pixels.data());
                    const int filter=string(source,"bilinear")=="true"?GL_LINEAR:GL_NEAREST;
                    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,filter);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,filter);
                    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
                    aux.width=integer(source,"width",int(image.width));aux.height=integer(source,"height",int(image.height));
                }
            }
        }
        if(!vao_){gl::GenVertexArrays(1,&vao_);gl::GenBuffers(1,&vbo_);gl::BindVertexArray(vao_);gl::BindBuffer(GL_ARRAY_BUFFER,vbo_);
            gl::EnableVertexAttribArray(0);gl::VertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,3*sizeof(float),nullptr);}
    }
public:
    std::string error;
    void directory(const std::string& value){directory_=value;}
    std::size_t passCount()const{return passes_.size();}
    void clear(){
        for(auto& pass:passes_){pass.program.destroy();for(auto& aux:pass.auxiliary)if(aux.texture)glDeleteTextures(1,&aux.texture);}
        passes_.clear();for(auto& entry:targets_){glDeleteTextures(1,&entry.second.texture);gl::DeleteFramebuffers(1,&entry.second.fbo);}targets_.clear();
        previousPartial_=time_=0;selected_=-1;error.clear();
    }
    void destroy(){clear();if(vbo_)gl::DeleteBuffers(1,&vbo_);if(vao_)gl::DeleteVertexArrays(1,&vao_);vbo_=vao_=0;}
    void apply(int index,GLuint mainFbo,GLuint mainTexture,int width,int height,float partial){
        try{
            gl::ActiveTexture(GL_TEXTURE0);
            if(index!=selected_)load(index,width,height);
            if(index<0||index>=24||!error.empty())return;
            if(width!=width_||height!=height_){for(auto& entry:targets_)if(entry.second.resize)allocate(entry.second,width,height);width_=width;height_=height;}
            time_+=partial<previousPartial_?1-previousPartial_+partial:partial-previousPartial_;previousPartial_=partial;
            while(time_>20)time_-=20;
            auto target=[&](const std::string& name)->Target{return name=="minecraft:main"?Target{mainTexture,mainFbo,width,height,true}:targets_.at(name);};
            glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glDisable(GL_BLEND);glDepthMask(GL_FALSE);glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);
            gl::BindVertexArray(vao_);gl::BindBuffer(GL_ARRAY_BUFFER,vbo_);
            for(auto& pass:passes_){
                const auto input=target(pass.input),output=target(pass.output);pass.program.use();
                for(const auto& entry:pass.uniforms){const auto& v=entry.second.values;const auto location=pass.program.at(entry.first.c_str());
                    if(entry.second.integer)gl::Uniform1i(location,int(v[0]));
                    else if(v.size()==1)gl::Uniform1f(location,v[0]);
                    else if(v.size()==2)gl::Uniform2f(location,v[0],v[1]);
                    else if(v.size()==3)gl::Uniform3f(location,v[0],v[1],v[2]);
                    else if(v.size()==4)gl::Uniform4f(location,v[0],v[1],v[2],v[3]);
                    else if(v.size()==16)gl::UniformMatrix4fv(location,1,GL_FALSE,v.data());
                }
                auto projection=gfx::Mat4::identity();projection.v[0]=2.f/width;projection.v[5]=-2.f/height;projection.v[10]=-.0020001999f;
                projection.v[12]=-1;projection.v[13]=1;projection.v[14]=-1.0001999f;
                pass.program.matrix("ProjMat",projection);pass.program.vec2("InSize",float(input.width),float(input.height));
                pass.program.vec2("OutSize",float(output.width),float(output.height));pass.program.vec2("ScreenSize",float(width),float(height));pass.program.scalar("Time",time_/20.f);
                gl::ActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,input.texture);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
                pass.program.integer("DiffuseSampler",0);
                for(std::size_t i=0;i<pass.auxiliary.size();++i){const auto& aux=pass.auxiliary[i];
                    const auto source=aux.texture?Target{aux.texture,0,aux.width,aux.height,false}:target(aux.target);
                    gl::ActiveTexture(GL_TEXTURE0+GLenum(i+1));glBindTexture(GL_TEXTURE_2D,source.texture);
                    pass.program.integer(aux.name.c_str(),int(i+1));pass.program.vec2(("AuxSize"+std::to_string(i)).c_str(),float(source.width),float(source.height));
                }
                gl::BindFramebuffer(GL_FRAMEBUFFER,output.fbo);glViewport(0,0,output.width,output.height);glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);
                const float w=float(output.width),h=float(output.height);const float vertices[]{0,h,500,w,h,500,w,0,500,0,h,500,w,0,500,0,0,500};
                gl::BufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices,GL_STREAM_DRAW);glDrawArrays(GL_TRIANGLES,0,6);
            }
        }catch(const std::exception& exception){const std::string problem=exception.what();clear();selected_=index;error=problem;}
        gl::ActiveTexture(GL_TEXTURE0);glDepthMask(GL_TRUE);
    }
};
