#include "mc/world.hpp"
#include "mc/registry.hpp"
#include "mc/selection_data.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace mc {
namespace {

unsigned sectionIndex(int x, int y, int z) {
    return (static_cast<unsigned>(y & 15) << 8) |
           (static_cast<unsigned>(z & 15) << 4) | static_cast<unsigned>(x & 15);
}

bool stairs(unsigned id) {
    switch (id) {
    case 53: case 67: case 108: case 109: case 114: case 128: case 134:
    case 135: case 136: case 156: case 163: case 164: case 180: return true;
    default: return false;
    }
}
bool fence(unsigned id) { return id == 85 || id == 113 || (id >= 188 && id <= 192); }
bool gate(unsigned id) { return id == 107 || (id >= 183 && id <= 187); }
bool door(unsigned id) { return id == 64 || id == 71 || (id >= 193 && id <= 197); }
bool pane(unsigned id) { return id == 101 || id == 102 || id == 160; }
bool water(unsigned id) { return id == 8 || id == 9; }
bool lava(unsigned id) { return id == 10 || id == 11; }

bool noCollision(unsigned id) {
    switch (id) {
    case 0: case 6: case 8: case 9: case 10: case 11: case 27: case 28:
    case 30: case 31: case 32: case 36: case 37: case 38: case 39: case 40:
    case 50: case 51: case 55: case 59: case 63: case 66: case 68: case 69:
    case 70: case 72: case 75: case 76: case 77: case 83: case 90: case 104:
    case 105: case 106: case 115: case 119: case 131: case 132: case 141:
    case 142: case 143: case 147: case 148: case 157: case 175: case 176:
    case 177: return true;
    default: return false;
    }
}

bool fullCube(BlockState state) {
    return registry::block(blockId(state)).fullCube;
}

bool opaqueMaterial(unsigned id) {
    return registry::block(id).materialOpaque;
}

bool materialBlocksMovement(unsigned id) {
    return registry::block(id).materialBlocksMovement;
}

bool normalCube(BlockState state) {
    return registry::block(blockId(state)).normalCube;
}

bool blocksSky(BlockState state) {
    return registry::block(blockId(state)).lightOpacity != 0;
}

float slipperiness(BlockState state) {
    return registry::block(blockId(state)).slipperiness;
}

int javaInt(float value) {
    if (std::isnan(value)) return 0;
    if (value >= static_cast<float>(std::numeric_limits<int>::max())) return std::numeric_limits<int>::max();
    if (value <= static_cast<float>(std::numeric_limits<int>::min())) return std::numeric_limits<int>::min();
    return static_cast<int>(value);
}

const std::array<float, 65536>& sineTable() {
    static const auto table = [] {
        std::array<float, 65536> result{};
        constexpr double pi = 3.14159265358979323846;
        for (std::size_t i = 0; i < result.size(); ++i)
            result[i] = static_cast<float>(std::sin(static_cast<double>(i) * pi * 2.0 / 65536.0));
        return result;
    }();
    return table;
}

bool fail(std::string* error, const char* message) {
    if (error) *error = message;
    return false;
}

bool validChunk(int x, int z) {
    // Allows the border-adjacent chunk while preventing integer overflow in x*16.
    return x >= -1875001 && x <= 1875000 && z >= -1875001 && z <= 1875000;
}

std::uint64_t mix(std::uint64_t value) {
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

} // namespace

int floorDiv16(int value) { return value / 16 - (value % 16 < 0 ? 1 : 0); }
int floorBlock(double value) {
    if (std::isnan(value)) return 0;
    if (value <= static_cast<double>(std::numeric_limits<int>::min())) return std::numeric_limits<int>::min();
    if (value >= static_cast<double>(std::numeric_limits<int>::max())) return std::numeric_limits<int>::max();
    const int integer = static_cast<int>(value);
    return value < static_cast<double>(integer) ? integer - 1 : integer;
}
float javaSin(float radians) { return sineTable()[static_cast<unsigned>(javaInt(radians * 10430.378f)) & 65535]; }
float javaCos(float radians) { return sineTable()[static_cast<unsigned>(javaInt(radians * 10430.378f + 16384.0f)) & 65535]; }

Aabb Aabb::offset(double x, double y, double z) const {
    return {minX + x, minY + y, minZ + z, maxX + x, maxY + y, maxZ + z};
}
Aabb Aabb::addCoord(double x, double y, double z) const {
    return {minX + std::min(0.0, x), minY + std::min(0.0, y), minZ + std::min(0.0, z),
            maxX + std::max(0.0, x), maxY + std::max(0.0, y), maxZ + std::max(0.0, z)};
}
Aabb Aabb::expand(double x, double y, double z) const {
    // AxisAlignedBB's constructor sorts bounds, including over-contraction.
    return {std::min(minX - x, maxX + x), std::min(minY - y, maxY + y), std::min(minZ - z, maxZ + z),
            std::max(minX - x, maxX + x), std::max(minY - y, maxY + y), std::max(minZ - z, maxZ + z)};
}
bool Aabb::intersects(const Aabb& o) const {
    return o.maxX > minX && o.minX < maxX && o.maxY > minY && o.minY < maxY && o.maxZ > minZ && o.minZ < maxZ;
}
double Aabb::clipX(const Aabb& o, double amount) const {
    if (o.maxY > minY && o.minY < maxY && o.maxZ > minZ && o.minZ < maxZ) {
        if (amount > 0.0 && o.maxX <= minX) amount = std::min(amount, minX - o.maxX);
        else if (amount < 0.0 && o.minX >= maxX) amount = std::max(amount, maxX - o.minX);
    }
    return amount;
}
double Aabb::clipY(const Aabb& o, double amount) const {
    if (o.maxX > minX && o.minX < maxX && o.maxZ > minZ && o.minZ < maxZ) {
        if (amount > 0.0 && o.maxY <= minY) amount = std::min(amount, minY - o.maxY);
        else if (amount < 0.0 && o.minY >= maxY) amount = std::max(amount, maxY - o.minY);
    }
    return amount;
}
double Aabb::clipZ(const Aabb& o, double amount) const {
    if (o.maxX > minX && o.minX < maxX && o.maxY > minY && o.minY < maxY) {
        if (amount > 0.0 && o.maxZ <= minZ) amount = std::min(amount, minZ - o.maxZ);
        else if (amount < 0.0 && o.minZ >= maxZ) amount = std::max(amount, maxZ - o.minZ);
    }
    return amount;
}

BlockState Chunk::getBlock(int localX, int y, int localZ) const {
    if (y < 0 || y >= 256 || !sections[y >> 4]) return 0;
    return sections[y >> 4]->blocks[sectionIndex(localX, y, localZ)];
}
std::uint8_t Chunk::getBlockLight(int localX, int y, int localZ) const {
    if (y < 0 || y >= 256 || !sections[y >> 4]) return 0;
    const unsigned i = sectionIndex(localX, y, localZ);
    return static_cast<std::uint8_t>((sections[y >> 4]->blockLight[i >> 1] >> ((i & 1) * 4)) & 15);
}
std::uint8_t Chunk::getSkyLight(int localX, int y, int localZ) const {
    if (!hasSkyLight || y < 0) return 0;
    if (y >= 256) return 15;
    if (!sections[y >> 4]) return y >= heightMap[(localZ & 15) * 16 + (localX & 15)] ? 15 : 0;
    const unsigned i = sectionIndex(localX, y, localZ);
    return static_cast<std::uint8_t>((sections[y >> 4]->skyLight[i >> 1] >> ((i & 1) * 4)) & 15);
}

std::uint64_t World::key(int x, int z) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32) | static_cast<std::uint32_t>(z);
}
const Chunk* World::chunk(int x, int z) const {
    const auto found = chunks_.find(key(x, z));
    return found == chunks_.end() ? nullptr : &found->second;
}
bool World::hasChunk(int x, int z) const { return chunk(x, z) != nullptr; }
BlockState World::getBlock(int x, int y, int z) const {
    if (y < 0 || y >= 256 || x < -30000000 || x >= 30000000 || z < -30000000 || z >= 30000000) return 0;
    const auto c = chunk(floorDiv16(x), floorDiv16(z));
    return c ? c->getBlock(x & 15, y, z & 15) : 0;
}
std::uint8_t World::getBlockLight(int x, int y, int z) const {
    const auto c = chunk(floorDiv16(x), floorDiv16(z));
    return c ? c->getBlockLight(x & 15, y, z & 15) : 0;
}
std::uint8_t World::getSkyLight(int x, int y, int z) const {
    const auto c = chunk(floorDiv16(x), floorDiv16(z));
    return c ? c->getSkyLight(x & 15, y, z & 15) : 15;
}
std::uint8_t World::getBiome(int x, int z) const {
    const auto c = chunk(floorDiv16(x), floorDiv16(z));
    return c && c->hasBiomes ? c->biomes[(z & 15) * 16 + (x & 15)] : 1;
}
Chunk& World::ensureChunk(int x, int z) {
    auto& result = chunks_[key(x, z)];
    result.x = x;
    result.z = z;
    return result;
}
void World::touch(int cx, int cz) {
    ++revision_;
    // Meshes also read neighbors (face visibility, connected shapes and AO).
    for (int dx = -1; dx <= 1; ++dx) for (int dz = -1; dz <= 1; ++dz) {
        const auto it = chunks_.find(key(cx + dx, cz + dz));
        if (it != chunks_.end()) it->second.revision = revision_;
    }
}
void World::updateHeight(Chunk& c, int lx, int lz) {
    int height = 256;
    while (height > 0 && !blocksSky(c.getBlock(lx, height - 1, lz))) --height;
    c.heightMap[lz * 16 + lx] = static_cast<std::uint16_t>(height);
}
bool World::setBlock(int x, int y, int z, BlockState state) {
    if (y < 0 || y >= 256 || x < -30000000 || x >= 30000000 || z < -30000000 || z >= 30000000) return false;
    const auto previous=getBlock(x,y,z);
    if (previous == state) return false;
    const int cx = floorDiv16(x), cz = floorDiv16(z);
    Chunk& c = ensureChunk(cx, cz);
    const int oldHeight=c.heightMap[(z&15)*16+(x&15)];
    auto& section = c.sections[y >> 4];
    if (!section) {
        section = std::make_unique<ChunkSection>();
        // Keep the skylight of an absent section when it receives its first block.
        if(c.hasSkyLight)for(int ly=0;ly<16;++ly)for(int lz=0;lz<16;++lz)for(int lx=0;lx<16;++lx){
            const unsigned index=sectionIndex(lx,ly,lz);
            if((y&~15)+ly>=c.heightMap[lz*16+lx])
                section->skyLight[index>>1]|=std::uint8_t(15<<((index&1)*4));
        }
    }
    section->blocks[sectionIndex(x, y, z)] = state;
    updateHeight(c, x & 15, z & 15);
    touch(cx, cz);
    relightAfterChange(x,y,z,oldHeight,previous);
    return true;
}
void World::unloadChunk(int x, int z) {
    if (chunks_.erase(key(x, z))) touch(x, z);
}
void World::clear() {
    if (!chunks_.empty()) { chunks_.clear(); ++revision_; }
}
void World::setWorldBorder(double cx, double cz, double diameter, int portalBoundary) {
    if (!std::isfinite(cx) || !std::isfinite(cz) || !std::isfinite(diameter) || diameter < 0 || portalBoundary < 0)
        throw std::invalid_argument("invalid world border");
    border_.minX = std::max(cx - diameter / 2.0, static_cast<double>(-portalBoundary));
    border_.minZ = std::max(cz - diameter / 2.0, static_cast<double>(-portalBoundary));
    border_.maxX = std::min(cx + diameter / 2.0, static_cast<double>(portalBoundary));
    border_.maxZ = std::min(cz + diameter / 2.0, static_cast<double>(portalBoundary));
}
std::size_t World::chunkDataSize(std::uint16_t mask, bool skyLight, bool groundUp) {
    std::size_t count = 0;
    for (unsigned bits = mask; bits; bits >>= 1) count += bits & 1;
    return count * (8192 + 2048 + (skyLight ? 2048 : 0)) + (groundUp ? 256 : 0);
}
void World::decodeChunk(int x, int z, bool groundUp, std::uint16_t mask,
                        bool skyLight, const std::uint8_t* data) {
    auto& c = ensureChunk(x, z);
    c.hasSkyLight = skyLight;
    std::size_t offset = 0;
    for (int section = 0; section < 16; ++section) {
        if (mask & (1u << section)) {
            if (!c.sections[section]) c.sections[section] = std::make_unique<ChunkSection>();
            auto& s = *c.sections[section];
            for (auto& state : s.blocks) {
                state = static_cast<BlockState>(data[offset] | (static_cast<unsigned>(data[offset + 1]) << 8));
                offset += 2;
            }
        } else if (groundUp) c.sections[section].reset();
    }
    for (int section = 0; section < 16; ++section) if (mask & (1u << section)) {
        std::memcpy(c.sections[section]->blockLight.data(), data + offset, 2048);
        offset += 2048;
    }
    for (int section = 0; section < 16; ++section) if (mask & (1u << section)) {
        if (skyLight) {
            std::memcpy(c.sections[section]->skyLight.data(), data + offset, 2048);
            offset += 2048;
        } else c.sections[section]->skyLight.fill(0);
    }
    if (groundUp) {
        std::memcpy(c.biomes.data(), data + offset, 256);
        c.hasBiomes = true;
    }
    for (int lz = 0; lz < 16; ++lz) for (int lx = 0; lx < 16; ++lx) updateHeight(c, lx, lz);
    touch(x, z);
}
bool World::applyChunk(int x, int z, bool groundUp, std::uint16_t mask, bool skyLight,
                       const std::uint8_t* data, std::size_t size, std::string* error) {
    if (error) error->clear();
    if (!validChunk(x, z)) return fail(error, "chunk coordinates exceed vanilla world limits");
    if (size && !data) return fail(error, "null chunk payload");
    if (size > 2u * 1024 * 1024) return fail(error, "chunk payload exceeds protocol frame limit");
    if (groundUp && mask == 0) {
        unloadChunk(x, z);
        return true;
    }
    // Vanilla consumes arrays according to provider.getHasNoSky and never
    // inspects fillChunk's remaining byte[]. Some limbo/proxy servers include
    // sky arrays in End packets; preserve vanilla's biome offset, do not infer
    // a different dimension layout from the extra bytes.
    if (size < chunkDataSize(mask, skyLight, groundUp)) return fail(error, "chunk payload is shorter than section mask requires");
    if (mask || groundUp) decodeChunk(x, z, groundUp, mask, skyLight, data);
    return true;
}
bool World::applyBulk(const std::vector<BulkChunk>& columns, bool skyLight,
                      const std::uint8_t* data, std::size_t size, std::string* error) {
    if (error) error->clear();
    if (size && !data) return fail(error, "null bulk chunk payload");
    std::size_t expected = 0;
    for (const auto& c : columns) {
        if (!validChunk(c.x, c.z)) return fail(error, "bulk chunk coordinates exceed vanilla world limits");
        const auto count = chunkDataSize(c.primaryMask, skyLight, true);
        if (count > size - std::min(expected, size)) return fail(error, "truncated bulk chunk payload");
        expected += count;
    }
    if (expected != size) return fail(error, "bulk chunk payload has trailing bytes");
    std::size_t offset = 0;
    for (const auto& c : columns) {
        // S26 empty columns are real loaded columns, unlike the S21 unload form.
        decodeChunk(c.x, c.z, true, c.primaryMask, skyLight, data + offset);
        offset += chunkDataSize(c.primaryMask, skyLight, true);
    }
    return true;
}

