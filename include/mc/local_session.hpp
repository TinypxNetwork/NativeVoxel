#pragma once
#include "mc/simulation.hpp"
#include "mc/world_io.hpp"
#include <functional>
#include <memory>
#include <unordered_set>
namespace mc {
class LocalSession {
  public:
    Simulation simulation;
    worldio::SaveInfo saveInfo;
    bool active() const { return active_; }
    void detach(Game &);
    void attachPreview(Game &, int mode, const std::string &playerName);
    bool create(Game &, std::int64_t seed, worldgen::Settings, int radius, int mode, const std::string &playerName,
                std::string &error, const std::function<bool(int, int)> &progress = {});
    bool load(Game &, const std::string &directory, int radius, const std::string &playerName, std::string &error);
    bool saveNew(Game &, const std::string &directory, std::string &error);
    void tick(Game &, const Input &, int renderDistance);

  private:
    bool active_ = false;
    std::unique_ptr<worldgen::Generator> generator_;
    std::unordered_set<std::uint64_t> absentSavedChunks_;
    void attach(Game &);
    void stream(Game &, int radius);
};
} // namespace mc
