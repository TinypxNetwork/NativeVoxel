// Controlled integration session for the user-authorized disposable server account.
// Credentials are read only from a local file; they never appear in process args.
#include "mc/game.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <map>
#include <thread>

using Clock=std::chrono::steady_clock;
int main(int argc,char** argv) {
    try {
        if(argc<3)throw std::runtime_error("Usage: server_probe account.ini capture.nvpc [seconds]");
        std::ifstream credentials(argv[1]);
        std::map<std::string,std::string> config;std::string line;
        while(std::getline(credentials,line)){if(!line.empty()&&line.back()=='\r')line.pop_back();auto at=line.find('=');if(at!=std::string::npos)config[line.substr(0,at)]=line.substr(at+1);}
        if(config["server"].empty()||config["username"].empty()||config["password"].size()<8)throw std::runtime_error("Local test credentials are incomplete");
        auto clean=[&](std::string text){for(auto p=text.find(config["password"]);p!=std::string::npos;p=text.find(config["password"]))text.replace(p,config["password"].size(),"[redacted]");return text;};
        int seconds=argc>3?std::stoi(argv[3]):30;
        int movementPerDirection=argc>4?std::clamp(std::stoi(argv[4]),0,20):0;
        for(int attempt=0;attempt<2;++attempt) {
            mc::Game game;
            std::ofstream capture(argv[2],std::ios::binary);capture.write("NVPC\1\0\0\0",8);
            game.incomingObserver=[&](const mc::net::Packet& packet){std::uint32_t id=packet.id,n=std::uint32_t(packet.data.size());capture.write(reinterpret_cast<const char*>(&id),4);capture.write(reinterpret_cast<const char*>(&n),4);capture.write(reinterpret_cast<const char*>(packet.data.data()),n);};
            if(!game.connect(config["server"],25565,config["username"]))throw std::runtime_error(clean(game.error));
            std::cout<<"Attempt "<<attempt+1<<": connected TCP\n"<<std::flush;
            auto start=Clock::now(),nextTick=start;bool sentRegistration=false,sentLogin=false;std::size_t seen=0;
            int movementTick=0;bool movementStarted=false;std::uint64_t correctionBaseline=0;
            std::string status;
            while(std::chrono::duration<double>(Clock::now()-start).count()<seconds) {
                game.poll();
                if(game.status!=status){status=game.status;std::cout<<"State: "<<clean(status)<<'\n'<<std::flush;}
                for(;seen<game.chat.size();++seen) {
                    auto text=game.chat[seen].text;
                    std::cout<<"Server: "<<clean(text)<<'\n'<<std::flush;
                    if(!sentRegistration&&(text.find("/register")!=std::string::npos)) {
                        if(!game.sendChat("/register "+config["password"]+" "+config["password"]))throw std::runtime_error("Registration command could not be sent");
                        sentRegistration=true;std::cout<<"Registration submitted (credentials withheld).\n"<<std::flush;
                    } else if(!sentLogin&&text.find("/login")!=std::string::npos) {
                        if(!game.sendChat("/login "+config["password"]))throw std::runtime_error("Login command could not be sent");
                        sentLogin=true;std::cout<<"Account login submitted (credentials withheld).\n"<<std::flush;
                    }
                }
                if(!game.error.empty())break;
                auto now=Clock::now();
                if(now>=nextTick){mc::Input input;
                    if(movementPerDirection&&game.dimension==0&&game.spawned&&game.world.chunks().size()>=100&&game.ticks>=100){
                        if(!movementStarted){movementStarted=true;correctionBaseline=game.positionCorrections;std::cout<<"Starting controlled short walk-and-return.\n"<<std::flush;}
                        if(movementTick<movementPerDirection)input.forward=1;
                        else if(movementTick<movementPerDirection*2)input.forward=-1;
                        ++movementTick;
                    }
                    game.tick(input);nextTick+=std::chrono::milliseconds(50);if(now-nextTick>std::chrono::milliseconds(500))nextTick=now;}
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            capture.flush();
            std::cout<<"Result joined="<<game.joined<<" spawned="<<game.spawned<<" chunks="<<game.world.chunks().size()<<" entities="<<game.entities.size()<<" dimension="<<game.dimension
                     <<" position="<<game.player.x<<','<<game.player.y<<','<<game.player.z<<" corrections="<<game.positionCorrections<<" received="<<game.packetsReceived<<" sent="<<game.packetsSent<<'\n';
            if(movementStarted)std::cout<<"Movement corrections="<<game.positionCorrections-correctionBaseline<<" scripted_input_ticks="<<std::min(movementTick,movementPerDirection*2)<<" physics_warnings="<<game.player.warnings<<'\n';
            std::string error=game.error;bool success=game.joined&&game.spawned&&error.empty();game.disconnect();
            if(success)return 0;
            if(attempt==0&&error.find("session is not configured")!=std::string::npos)continue;
            std::cerr<<clean(error)<<'\n';return 2;
        }
        return 2;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