bool World::isFullCube(int x, int y, int z) const { return fullCube(getBlock(x, y, z)); }

Aabb World::rayBoundsLocal(int x,int y,int z) const {
    const auto state=getBlock(x,y,z);const auto id=blockId(state);
    const auto& raw=selection::state(state).rayBounds;
    Aabb result{raw[0],raw[1],raw[2],raw[3],raw[4],raw[5]};
    if(door(id)||id==54||id==146||fence(id)||id==139) {
        std::vector<Aabb> boxes;blockCollisionBoxes(x,y,z,boxes);
        if(!boxes.empty()){
            result=boxes.front().offset(-double(x),-double(y),-double(z));
            for(std::size_t i=1;i<boxes.size();++i){const auto b=boxes[i].offset(-double(x),-double(y),-double(z));result.minX=std::min(result.minX,b.minX);result.minY=std::min(result.minY,b.minY);result.minZ=std::min(result.minZ,b.minZ);result.maxX=std::max(result.maxX,b.maxX);result.maxY=std::max(result.maxY,b.maxY);result.maxZ=std::max(result.maxZ,b.maxZ);}
            if(fence(id))result.maxY=1;
            if(id==139)result.maxY=((result.minX==.3125&&result.maxX==.6875)||(result.minZ==.3125&&result.maxZ==.6875))?.8125:1;
        }
    } else if(pane(id)) {
        const auto connects=[&](int bx,int bz){auto neighbor=blockId(getBlock(bx,y,bz));return registry::block(neighbor).fullBlock||neighbor==20||neighbor==95||pane(neighbor);};
        const bool n=connects(x,z-1),s=connects(x,z+1),w=connects(x-1,z),e=connects(x+1,z),any=n||s||w||e;
        result={.4375,0,.4375,.5625,1,.5625};
        if((!w||!e)&&any){if(w)result.minX=0;else if(e)result.maxX=1;}else{result.minX=0;result.maxX=1;}
        if((!n||!s)&&any){if(n)result.minZ=0;else if(s)result.maxZ=1;}else{result.minZ=0;result.maxZ=1;}
    } else if(id==106&&blockMeta(state)==0) {
        const auto& above=registry::block(blockId(getBlock(x,y+1,z)));
        if(above.fullCube&&above.materialBlocksMovement)result={0,.9375,0,1,1,1};
    }
    return result;
}

