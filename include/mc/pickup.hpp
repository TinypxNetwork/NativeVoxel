#pragma once

#include "mc/game.hpp"

namespace mc {

struct PickupAnimationSample {
    const Entity* entity{}; // Owned by the tracker until its next update/reset.
    double x{},y{},z{};
};

// Client-only EntityPickupFX. The world removes collected entities at once;
// these snapshots are rendered for three ticks without running their physics.
class PickupAnimationTracker {
public:
    void update(Game&); // Drain pickupEvents; repeated calls in one tick do not age effects.
    std::vector<PickupAnimationSample> frame(float partialTicks) const;
    void reset();
    std::size_t size() const noexcept {return animations_.size();}
private:
    struct Animation {
        EntityPickupEvent event;
        int age{};
        bool localCollector{},detachedCollector{};
    };
    std::deque<Animation> animations_;
    std::uint64_t lastTick_{},generation_{};
    int dimension_{};
    bool initialized_{};
};

} // namespace mc
