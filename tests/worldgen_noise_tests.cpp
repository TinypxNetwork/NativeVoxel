#include "mc/worldgen_noise.hpp"
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
std::string bits(double value){std::uint64_t raw;std::memcpy(&raw,&value,8);std::ostringstream out;out<<std::hex<<raw;return out.str();}
std::string bits(float value){std::uint32_t raw;std::memcpy(&raw,&value,4);std::ostringstream out;out<<std::hex<<raw;return out.str();}
void values(int id,const std::vector<double>& data){for(std::size_t i=0;i<data.size();++i)std::cout<<"N "<<id<<' '<<i<<' '<<bits(data[i])<<'\n';}
}
int main(int argc,char** argv){
    try{
        if(argc!=2)throw std::runtime_error("worldgen_noise_tests requires a fixture path");
        std::ifstream input(argv[1]);if(!input)throw std::runtime_error("cannot open noise fixture");
        std::string line;int id=0;
        while(std::getline(input,line)){
            if(line.empty()||line[0]=='#')continue;std::istringstream in(line);char kind;std::int64_t seed;in>>kind>>seed;mc::worldgen::JavaRandom random(seed);
            if(kind=='R'){
                int count;in>>count;const int bounds[]={1,2,3,16,17,1000,1073741825,2147483647};
                for(int i=0;i<count;++i){const auto a=random.nextInt(),b=random.nextInt(bounds[i%8]);const auto c=random.nextLong();const auto f=random.nextFloat();const auto d=random.nextDouble();const auto flag=random.nextBoolean();
                    std::vector<std::uint8_t> bytes(std::size_t(i%9));random.nextBytes(bytes.data(),bytes.size());std::ostringstream encoded;for(auto v:bytes)encoded<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(v);
                    std::cout<<"R "<<id<<' '<<i<<' '<<a<<' '<<b<<' '<<c<<' '<<bits(f)<<' '<<bits(d)<<' '<<flag<<' '<<(bytes.empty()?"-":encoded.str())<<'\n';}
            }else if(kind=='G'){int count;in>>count;for(int i=0;i<count;++i)std::cout<<"G "<<id<<' '<<i<<' '<<bits(random.nextGaussian())<<'\n';}
            else if(kind=='I'){
                double x,y,z,xs,ys,zs,scale;int nx,ny,nz;in>>x>>y>>z>>nx>>ny>>nz>>xs>>ys>>zs>>scale;
                mc::worldgen::NoiseImproved noise(random);std::vector<double> out(std::size_t(nx)*ny*nz,.125);
                noise.add(out.data(),x,y,z,nx,ny,nz,xs,ys,zs,scale);values(id,out);
            }else if(kind=='O'){
                int octaves,x,y,z,nx,ny,nz;double xs,ys,zs;in>>octaves>>x>>y>>z>>nx>>ny>>nz>>xs>>ys>>zs;
                mc::worldgen::NoiseOctaves noise(random,octaves);std::vector<double> out(std::size_t(nx)*ny*nz+3,7);
                noise.generate(out,x,y,z,nx,ny,nz,xs,ys,zs);values(id,out);
            }else if(kind=='S'){
                double x,z;int count;in>>x>>z>>count;mc::worldgen::NoiseSimplex noise(random);std::vector<double> out;
                for(int i=0;i<count;++i)out.push_back(noise.sample(x+double(i)*.125,z-double(i)*.0625));values(id,out);
            }else if(kind=='A'){
                double x,z,xs,zs,scale;int nx,nz;in>>x>>z>>nx>>nz>>xs>>zs>>scale;mc::worldgen::NoiseSimplex noise(random);std::vector<double> out(std::size_t(nx)*nz,.125);
                noise.add(out.data(),x,z,nx,nz,xs,zs,scale);values(id,out);
            }else if(kind=='P'){
                int octaves,nx,nz;double x,z,xs,zs,lacunarity,persistence;in>>octaves>>x>>z>>nx>>nz>>xs>>zs>>lacunarity>>persistence;
                mc::worldgen::NoisePerlin noise(random,octaves);std::vector<double> out(std::size_t(nx)*nz+3,8);noise.generate(out,x,z,nx,nz,xs,zs,lacunarity,persistence);values(id,out);
            }else if(kind=='V'){
                int octaves,count;double x,z;in>>octaves>>x>>z>>count;mc::worldgen::NoisePerlin noise(random,octaves);std::vector<double> out;
                for(int i=0;i<count;++i)out.push_back(noise.sample(x+double(i)*.125,z-double(i)*.0625));values(id,out);
            }else throw std::runtime_error("unknown noise fixture command");
            std::cout<<"T "<<id<<' '<<random.nextLong()<<'\n';++id;
        }
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