Aabb World::selectionBounds(int x,int y,int z) const {
    const auto state=getBlock(x,y,z);const auto id=blockId(state);
    if(door(id)||id==54||id==146||fence(id)||id==139||pane(id)||id==106) {
        const auto b=rayBoundsLocal(x,y,z);
        return {x+std::min(b.minX,b.maxX),y+std::min(b.minY,b.maxY),z+std::min(b.minZ,b.maxZ),
                x+std::max(b.minX,b.maxX),y+std::max(b.minY,b.maxY),z+std::max(b.minZ,b.maxZ)};
    }
    if(id==81)return {double(float(x)+.0625f),double(y),double(float(z)+.0625f),double(float(x+1)-.0625f),double(y+1),double(float(z+1)-.0625f)};
    const auto& b=selection::state(state).selected;
    return {x+b[0],y+b[1],z+b[2],x+b[3],y+b[4],z+b[5]};
}

namespace {
BlockRayHit traceBounds(const Aabb& bounds,int x,int y,int z,BlockState state,
                        double sx,double sy,double sz,double ex,double ey,double ez) {
    // Block.collisionRayTrace first translates both endpoints into block space.
    sx+=double(-x);sy+=double(-y);sz+=double(-z);ex+=double(-x);ey+=double(-y);ez+=double(-z);
    const double start[]={sx,sy,sz},delta[]={ex-sx,ey-sy,ez-sz};
    const double low[]={bounds.minX,bounds.minY,bounds.minZ},high[]={bounds.maxX,bounds.maxY,bounds.maxZ};
    constexpr int faces[]={4,5,0,1,2,3};
    BlockRayHit result;double bestDistance=0;
    for(int candidate=0;candidate<6;++candidate) {
        const int axis=candidate/2;
        if(delta[axis]*delta[axis]<1.0000000116860974e-7)continue;
        const double fraction=((candidate&1?high[axis]:low[axis])-start[axis])/delta[axis];
        if(fraction<0||fraction>1)continue;
        const double point[]={sx+delta[0]*fraction,sy+delta[1]*fraction,sz+delta[2]*fraction};
        bool inside=true;for(int a=0;a<3;++a)if(a!=axis)inside=inside&&point[a]>=low[a]&&point[a]<=high[a];
        if(!inside)continue;
        const double dx=point[0]-sx,dy=point[1]-sy,dz=point[2]-sz,distance=dx*dx+dy*dy+dz*dz;
        if(!result.hit||distance<bestDistance){result={true,false,x,y,z,faces[candidate],point[0]+x,point[1]+y,point[2]+z,state};bestDistance=distance;}
    }
    return result;
}
}

BlockRayHit World::raycast(double sx,double sy,double sz,double ex,double ey,double ez,
                           bool stopOnLiquid,bool ignoreWithoutBox,bool returnLastMiss) const {
    for(double value:{sx,sy,sz,ex,ey,ez})if(!std::isfinite(value)||std::abs(value)>32000000)return {};
    const int endX=floorBlock(ex),endY=floorBlock(ey),endZ=floorBlock(ez);
    int bx=floorBlock(sx),by=floorBlock(sy),bz=floorBlock(sz);
    const auto canRay=[&](BlockState state){const auto& shape=selection::state(state);return shape.valid&&(stopOnLiquid?shape.liquidRay:shape.ray);};
    const auto allowed=[&](BlockState state){return !ignoreWithoutBox||selection::state(state).collision;};
    const auto blockTrace=[&](int x,int y,int z,BlockState state,double startX,double startY,double startZ) {
        const auto id=blockId(state);
        if(id==36)return BlockRayHit{}; // BlockPistonMoving.collisionRayTrace always returns null.
        if(stairs(id)) {
            // 1.8 stairs deliberately ray trace six octants of a straight stair;
            // their neighbor-dependent collision shape is a separate algorithm.
            constexpr int excluded[8][2]={{4,5},{5,7},{6,7},{4,6},{0,1},{1,3},{2,3},{0,2}};
            constexpr int horizontal[]={3,1,0,2};
            const auto meta=blockMeta(state);const int shape=horizontal[meta&3]+((meta&4)?4:0);
            BlockRayHit best;double farthest=0;
            for(int pass=0;pass<8;++pass)if(pass!=excluded[shape][0]&&pass!=excluded[shape][1]) {
                const double x0=.5*(pass%2),y0=.5*(pass/4%2),z0=.5*(pass/2%2);
                auto hit=traceBounds({x0,y0,z0,x0+.5,y0+.5,z0+.5},x,y,z,state,startX,startY,startZ,ex,ey,ez);
                if(hit.hit){const double dx=hit.hitX-ex,dy=hit.hitY-ey,dz=hit.hitZ-ez,distance=dx*dx+dy*dy+dz*dz;
                    if(distance>farthest){best=hit;farthest=distance;}}
            }
            return best;
        }
        return traceBounds(rayBoundsLocal(x,y,z),x,y,z,state,startX,startY,startZ,ex,ey,ez);
    };
    BlockState state=getBlock(bx,by,bz);
    if(allowed(state)&&canRay(state)){auto hit=blockTrace(bx,by,bz,state,sx,sy,sz);if(hit.hit)return hit;}
    BlockRayHit last;
    for(int remaining=200;remaining>=0;--remaining) {
        if(bx==endX&&by==endY&&bz==endZ)return returnLastMiss?last:BlockRayHit{};
        bool stepX=true,stepY=true,stepZ=true;double edgeX=999,edgeY=999,edgeZ=999;
        if(endX>bx)edgeX=double(bx)+1;else if(endX<bx)edgeX=double(bx);else stepX=false;
        if(endY>by)edgeY=double(by)+1;else if(endY<by)edgeY=double(by);else stepY=false;
        if(endZ>bz)edgeZ=double(bz)+1;else if(endZ<bz)edgeZ=double(bz);else stepZ=false;
        const double dx=ex-sx,dy=ey-sy,dz=ez-sz;
        double tx=999,ty=999,tz=999;if(stepX)tx=(edgeX-sx)/dx;if(stepY)ty=(edgeY-sy)/dy;if(stepZ)tz=(edgeZ-sz)/dz;
        if(tx==-0.0)tx=-1e-4;
        if(ty==-0.0)ty=-1e-4;
        if(tz==-0.0)tz=-1e-4;
        int face;
        if(tx<ty&&tx<tz){face=endX>bx?4:5;sx=edgeX;sy+=dy*tx;sz+=dz*tx;}
        else if(ty<tz){face=endY>by?0:1;sx+=dx*ty;sy=edgeY;sz+=dz*ty;}
        else{face=endZ>bz?2:3;sx+=dx*tz;sy+=dy*tz;sz=edgeZ;}
        bx=floorBlock(sx)-(face==5?1:0);by=floorBlock(sy)-(face==1?1:0);bz=floorBlock(sz)-(face==3?1:0);
        state=getBlock(bx,by,bz);
        if(allowed(state)) {
            if(canRay(state)){auto hit=blockTrace(bx,by,bz,state,sx,sy,sz);if(hit.hit)return hit;}
            else last={false,true,bx,by,bz,face,sx,sy,sz,state};
        }
    }
    return returnLastMiss?last:BlockRayHit{};
}

