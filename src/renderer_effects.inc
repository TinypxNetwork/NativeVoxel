// Renderer::Impl helpers for ItemRenderer overlays and Render.renderEntityOnFire.
std::array<std::array<float,4>,2> fireSprites{};
void loadEffectSprites() {
    for(int layer=0;layer<2;++layer){const auto& sprite=assets->sprite("minecraft:blocks/fire_layer_"+std::to_string(layer));
        if(sprite.valid)fireSprites[layer]={{sprite.u0,sprite.v0,sprite.u1,sprite.v1}};}
}
void prepareEntityFire(const std::vector<RenderEntity>& entities) {
    if(fireSprites[0][2]<=fireSprites[0][0]||fireSprites[1][2]<=fireSprites[1][0])return;
    const float yaw=-lastCamera.yaw*.01745329252f,c=std::cos(yaw),s=std::sin(yaw);
    const Frustum frustum(lastVP);
    for(const auto& entity:entities){
        if(!entity.burning||entity.invisible||entity.width<=0||entity.height<=0||entity.pose.spectator)continue;
        const Vec3 base{float(entity.x-originX),float(entity.y-originY),float(entity.z-originZ)};
        if(!frustum.visible(base-Vec3{entity.width,0,entity.width},base+Vec3{entity.width,entity.height+1,entity.width}))continue;
        const float scale=entity.width*1.4f;float remaining=entity.height/scale,half=.5f,offset=0,depth=0;
        const float baseDepth=-.3f+float(int(remaining))*.02f;
        const int first=int(entityVertices.size());int layer=0;
        while(remaining>0&&layer<128){
            const auto& uv=fireSprites[layer%2];float u0=uv[0],u1=uv[2];if(layer/2%2==0)std::swap(u0,u1);
            const float coordinates[4][4]={{half,-offset,u1,uv[3]},{-half,-offset,u0,uv[3]},{-half,1.4f-offset,u0,uv[1]},{half,1.4f-offset,u1,uv[1]}};
            for(int i:{0,1,2,0,2,3}){const float x=coordinates[i][0]*scale,y=coordinates[i][1]*scale,z=(depth+baseDepth)*scale;
                entityVertices.push_back({base.x+c*x+s*z,base.y+y,base.z-s*x+c*z,coordinates[i][2],coordinates[i][3],{255,255,255,255},{255,255,0,0}});}
            remaining-=.45f;offset-=.45f;half*=.9f;depth+=.03f;++layer;
        }
        entityDraws.push_back({first,int(entityVertices.size())-first,atlas,1,false,true});
    }
}
void drawEnvironmentOverlay(const World& world,const Camera& camera,int w,int h,const RenderOptions& options,float day) {
    if(!options.firstPersonEffects)return;
    const auto projection=perspective(options.firstPersonFov,float(w)/h,.05f,32);
    label.use();label.integer("uAtlas",0);label.scalar("uAlpha",1);label.vec3("uOffset",{});
    gl::ActiveTexture(GL_TEXTURE0);gl::BindVertexArray(labelVAO);gl::BindBuffer(GL_ARRAY_BUFFER,labelVBO);
    glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    auto quad=[&](GLuint texture,const Mat4& matrix,float half,float z,const std::array<float,4>& uv,std::uint8_t brightness,std::uint8_t opacity){
        if(!texture)return;label.matrix("uVP",projection*matrix);glBindTexture(GL_TEXTURE_2D,texture);
        const float p[4][4]={{-half,-half,uv[2],uv[3]},{half,-half,uv[0],uv[3]},{half,half,uv[0],uv[1]},{-half,half,uv[2],uv[1]}};
        std::array<Vertex,6> vertices;const int order[]{0,1,2,0,2,3};
        for(int i=0;i<6;++i){const auto* v=p[order[i]];vertices[i]={v[0],v[1],z,v[2],v[3],{brightness,brightness,brightness,opacity},{255,255,0,0}};}
        gl::BufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices.data(),GL_STREAM_DRAW);glDrawArrays(GL_TRIANGLES,0,6);
    };
    BlockState solid=0;
    for(int i=0;i<8;++i){const auto state=world.getBlock(floorBlock(camera.x+((i&1)?1:-1)*.24),floorBlock(camera.y+((i&2)?1:-1)*.05),floorBlock(camera.z+((i&4)?1:-1)*.24));
        if(assets->model(state).occludes)solid=state;}
    if(solid){const auto& model=assets->model(solid);if(!model.quads.empty()){
        std::array<float,4> uv{1,1,0,0};for(const auto& v:model.quads.front().vertices){uv[0]=std::min(uv[0],v.u);uv[1]=std::min(uv[1],v.v);uv[2]=std::max(uv[2],v.u);uv[3]=std::max(uv[3],v.v);}
        quad(atlas,Mat4::identity(),1,-.5f,uv,26,128);
    }}
    if(visual::viewpointInWater(world,camera.x,camera.y,camera.z)){
        const auto light=visual::combinedLight(world,floorBlock(camera.x),floorBlock(camera.y),floorBlock(camera.z));
        const int skylight=std::max(0,int(light>>4)-int((1.f-day)*11.f));
        const auto brightness=std::uint8_t(std::clamp(visual::brightness(std::max(skylight,int(light&15)),options.dimension),0.f,1.f)*255);
        const float u=-camera.yaw/64.f,v=camera.pitch/64.f;
        quad(runtimeTextures["underwater"].id,Mat4::identity(),1,-.5f,{{u,v,u+4,v+4}},brightness,128);
    }
    if(options.burning&&fireSprites[1][2]>fireSprites[1][0]){
        for(int i=0;i<2;++i){auto transform=Mat4::identity();const float angle=float(i*2-1)*10.f*.01745329252f;
            transform.v[0]=std::cos(angle);transform.v[8]=std::sin(angle);transform.v[2]=-std::sin(angle);transform.v[10]=std::cos(angle);
            transform.v[12]=-float(i*2-1)*.24f;transform.v[13]=-.3f;
            quad(atlas,transform,.5f,-.5f,fireSprites[1],255,230);
        }
    }
    glDepthMask(GL_TRUE);glEnable(GL_DEPTH_TEST);glEnable(GL_CULL_FACE);glDisable(GL_BLEND);
}
void drawFishingLines(const std::vector<RenderEntity>& entities,int thirdPerson) {
    bool active=false;
    for(const auto& entity:entities){
        if(!entity.fishingLine||entity.kind!=2||entity.type!=90)continue;
        if(std::abs(entity.x-lastCamera.x)>200||std::abs(entity.z-lastCamera.z)>200)continue;
        if(!active){line.use();line.matrix("uVP",lastVP);line.vec3("uOffset",{});line.integer("uFishing",1);
            gl::BindVertexArray(lineVAO);gl::BindBuffer(GL_ARRAY_BUFFER,lineVBO);glEnable(GL_DEPTH_TEST);glDisable(GL_BLEND);glLineWidth(1);active=true;}
        FishingLinePose pose;pose.hookX=entity.x;pose.hookY=entity.y;pose.hookZ=entity.z;
        pose.anglerX=entity.anglerX;pose.anglerY=entity.anglerY;pose.anglerZ=entity.anglerZ;
        pose.yaw=entity.anglerYaw;pose.pitch=entity.anglerPitch;pose.bodyYaw=entity.anglerBodyYaw;pose.swing=entity.anglerSwing;pose.eyeHeight=entity.anglerEyeHeight;
        pose.localAngler=entity.localAngler;pose.sneaking=entity.anglerSneaking;pose.thirdPerson=thirdPerson!=0;
        const auto points=EntityVisuals::fishingLine(pose);std::array<Vec3,17> vertices;
        for(int i=0;i<17;++i)vertices[i]={float(points[i][0]-originX),float(points[i][1]-originY),float(points[i][2]-originZ)};
        gl::BufferData(GL_ARRAY_BUFFER,sizeof(vertices),vertices.data(),GL_STREAM_DRAW);glDrawArrays(GL_LINE_STRIP,0,17);
    }
    if(active)line.integer("uFishing",0);
}