void World::blockCollisionBoxes(int x, int y, int z, std::vector<Aabb>& out) const {
    const BlockState state = getBlock(x, y, z);
    const unsigned id = blockId(state), meta = blockMeta(state);
    if (noCollision(id)) return;
    const auto box = [&](double x0, double y0, double z0, double x1, double y1, double z1) {
        out.push_back({x + x0, y + y0, z + z0, x + x1, y + y1, z + z1});
    };
    if (id == 44 || id == 126 || id == 182) {
        box(0, (meta & 8) ? .5 : 0, 0, 1, (meta & 8) ? 1 : .5, 1);
        return;
    }
    if (stairs(id)) {
        // BlockStairs.func_176306_h / func_176304_i. Directions in legacy metadata
        // are east, west, south, north. The front stair can cut an outer corner;
        // only when it did not do so can the rear stair add an inner quarter.
        const unsigned facing = meta & 3;
        const bool top = (meta & 4) != 0;
        constexpr int dx[] = {1, -1, 0, 0}, dz[] = {0, 0, 1, -1};
        const auto same = [&](int bx, int bz) {
            const auto other = getBlock(bx, y, bz);
            return stairs(blockId(other)) && (blockMeta(other) & 7) == (meta & 7);
        };
        const auto compatible = [&](BlockState other) {
            return stairs(blockId(other)) && (blockMeta(other) & 4) == (meta & 4);
        };
        auto half = [](unsigned direction) -> Aabb {
            switch (direction) {
            case 0: return {.5, 0, 0, 1, 1, 1};
            case 1: return {0, 0, 0, .5, 1, 1};
            case 2: return {0, 0, .5, 1, 1, 1};
            default: return {0, 0, 0, 1, 1, .5};
            }
        };
        const auto intersect = [](Aabb a, const Aabb& b) {
            a.minX = std::max(a.minX, b.minX); a.maxX = std::min(a.maxX, b.maxX);
            a.minZ = std::max(a.minZ, b.minZ); a.maxZ = std::min(a.maxZ, b.maxZ);
            return a;
        };
        const double y0 = top ? 0 : .5, y1 = top ? .5 : 1;
        box(0, top ? .5 : 0, 0, 1, top ? 1 : .5, 1);
        Aabb upper = half(facing);
        const auto front = getBlock(x + dx[facing], y, z + dz[facing]);
        const unsigned frontDir = blockMeta(front) & 3;
        bool outer = false;
        if (compatible(front) && (frontDir / 2 != facing / 2) &&
            !same(x - dx[frontDir], z - dz[frontDir])) {
            upper = intersect(upper, half(frontDir));
            outer = true;
        }
        box(upper.minX, y0, upper.minZ, upper.maxX, y1, upper.maxZ);
        if (!outer) {
            const auto back = getBlock(x - dx[facing], y, z - dz[facing]);
            const unsigned backDir = blockMeta(back) & 3;
            if (compatible(back) && (backDir / 2 != facing / 2) &&
                !same(x + dx[backDir], z + dz[backDir])) {
                upper = intersect(half(facing ^ 1), half(backDir));
                box(upper.minX, y0, upper.minZ, upper.maxX, y1, upper.maxZ);
            }
        }
        return;
    }
    if (fence(id) || id == 139) {
        const auto connects = [&](int bx, int bz) {
            const auto other = getBlock(bx, y, bz);
            const auto otherId = blockId(other);
            if (otherId == 166) return false;
            if (gate(otherId)) return true;
            if (id == 139 && otherId == 139) return true;
            if (fence(id) && fence(otherId) && ((id == 113) == (otherId == 113))) return true;
            return fullCube(other) && opaqueMaterial(otherId) && otherId != 86 && otherId != 91 && otherId != 103;
        };
        const bool north = connects(x, z - 1), south = connects(x, z + 1);
        const bool west = connects(x - 1, z), east = connects(x + 1, z);
        if (id == 139) {
            double x0 = west ? 0 : .25, x1 = east ? 1 : .75;
            double z0 = north ? 0 : .25, z1 = south ? 1 : .75;
            if (north && south && !west && !east) { x0 = .3125; x1 = .6875; }
            if (!north && !south && west && east) { z0 = .3125; z1 = .6875; }
            box(x0, 0, z0, x1, 1.5, z1);
        } else {
            if (north || south) box(.375, 0, north ? 0 : .375, .625, 1.5, south ? 1 : .625);
            if (west || east || (!north && !south))
                box(west ? 0 : .375, 0, .375, east ? 1 : .625, 1.5, .625);
        }
        return;
    }
    if (gate(id)) {
        if (!(meta & 4)) {
            if (meta & 1) box(.375, 0, 0, .625, 1.5, 1);
            else box(0, 0, .375, 1, 1.5, .625);
        }
        return;
    }
    if (door(id)) {
        const unsigned lower = (meta & 8) ? blockMeta(getBlock(x, y - 1, z)) : meta;
        const unsigned upper = (meta & 8) ? meta : blockMeta(getBlock(x, y + 1, z));
        const bool hinge = (upper & 1) != 0;
        const unsigned facing = lower & 3; // east, south, west, north
        if (!(lower & 4)) {
            switch (facing) {
            case 0: box(0, 0, 0, .1875, 1, 1); break;
            case 1: box(0, 0, 0, 1, 1, .1875); break;
            case 2: box(.8125, 0, 0, 1, 1, 1); break;
            case 3: box(0, 0, .8125, 1, 1, 1); break;
            }
        } else {
            switch (facing) {
            case 0: box(0, 0, hinge ? .8125 : 0, 1, 1, hinge ? 1 : .1875); break;
            case 1: box(hinge ? 0 : .8125, 0, 0, hinge ? .1875 : 1, 1, 1); break;
            case 2: box(0, 0, hinge ? 0 : .8125, 1, 1, hinge ? .1875 : 1); break;
            case 3: box(hinge ? .8125 : 0, 0, 0, hinge ? 1 : .1875, 1, 1); break;
            }
        }
        return;
    }
    if (pane(id)) {
        const auto connects = [&](int bx, int bz) {
            const auto other = getBlock(bx, y, bz);
            const auto oid = blockId(other);
            const bool fullBlock = registry::block(oid).fullBlock;
            return fullBlock || blockId(other) == 20 ||
                   blockId(other) == 95 || pane(blockId(other));
        };
        const bool n = connects(x, z - 1), s = connects(x, z + 1);
        const bool w = connects(x - 1, z), e = connects(x + 1, z);
        const bool any = n || s || w || e;
        if ((!w || !e) && any) {
            if (w) box(0, 0, .4375, .5, 1, .5625);
            else if (e) box(.5, 0, .4375, 1, 1, .5625);
        } else box(0, 0, .4375, 1, 1, .5625);
        if ((!n || !s) && any) {
            if (n) box(.4375, 0, 0, .5625, 1, .5);
            else if (s) box(.4375, 0, .5, .5625, 1, 1);
        } else box(.4375, 0, 0, .5625, 1, 1);
        return;
    }
    switch (id) {
    case 26: box(0, 0, 0, 1, .5625, 1); return;
    case 29: case 33:
        if (meta & 8) {
            switch (meta & 7) {
            case 0: box(0, .25, 0, 1, 1, 1); return;
            case 1: box(0, 0, 0, 1, .75, 1); return;
            case 2: box(0, 0, .25, 1, 1, 1); return;
            case 3: box(0, 0, 0, 1, 1, .75); return;
            case 4: box(.25, 0, 0, 1, 1, 1); return;
            case 5: box(0, 0, 0, .75, 1, 1); return;
            }
        }
        break;
    case 34:
        switch (meta & 7) {
        case 0: box(0, 0, 0, 1, .25, 1); box(.375, .25, .375, .625, 1, .625); break;
        case 1: box(0, .75, 0, 1, 1, 1); box(.375, 0, .375, .625, .75, .625); break;
        case 2: box(0, 0, 0, 1, 1, .25); box(.25, .375, .25, .75, .625, 1); break;
        case 3: box(0, 0, .75, 1, 1, 1); box(.25, .375, 0, .75, .625, .75); break;
        case 4: box(0, 0, 0, .25, 1, 1); box(.375, .25, .25, .625, .75, 1); break;
        case 5: box(.75, 0, 0, 1, 1, 1); box(0, .375, .25, .75, .625, .75); break;
        default: break;
        }
        return;
    case 54: case 146: {
        double x0 = .0625, x1 = .9375, z0 = .0625, z1 = .9375;
        if (blockId(getBlock(x, y, z - 1)) == id) z0 = 0;
        else if (blockId(getBlock(x, y, z + 1)) == id) z1 = 1;
        else if (blockId(getBlock(x - 1, y, z)) == id) x0 = 0;
        else if (blockId(getBlock(x + 1, y, z)) == id) x1 = 1;
        box(x0, 0, z0, x1, .875, z1); return;
    }
    case 65:
        switch (meta % 6) {
        case 3: box(0, 0, 0, 1, 1, .125); break;
        case 4: box(.875, 0, 0, 1, 1, 1); break;
        case 5: box(0, 0, 0, .125, 1, 1); break;
        default: box(0, 0, .875, 1, 1, 1); break;
        }
        return;
    case 78: box(0, 0, 0, 1, (meta & 7) * .125, 1); return;
    case 81:
        // The original explicitly adds world coordinates as floats here.
        out.push_back({static_cast<double>(static_cast<float>(x) + .0625f), static_cast<double>(y),
                       static_cast<double>(static_cast<float>(z) + .0625f),
                       static_cast<double>(static_cast<float>(x + 1) - .0625f),
                       static_cast<double>(static_cast<float>(y + 1) - .0625f),
                       static_cast<double>(static_cast<float>(z + 1) - .0625f)});
        return;
    case 88: box(0, 0, 0, 1, .875, 1); return;
    case 92: box((1 + std::min(meta, 6u) * 2) / 16.0, 0, .0625, .9375, .5, .9375); return;
    case 93: case 94: case 149: case 150: box(0, 0, 0, 1, .125, 1); return;
    case 96: case 167:
        if (!(meta & 4)) box(0, (meta & 8) ? .8125 : 0, 0, 1, (meta & 8) ? 1 : .1875, 1);
        else switch (meta & 3) {
        case 0: box(0, 0, .8125, 1, 1, 1); break;
        case 1: box(0, 0, 0, 1, 1, .1875); break;
        case 2: box(.8125, 0, 0, 1, 1, 1); break;
        case 3: box(0, 0, 0, .1875, 1, 1); break;
        }
        return;
    case 111: box(0, 0, 0, 1, .015625, 1); return;
    case 116: box(0, 0, 0, 1, .75, 1); return;
    case 117:
        box(.4375, 0, .4375, .5625, .875, .5625);
        box(0, 0, 0, 1, .125, 1); return;
    case 118: case 154: {
        const double wall = id == 118 ? .125 : .125;
        box(0, 0, 0, 1, id == 118 ? .3125 : .625, 1);
        box(0, 0, 0, wall, 1, 1); box(0, 0, 0, 1, 1, wall);
        box(1 - wall, 0, 0, 1, 1, 1); box(0, 0, 1 - wall, 1, 1, 1); return;
    }
    case 120:
        box(0, 0, 0, 1, .8125, 1);
        if (meta & 4) box(.3125, .8125, .3125, .6875, 1, .6875);
        return;
    case 122: box(.0625, 0, .0625, .9375, 1, .9375); return;
    case 127: {
        const unsigned age = std::min(meta >> 2, 2u);
        const double width = 4 + age * 2, height = 5 + age * 2;
        const double lo = (8 - width / 2) / 16, hi = (8 + width / 2) / 16;
        const double bottom = (12 - height) / 16;
        switch (meta & 3) {
        case 0: box(lo, bottom, (15 - width) / 16, hi, .75, .9375); break;
        case 1: box(.0625, bottom, lo, (1 + width) / 16, .75, hi); break;
        case 2: box(lo, bottom, .0625, hi, .75, (1 + width) / 16); break;
        case 3: box((15 - width) / 16, bottom, lo, .9375, .75, hi); break;
        }
        return;
    }
    case 130: box(.0625, 0, .0625, .9375, .875, .9375); return;
    case 140: box(.3125, 0, .3125, .6875, .375, .6875); return;
    case 144:
        switch (meta & 7) {
        case 2: box(.25, .25, .5, .75, .75, 1); break;
        case 3: box(.25, .25, 0, .75, .75, .5); break;
        case 4: box(.5, .25, .25, 1, .75, .75); break;
        case 5: box(0, .25, .25, .5, .75, .75); break;
        default: box(.25, 0, .25, .75, .5, .75); break;
        }
        return;
    case 145:
        if (meta & 1) box(0, 0, .125, 1, 1, .875);
        else box(.125, 0, 0, .875, 1, 1);
        return;
    case 151: case 178: box(0, 0, 0, 1, .375, 1); return;
    case 171: box(0, 0, 0, 1, .0625, 1); return;
    default: break;
    }
    box(0, 0, 0, 1, 1, 1);
}

std::vector<Aabb> World::collisions(const Aabb& bounds, bool borderCollision) const {
    std::vector<Aabb> result, blockBoxes;
    const int x0 = floorBlock(bounds.minX), x1 = floorBlock(bounds.maxX + 1.0);
    const int bottom = floorBlock(bounds.minY);
    const int y0 = borderCollision ? bottom - 1 : bottom <= 0 ? 0 : bottom - 1;
    const int y1 = borderCollision ? floorBlock(bounds.maxY + 1.0) : std::min(256, floorBlock(bounds.maxY + 1.0));
    const int z0 = floorBlock(bounds.minZ), z1 = floorBlock(bounds.maxZ + 1.0);
    // An untrusted velocity must not turn collision lookup into an unbounded loop.
    if (static_cast<std::int64_t>(x1) - x0 > 1024 || static_cast<std::int64_t>(z1) - z0 > 1024)
        throw std::out_of_range("collision query exceeds supported movement extent");
    result.reserve(32); blockBoxes.reserve(5);
    // Preserve World.getCollidingBoundingBoxes' X, Z, Y iteration order.
    for (int bx = x0; bx < x1; ++bx) for (int bz = z0; bz < z1; ++bz) {
        if (!hasChunk(floorDiv16(bx), floorDiv16(bz))) continue;
        const bool borderStone = borderCollision && !(bx + 1.0 > border_.minX && bx < border_.maxX &&
                                                      bz + 1.0 > border_.minZ && bz < border_.maxZ);
        for (int by = y0; by < y1; ++by) {
            blockBoxes.clear();
            if (borderStone) blockBoxes.push_back({static_cast<double>(bx), static_cast<double>(by), static_cast<double>(bz), bx + 1.0, by + 1.0, bz + 1.0});
            else blockCollisionBoxes(bx, by, bz, blockBoxes);
            for (const auto& box : blockBoxes) if (box.intersects(bounds)) result.push_back(box);
        }
    }
    return result;
}

void World::generatePreview(int radiusChunks, std::uint64_t seed) {
    clear();
    setWorldBorder(0, 0, 60000000.0);
    radiusChunks = std::clamp(radiusChunks, 1, 32);
    // Separate offline fixture: flat spawn court, gently varying surrounding hills,
    // streams, trees and a small course for inspecting legacy collision shapes.
    for (int cz = -radiusChunks; cz <= radiusChunks; ++cz) for (int cx = -radiusChunks; cx <= radiusChunks; ++cx) {
        auto& c = ensureChunk(cx, cz);
        c.hasSkyLight = c.hasBiomes = true;
        c.biomes.fill(1);
        const auto put = [&](int lx, int by, int lz, BlockState state) {
            if (by < 0 || by >= 256) return;
            if (!c.sections[by >> 4]) c.sections[by >> 4] = std::make_unique<ChunkSection>();
            c.sections[by >> 4]->blocks[sectionIndex(lx, by, lz)] = state;
        };
        for (int lz = 0; lz < 16; ++lz) for (int lx = 0; lx < 16; ++lx) {
            const int wx = cx * 16 + lx, wz = cz * 16 + lz;
            const double hills = std::sin(wx * .044) * 4 + std::cos(wz * .057) * 3 + std::sin((wx + wz) * .075) * 2;
            const int ground = std::abs(wx) < 18 && std::abs(wz) < 18 ? 63 : 63 + static_cast<int>(hills);
            for (int by = 0; by <= ground; ++by)
                put(lx, by, lz, blockState(by == 0 ? 7 : by == ground ? (ground < 61 ? 12 : 2) : by >= ground - 3 ? 3 : 1));
            for (int by = ground + 1; by <= 61; ++by) put(lx, by, lz, blockState(9));
            if ((std::abs(wx) > 20 || std::abs(wz) > 20) && ground >= 62 && lx > 2 && lx < 13 && lz > 2 && lz < 13 &&
                mix(key(wx, wz) ^ seed) % 131 == 0) {
                for (int by = ground + 1; by <= ground + 5; ++by) put(lx, by, lz, blockState(17));
                for (int dy = 3; dy <= 6; ++dy) for (int dx = -2; dx <= 2; ++dx) for (int dz = -2; dz <= 2; ++dz) {
                    if (std::abs(dx) + std::abs(dz) < (dy == 6 ? 3 : 5) && (dx || dz || dy == 6))
                        put(lx + dx, ground + dy, lz + dz, blockState(18));
                }
            }
        }
    }
    // A path and distinct samples, kept outside the centered spawn area.
    for (int z = -16; z <= 16; ++z) for (int x = -2; x <= 2; ++x) setBlock(x, 63, z, blockState(98));
    for (int i = 0; i < 8; ++i) {
        setBlock(6 + i, 64, 5, blockState(35, i * 2));
        setBlock(6 + i, 64, 8, blockState(i & 1 ? 44 : 53, i & 1 ? 0 : 2));
        setBlock(-8, 64, i + 3, blockState(85));
    }
    int lightingHeight = 0;
    for (auto& entry : chunks_) {
        auto& c = entry.second;
        for (int lz = 0; lz < 16; ++lz) for (int lx = 0; lx < 16; ++lx) updateHeight(c, lx, lz);
        for (const auto height : c.heightMap) lightingHeight = std::max(lightingHeight, static_cast<int>(height) + 1);
    }
    lightingHeight = std::min(lightingHeight, 256);
    const int side = (radiusChunks * 2 + 1) * 16, base = -radiusChunks * 16;
    const std::size_t plane = static_cast<std::size_t>(side) * side;
    std::vector<std::uint8_t> light(plane * lightingHeight), opacity(light.size());
    const auto lightIndex = [=](int lx, int by, int lz) {
        return static_cast<std::size_t>(by) * plane + static_cast<std::size_t>(lz) * side + lx;
    };
    const auto previewOpacity = [](BlockState state) -> unsigned {
        return static_cast<unsigned>(std::min(15, registry::block(blockId(state)).lightOpacity));
    };
    std::vector<std::uint32_t> queue;
    queue.reserve(light.size());
    // Direct vertical sky reproduces the leaf/water attenuation used by the
    // vanilla lighting pass. The subsequent flood fills canopy and overhang shade.
    for (int lz = 0; lz < side; ++lz) for (int lx = 0; lx < side; ++lx) {
        unsigned sky = 15;
        for (int by = lightingHeight - 1; by >= 0; --by) {
            const auto index = lightIndex(lx, by, lz);
            const unsigned op = previewOpacity(getBlock(base + lx, by, base + lz));
            opacity[index] = static_cast<std::uint8_t>(op);
            const unsigned loss = op == 0 && sky != 15 ? 1 : op;
            sky = sky > loss ? sky - loss : 0;
            light[index] = static_cast<std::uint8_t>(sky);
            if (sky > 1) queue.push_back(static_cast<std::uint32_t>(index));
        }
    }
    const auto spread = [&](std::size_t target, unsigned from) {
        const unsigned loss = std::max(1u, static_cast<unsigned>(opacity[target]));
        const unsigned next = from > loss ? from - loss : 0;
        if (next > light[target]) {
            light[target] = static_cast<std::uint8_t>(next);
            if (next > 1) queue.push_back(static_cast<std::uint32_t>(target));
        }
    };
    for (std::size_t head = 0; head < queue.size(); ++head) {
        const std::size_t index = queue[head];
        const unsigned value = light[index];
        const int lx = static_cast<int>(index % side), lz = static_cast<int>(index / side % side);
        if (lx > 0) spread(index - 1, value);
        if (lx + 1 < side) spread(index + 1, value);
        if (lz > 0) spread(index - side, value);
        if (lz + 1 < side) spread(index + side, value);
        if (index >= plane) spread(index - plane, value);
        if (index + plane < light.size()) spread(index + plane, value);
    }
    for (auto& entry : chunks_) {
        auto& c = entry.second;
        for (int si = 0; si < 16; ++si) if (c.sections[si]) {
            auto& section = *c.sections[si];
            for (int ly = 0; ly < 16; ++ly) for (int lz = 0; lz < 16; ++lz) for (int lx = 0; lx < 16; ++lx) {
                const unsigned index = sectionIndex(lx, ly, lz), shift = (index & 1) * 4;
                const int by = si * 16 + ly;
                const unsigned sky = by >= lightingHeight ? 15 : light[lightIndex(c.x * 16 + lx - base, by, c.z * 16 + lz - base)];
                section.skyLight[index >> 1] = static_cast<std::uint8_t>((section.skyLight[index >> 1] & ~(15u << shift)) | (sky << shift));
            }
        }
        c.revision = ++revision_;
    }
}

Player::Player() { teleport(x, y, z); }
void Player::teleport(double feetX, double feetY, double feetZ) {
    if (!std::isfinite(feetX) || !std::isfinite(feetY) || !std::isfinite(feetZ) ||
        std::abs(feetX) > 32000000 || std::abs(feetZ) > 32000000 || std::abs(feetY) > 32000000)
        throw std::invalid_argument("non-finite or out-of-range player position");
    x = previousX = feetX;
    y = previousY = feetY;
    z = previousZ = feetZ;
    const double halfWidth = static_cast<double>(0.6f / 2.0f);
    bounds_ = {x - halfWidth, y, z - halfWidth, x + halfWidth, y + static_cast<double>(1.8f), z + halfWidth};
}
void Player::setSprinting(bool value) {
    sprinting = value;
    sprintTicksLeft_ = value ? 600 : 0;
}
std::vector<Aabb> Player::collisionBoxes(const World& world, const Aabb& query) {
    const auto& border = world.border();
    const double margin = outsideBorder ? 1.0 : -1.0;
    const bool inside = x > border.minX + margin && x < border.maxX - margin &&
                        z > border.minZ + margin && z < border.maxZ - margin;
    // World updates this flag while visiting loaded columns. All ordinary
    // player queries include at least the already-loaded player column.
    if (world.hasChunk(floorDiv16(floorBlock(x)), floorDiv16(floorBlock(z)))) outsideBorder = !inside;
    return world.collisions(query, inside);
}
void Player::moveFlying(float strafe, float forward, float acceleration) {
    float magnitude = strafe * strafe + forward * forward;
    if (magnitude >= 1.0e-4f) {
        magnitude = static_cast<float>(std::sqrt(static_cast<double>(magnitude)));
        if (magnitude < 1.0f) magnitude = 1.0f;
        magnitude = acceleration / magnitude;
        strafe = strafe * magnitude;
        forward = forward * magnitude;
        const float sine = javaSin(yaw * 3.1415927f / 180.0f);
        const float cosine = javaCos(yaw * 3.1415927f / 180.0f);
        motionX += static_cast<double>(strafe * cosine - forward * sine);
        motionZ += static_cast<double>(forward * cosine + strafe * sine);
    }
}
void Player::jump() {
    jumpedLastTick = true;
    motionY = static_cast<double>(0.42f);
    if (jumpBoostAmplifier >= 0) motionY += static_cast<double>(static_cast<float>(jumpBoostAmplifier + 1) * .1f);
    if (sprinting) {
        const float angle = yaw * .017453292f;
        motionX -= static_cast<double>(javaSin(angle) * .2f);
        motionZ += static_cast<double>(javaCos(angle) * .2f);
    }
}
bool Player::onLadder(const World& world) const {
    const auto id = blockId(world.getBlock(floorBlock(x), floorBlock(bounds_.minY), floorBlock(z)));
    return (id == 65 || id == 106) && !spectator;
}
void Player::pushOutOfBlocks(const World& world, double atX, double atY, double atZ) {
    if (noClip) return;
    const int bx = floorBlock(atX), by = floorBlock(atY), bz = floorBlock(atZ);
    const auto open = [&](int qx, int qz) {
        return !normalCube(world.getBlock(qx, by, qz)) && !normalCube(world.getBlock(qx, by + 1, qz));
    };
    if (open(bx, bz)) return;
    const double localX = atX - bx, localZ = atZ - bz;
    int direction = -1;
    double closest = 9999.0;
    if (open(bx - 1, bz) && localX < closest) { closest = localX; direction = 0; }
    if (open(bx + 1, bz) && 1.0 - localX < closest) { closest = 1.0 - localX; direction = 1; }
    if (open(bx, bz - 1) && localZ < closest) { closest = localZ; direction = 4; }
    if (open(bx, bz + 1) && 1.0 - localZ < closest) { direction = 5; }
    if (direction == 0) motionX = static_cast<double>(-.1f);
    if (direction == 1) motionX = static_cast<double>(.1f);
    if (direction == 4) motionZ = static_cast<double>(-.1f);
    if (direction == 5) motionZ = static_cast<double>(.1f);
}

void Player::move(const World& world, double dx, double dy, double dz) {
    if (!std::isfinite(dx) || !std::isfinite(dy) || !std::isfinite(dz) ||
        std::abs(dx) > 512 || std::abs(dy) > 512 || std::abs(dz) > 512)
        throw std::invalid_argument("unsupported player displacement");
    if (noClip) {
        bounds_ = bounds_.offset(dx, dy, dz);
        x = (bounds_.minX + bounds_.maxX) / 2.0;
        y = bounds_.minY;
        z = (bounds_.minZ + bounds_.maxZ) / 2.0;
        return;
    }
    if (inWeb) {
        inWeb = false;
        dx *= .25;
        dy *= .05000000074505806;
        dz *= .25;
        motionX = motionY = motionZ = 0;
    }
    double originalX = dx, originalZ = dz;
    const double originalY = dy;
    const bool sneakOnGround = onGround && sneaking;
    if (sneakOnGround) {
        const auto reduce = [](double v) {
            return v < .05 && v >= -.05 ? 0.0 : v > 0.0 ? v - .05 : v + .05;
        };
        while (dx != 0.0 && collisionBoxes(world, bounds_.offset(dx, -1.0, 0)).empty()) {
            dx = reduce(dx); originalX = dx;
        }
        while (dz != 0.0 && collisionBoxes(world, bounds_.offset(0, -1.0, dz)).empty()) {
            dz = reduce(dz); originalZ = dz;
        }
        while (dx != 0.0 && dz != 0.0 && collisionBoxes(world, bounds_.offset(dx, -1.0, dz)).empty()) {
            dx = reduce(dx); originalX = dx;
            dz = reduce(dz); originalZ = dz;
        }
    }
    const auto list = collisionBoxes(world, bounds_.addCoord(dx, dy, dz));
    const Aabb startingBounds = bounds_;
    for (const auto& obstacle : list) dy = obstacle.clipY(bounds_, dy);
    bounds_ = bounds_.offset(0, dy, 0);
    const bool canStep = onGround || (originalY != dy && originalY < 0.0);
    for (const auto& obstacle : list) dx = obstacle.clipX(bounds_, dx);
    bounds_ = bounds_.offset(dx, 0, 0);
    for (const auto& obstacle : list) dz = obstacle.clipZ(bounds_, dz);
    bounds_ = bounds_.offset(0, 0, dz);

    if (stepHeight > 0.0f && canStep && (originalX != dx || originalZ != dz)) {
        const double normalX = dx, normalY = dy, normalZ = dz;
        const Aabb normalBounds = bounds_;
        bounds_ = startingBounds;
        dy = static_cast<double>(stepHeight);
        const auto stepList = collisionBoxes(world, bounds_.addCoord(originalX, dy, originalZ));
        Aabb pathA = bounds_;
        const Aabb sweptA = pathA.addCoord(originalX, 0, originalZ);
        double upA = dy;
        for (const auto& obstacle : stepList) upA = obstacle.clipY(sweptA, upA);
        pathA = pathA.offset(0, upA, 0);
        double ax = originalX;
        for (const auto& obstacle : stepList) ax = obstacle.clipX(pathA, ax);
        pathA = pathA.offset(ax, 0, 0);
        double az = originalZ;
        for (const auto& obstacle : stepList) az = obstacle.clipZ(pathA, az);
        pathA = pathA.offset(0, 0, az);

        Aabb pathB = bounds_;
        double upB = dy;
        for (const auto& obstacle : stepList) upB = obstacle.clipY(pathB, upB);
        pathB = pathB.offset(0, upB, 0);
        double bx = originalX;
        for (const auto& obstacle : stepList) bx = obstacle.clipX(pathB, bx);
        pathB = pathB.offset(bx, 0, 0);
        double bz = originalZ;
        for (const auto& obstacle : stepList) bz = obstacle.clipZ(pathB, bz);
        pathB = pathB.offset(0, 0, bz);

        if (ax * ax + az * az > bx * bx + bz * bz) {
            dx = ax; dz = az; dy = -upA; bounds_ = pathA;
        } else {
            dx = bx; dz = bz; dy = -upB; bounds_ = pathB;
        }
        for (const auto& obstacle : stepList) dy = obstacle.clipY(bounds_, dy);
        bounds_ = bounds_.offset(0, dy, 0);
        if (normalX * normalX + normalZ * normalZ >= dx * dx + dz * dz) {
            dx = normalX; dy = normalY; dz = normalZ; bounds_ = normalBounds;
        }
    }

    x = (bounds_.minX + bounds_.maxX) / 2.0;
    y = bounds_.minY;
    z = (bounds_.minZ + bounds_.maxZ) / 2.0;
    collidedHorizontally = originalX != dx || originalZ != dz;
    collidedVertically = originalY != dy;
    onGround = collidedVertically && originalY < 0.0;
    const int footX = floorBlock(x), footY = floorBlock(y - .20000000298023224), footZ = floorBlock(z);
    auto groundId = blockId(world.getBlock(footX, footY, footZ));
    if (groundId == 0) {
        const auto lower = blockId(world.getBlock(footX, footY - 1, footZ));
        if (fence(lower) || lower == 139 || gate(lower)) groundId = lower;
    }
    // EntityLivingBase.updateFallState rechecks water after movement when the
    // entity was dry, before Entity.updateFallState adds this tick's fall delta.
    if (!inWater) updateWater(world);
    if (onGround) fallDistance = 0.0f;
    else if (dy < 0.0) fallDistance = static_cast<float>(static_cast<double>(fallDistance) - dy);
    if (originalX != dx) motionX = 0.0;
    if (originalZ != dz) motionZ = 0.0;
    if (originalY != dy) {
        if (groundId == 165 && !sneaking) {
            if (motionY < 0.0) motionY = -motionY;
        } else motionY = 0.0;
    }
    // BlockSlime's walking hook is distinct from its intersecting-block hook.
    if (!sneakOnGround && onGround && groundId == 165 && std::abs(motionY) < .1 && !sneaking) {
        const double multiplier = .4 + std::abs(motionY) * .2;
        motionX *= multiplier; motionZ *= multiplier;
    }
    const int x0 = floorBlock(bounds_.minX + .001), x1 = floorBlock(bounds_.maxX - .001);
    const int y0 = std::max(0, floorBlock(bounds_.minY + .001)), y1 = std::min(255, floorBlock(bounds_.maxY - .001));
    const int z0 = floorBlock(bounds_.minZ + .001), z1 = floorBlock(bounds_.maxZ - .001);
    bool loaded = true;
    for (int cx = floorDiv16(x0); cx <= floorDiv16(x1); ++cx)
        for (int cz = floorDiv16(z0); cz <= floorDiv16(z1); ++cz) loaded = loaded && world.hasChunk(cx, cz);
    if (loaded) for (int bx = x0; bx <= x1; ++bx) for (int by = y0; by <= y1; ++by) for (int bz = z0; bz <= z1; ++bz) {
        const auto id = blockId(world.getBlock(bx, by, bz));
        if (id == 30) { inWeb = true; fallDistance = 0; }
        if (id == 88) { motionX *= .4; motionZ *= .4; }
        if (id == 36) warnings |= PhysicsMovingPiston;
        if (id > 197) warnings |= PhysicsUnknownBlock;
    }
    else warnings |= PhysicsMissingChunks;
}

namespace {
struct FlowVector {
    double x{}, y{}, z{};
    FlowVector normalized() const {
        const double length = static_cast<double>(static_cast<float>(std::sqrt(x * x + y * y + z * z)));
        return length < 1.0e-4 ? FlowVector{} : FlowVector{x / length, y / length, z / length};
    }
};

FlowVector waterFlow(const World& world, int x, int y, int z) {
    const auto decay = [&](int bx, int by, int bz) {
        const auto state = world.getBlock(bx, by, bz);
        return water(blockId(state)) ? (blockMeta(state) >= 8 ? 0 : static_cast<int>(blockMeta(state))) : -1;
    };
    const int level = decay(x, y, z);
    constexpr int dx[] = {0, 1, 0, -1}, dz[] = {-1, 0, 1, 0};
    FlowVector flow;
    for (int direction = 0; direction < 4; ++direction) {
        const int bx = x + dx[direction], bz = z + dz[direction];
        int neighbor = decay(bx, y, bz);
        if (neighbor < 0) {
            if (!materialBlocksMovement(blockId(world.getBlock(bx, y, bz)))) {
                neighbor = decay(bx, y - 1, bz);
                if (neighbor >= 0) {
                    const int difference = neighbor - (level - 8);
                    flow.x += dx[direction] * difference;
                    flow.z += dz[direction] * difference;
                }
            }
        } else {
            flow.x += dx[direction] * (neighbor - level);
            flow.z += dz[direction] * (neighbor - level);
        }
    }
    if (blockMeta(world.getBlock(x, y, z)) >= 8) {
        const auto solid = [&](int bx, int by, int bz) {
            const auto id = blockId(world.getBlock(bx, by, bz));
            return id != 79 && !water(id) && registry::block(id).materialSolid;
        };
        for (int direction = 0; direction < 4; ++direction) {
            const int bx = x + dx[direction], bz = z + dz[direction];
            if (solid(bx, y, bz) || solid(bx, y + 1, bz)) {
                flow = flow.normalized(); flow.y -= 6.0; break;
            }
        }
    }
    return flow.normalized();
}

bool containsLiquid(const World& world, const Aabb& bounds, bool onlyLava) {
    for (int bx = floorBlock(bounds.minX); bx <= floorBlock(bounds.maxX); ++bx)
        for (int by = std::max(0, floorBlock(bounds.minY)); by <= std::min(255, floorBlock(bounds.maxY)); ++by)
            for (int bz = floorBlock(bounds.minZ); bz <= floorBlock(bounds.maxZ); ++bz) {
                const auto id = blockId(world.getBlock(bx, by, bz));
                if (lava(id) || (!onlyLava && water(id))) return true;
            }
    return false;
}
} // namespace

WaterAcceleration World::waterAcceleration(const Aabb& query) const {
    const int x0=floorBlock(query.minX),x1=floorBlock(query.maxX+1.0);
    const int rawY0=floorBlock(query.minY),rawY1=floorBlock(query.maxY+1.0);
    const int z0=floorBlock(query.minZ),z1=floorBlock(query.maxZ+1.0);
    WaterAcceleration result;
    if(rawY1<0||rawY0>=256)return result;
    for(int cx=floorDiv16(x0);cx<=floorDiv16(x1);++cx)
        for(int cz=floorDiv16(z0);cz<=floorDiv16(z1);++cz)if(!hasChunk(cx,cz))return result;
    result.loaded=true;FlowVector flow;
    for(int bx=x0;bx<x1;++bx)for(int by=std::max(0,rawY0);by<std::min(256,rawY1);++by)for(int bz=z0;bz<z1;++bz){
        const auto state=getBlock(bx,by,bz);
        if(water(blockId(state))){const unsigned level=blockMeta(state)>=8?0:blockMeta(state);
            const float liquidHeight=static_cast<float>(level+1)/9.0f;
            const double surface=static_cast<double>(static_cast<float>(by+1)-liquidHeight);
            if(static_cast<double>(rawY1)>=surface){result.inside=true;const auto value=waterFlow(*this,bx,by,bz);flow.x+=value.x;flow.y+=value.y;flow.z+=value.z;}
        }
    }
    result.hasFlow=static_cast<float>(std::sqrt(flow.x*flow.x+flow.y*flow.y+flow.z*flow.z))>0.0f;
    if(result.hasFlow){flow=flow.normalized();result.x=flow.x*.014;result.y=flow.y*.014;result.z=flow.z*.014;}
    return result;
}

void Player::updateWater(const World& world) {
    const Aabb wet = bounds_.expand(0, -.4000000059604645, 0).expand(-.001, -.001, -.001);
    const int x0 = floorBlock(wet.minX), x1 = floorBlock(wet.maxX + 1.0);
    const int y0 = std::max(0, floorBlock(wet.minY)), y1 = std::min(256, floorBlock(wet.maxY + 1.0));
    const int z0 = floorBlock(wet.minZ), z1 = floorBlock(wet.maxZ + 1.0);
    bool loaded = true;
    for (int cx = floorDiv16(x0); cx <= floorDiv16(x1); ++cx)
        for (int cz = floorDiv16(z0); cz <= floorDiv16(z1); ++cz) loaded = loaded && world.hasChunk(cx, cz);
    inWater = false;
    FlowVector acceleration;
    if (loaded) for (int bx = x0; bx < x1; ++bx) for (int by = y0; by < y1; ++by) for (int bz = z0; bz < z1; ++bz) {
        const auto state = world.getBlock(bx, by, bz);
        if (water(blockId(state))) {
            const unsigned level = blockMeta(state) >= 8 ? 0 : blockMeta(state);
            const float liquidHeight = static_cast<float>(level + 1) / 9.0f;
            const double surface = static_cast<double>(static_cast<float>(by + 1) - liquidHeight);
            if (static_cast<double>(y1) >= surface) {
                inWater = true;
                const auto flow = waterFlow(world, bx, by, bz);
                acceleration.x += flow.x; acceleration.y += flow.y; acceleration.z += flow.z;
            }
        }
    }
    if (!loaded) warnings |= PhysicsMissingChunks;
    if (inWater) {
        fallDistance = 0;
        if (!flying) {
            acceleration = acceleration.normalized();
            motionX += acceleration.x * .014;
            motionY += acceleration.y * .014;
            motionZ += acceleration.z * .014;
        }
    }
}

void Player::updateEnvironment(const World& world) {
    updateWater(world);
    inLava = containsLiquid(world, bounds_.expand(-.10000000149011612, -.4000000059604645, -.10000000149011612), true);
    if (inWater || inLava) warnings |= PhysicsFluidApproximation;
    const Aabb near = bounds_.expand(1, 1, 1);
    for (int bx = floorBlock(near.minX); bx <= floorBlock(near.maxX); ++bx)
        for (int by = std::max(0, floorBlock(near.minY)); by <= std::min(255, floorBlock(near.maxY)); ++by)
            for (int bz = floorBlock(near.minZ); bz <= floorBlock(near.maxZ); ++bz) {
                const auto id = blockId(world.getBlock(bx, by, bz));
                if (id == 36) warnings |= PhysicsMovingPiston;
                if (id > 197) warnings |= PhysicsUnknownBlock;
            }
}

void Player::travel(const World& world, float strafe, float forward) {
    // EntityPlayer.getAIMoveSpeed reads the attribute directly. Sprint is the
    // exact operation-2 modifier, not a decimal approximation of 30 percent.
    const float speed = static_cast<float>(std::clamp(movementSpeed * (sprinting ? 1.30000001192092896 : 1.0), 0.0, 1024.0));
    const double beforeY = y;
    const double savedFlyingY = motionY;
    const bool liquidTravel = (inWater || inLava) && !flying;
    if ((!inWater && !inLava) || flying) {
        float friction = .91f;
        if (onGround) friction = slipperiness(world.getBlock(floorBlock(x), floorBlock(bounds_.minY) - 1, floorBlock(z))) * .91f;
        const float scale = .16277136f / (friction * friction * friction);
        const float airAcceleration = flying ? flySpeed * static_cast<float>(sprinting ? 2 : 1) : jumpMovementFactor_;
        moveFlying(strafe, forward, onGround ? speed * scale : airAcceleration);
        friction = .91f;
        if (onGround) friction = slipperiness(world.getBlock(floorBlock(x), floorBlock(bounds_.minY) - 1, floorBlock(z))) * .91f;
        if (onLadder(world)) {
            motionX = std::clamp(motionX, static_cast<double>(-.15f), static_cast<double>(.15f));
            motionZ = std::clamp(motionZ, static_cast<double>(-.15f), static_cast<double>(.15f));
            fallDistance = 0;
            if (motionY < -.15) motionY = -.15;
            if (sneaking && motionY < 0) motionY = 0;
        }
        move(world, motionX, motionY, motionZ);
        if (collidedHorizontally && onLadder(world)) motionY = .2;
        // The reference intentionally truncates X/Z here, unlike BlockPos(double).
        if (!world.hasChunk(floorDiv16(static_cast<int>(x)), floorDiv16(static_cast<int>(z)))) {
            motionY = y > 0.0 ? -.1 : 0.0;
            warnings |= PhysicsMissingChunks;
        } else motionY -= .08;
        motionY *= .9800000190734863;
        motionX *= static_cast<double>(friction);
        motionZ *= static_cast<double>(friction);
    } else if (inWater) {
        float drag = .8f, acceleration = .02f;
        float depth = static_cast<float>(std::clamp(depthStriderLevel, 0, 3));
        if (!onGround) depth *= .5f;
        if (depth > 0) {
            drag += (.54600006f - drag) * depth / 3.0f;
            acceleration += (speed * 1.0f - acceleration) * depth / 3.0f;
        }
        moveFlying(strafe, forward, acceleration);
        move(world, motionX, motionY, motionZ);
        motionX *= static_cast<double>(drag);
        motionY *= .800000011920929;
        motionZ *= static_cast<double>(drag);
        motionY -= .02;
    } else {
        moveFlying(strafe, forward, .02f);
        move(world, motionX, motionY, motionZ);
        motionX *= .5; motionY *= .5; motionZ *= .5;
        motionY -= .02;
    }
    if (liquidTravel && collidedHorizontally) {
        const auto escape = bounds_.offset(motionX, motionY + .6000000238418579 - y + beforeY, motionZ);
        if (collisionBoxes(world, escape).empty() && !containsLiquid(world, escape, false)) motionY = .30000001192092896;
    }
    if (flying) motionY = savedFlyingY * .6;
}

void Player::tick(const World& world, const Input& input) {
    jumpedLastTick = false;
    warnings = PhysicsEntityInteractionsUnavailable;
    flightChangesLastTick.clear();
    // EntityPlayerSP.onUpdate skips all simulation in an unloaded current column.
    if (!world.hasChunk(floorDiv16(floorBlock(x)), floorDiv16(floorBlock(z)))) {
        warnings |= PhysicsMissingChunks;
        return;
    }
    if (x != (bounds_.minX + bounds_.maxX) / 2.0 || y != bounds_.minY || z != (bounds_.minZ + bounds_.maxZ) / 2.0)
        teleport(x, y, z);
    previousX = x; previousY = y; previousZ = z;
    if (spectator) { noClip = true; onGround = false; }
    updateEnvironment(world);
    if (sprintTicksLeft_ > 0 && --sprintTicksLeft_ == 0) setSprinting(false);
    if (sprintToggleTimer_ > 0) --sprintToggleTimer_;

    float strafe = std::isfinite(input.strafe) ? std::clamp(input.strafe, -1.0f, 1.0f) : 0.0f;
    float forward = std::isfinite(input.forward) ? std::clamp(input.forward, -1.0f, 1.0f) : 0.0f;
    if (input.sneak) {
        strafe = static_cast<float>(static_cast<double>(strafe) * .3);
        forward = static_cast<float>(static_cast<double>(forward) * .3);
    }
    if (input.usingItem) { strafe *= .2f; forward *= .2f; sprintToggleTimer_ = 0; }
    sneaking = input.sneak;
    const double push = static_cast<double>(.6f) * .35;
    pushOutOfBlocks(world, x - push, bounds_.minY + .5, z + push);
    pushOutOfBlocks(world, x - push, bounds_.minY + .5, z - push);
    pushOutOfBlocks(world, x + push, bounds_.minY + .5, z - push);
    pushOutOfBlocks(world, x + push, bounds_.minY + .5, z + push);
    const bool canSprint = foodLevel > 6 || allowFlying;
    if (onGround && !previousSneak_ && previousForward_ < .8f && forward >= .8f && !sprinting && canSprint && !input.usingItem && !blind) {
        if (sprintToggleTimer_ <= 0 && !input.sprint) sprintToggleTimer_ = 7;
        else setSprinting(true);
    }
    if (!sprinting && forward >= .8f && canSprint && !input.usingItem && !blind && input.sprint) setSprinting(true);
    if (sprinting && (forward < .8f || collidedHorizontally || !canSprint)) setSprinting(false);
    if (allowFlying) {
        if (spectator) {
            if (!flying) { flying = true; flightChangesLastTick.push_back(true); }
        }
        else if (!previousJump_ && input.jump) {
            if (flyToggleTimer_ == 0) flyToggleTimer_ = 7;
            else { flying = !flying; flightChangesLastTick.push_back(flying); flyToggleTimer_ = 0; }
        }
    }
    if (flying) {
        if (input.sneak) motionY -= static_cast<double>(flySpeed * 3.0f);
        if (input.jump) motionY += static_cast<double>(flySpeed * 3.0f);
    }
    if (flyToggleTimer_ > 0) --flyToggleTimer_;
    if (jumpTicks_ > 0) --jumpTicks_;
    if (std::abs(motionX) < .005) motionX = 0;
    if (std::abs(motionY) < .005) motionY = 0;
    if (std::abs(motionZ) < .005) motionZ = 0;
    const bool isJumping = input.jump && !dead;
    if (dead) { strafe = 0; forward = 0; }
    if (isJumping) {
        if (inWater || inLava) motionY += .03999999910593033;
        else if (onGround && jumpTicks_ == 0) { jump(); jumpTicks_ = 10; }
    } else jumpTicks_ = 0;
    travel(world, strafe * .98f, forward * .98f);
    inLava = containsLiquid(world, bounds_.expand(-.10000000149011612, -.4000000059604645, -.10000000149011612), true);
    jumpMovementFactor_ = speedInAir;
    if (sprinting) jumpMovementFactor_ = static_cast<float>(static_cast<double>(jumpMovementFactor_) + static_cast<double>(speedInAir) * .3);
    if (onGround && flying && !spectator) { flying = false; flightChangesLastTick.push_back(false); }
    previousForward_ = forward;
    previousJump_ = input.jump;
    previousSneak_ = input.sneak;
    ++ticks;
}

} // namespace mc

#include "world_lighting.inc"
