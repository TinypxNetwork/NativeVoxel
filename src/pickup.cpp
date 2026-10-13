#include "mc/pickup.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace mc {

void PickupAnimationTracker::reset() {
    animations_.clear();initialized_=false;
}

void PickupAnimationTracker::update(Game& game) {
    if(!initialized_||generation_!=game.pickupGeneration||dimension_!=game.dimension||game.ticks<lastTick_) {
        animations_.clear();lastTick_=game.ticks;initialized_=true;
        generation_=game.pickupGeneration;dimension_=game.dimension;
    }
    const int elapsed=static_cast<int>(std::min<std::uint64_t>(3,game.ticks-lastTick_));
    for(auto& animation:animations_)animation.age+=elapsed;
    animations_.erase(std::remove_if(animations_.begin(),animations_.end(),[](const Animation& value){return value.age>=3;}),animations_.end());
    while(!game.pickupEvents.empty()) {
        auto event=std::move(game.pickupEvents.front());game.pickupEvents.pop_front();
        const auto age=game.ticks>=event.tick?game.ticks-event.tick:0;
        if(age>=3)continue;
        if(animations_.size()>=4000)animations_.pop_front();
        const bool localCollector=event.collectorId==game.entityId;
        animations_.push_back({std::move(event),static_cast<int>(age),localCollector,false});
    }
    for(auto& animation:animations_) {
        auto& event=animation.event;
        if(animation.localCollector) {
            event.collectorX=game.player.x;event.collectorY=game.player.y;event.collectorZ=game.player.z;
            event.previousCollectorX=game.player.previousX;event.previousCollectorY=game.player.previousY;event.previousCollectorZ=game.player.previousZ;
        } else if(!animation.detachedCollector) {
            const auto found=game.entities.find(event.collectorId);
            if(found==game.entities.end())animation.detachedCollector=true;
            else {
                const auto& target=found->second;
                event.collectorX=target.x;event.collectorY=target.y;event.collectorZ=target.z;
                event.previousCollectorX=target.previousX;event.previousCollectorY=target.previousY;event.previousCollectorZ=target.previousZ;
            }
        }
    }
    lastTick_=game.ticks;
}

std::vector<PickupAnimationSample> PickupAnimationTracker::frame(float partialTicks) const {
    partialTicks=std::isfinite(partialTicks)?std::clamp(partialTicks,0.f,1.f):0.f;
    std::vector<PickupAnimationSample> samples;samples.reserve(animations_.size());
    for(const auto& animation:animations_) {
        const auto& event=animation.event;
        // EntityPickupFX.renderParticle: Java float arithmetic for progress,
        // then double interpolation from the frozen source to the moving target.
        float progress=(static_cast<float>(animation.age)+partialTicks)/3.f;
        progress*=progress;
        const double targetX=event.previousCollectorX+(event.collectorX-event.previousCollectorX)*static_cast<double>(partialTicks);
        const double targetY=event.previousCollectorY+(event.collectorY-event.previousCollectorY)*static_cast<double>(partialTicks)+.5;
        const double targetZ=event.previousCollectorZ+(event.collectorZ-event.previousCollectorZ)*static_cast<double>(partialTicks);
        samples.push_back({&event.entity,
            event.entity.x+(targetX-event.entity.x)*static_cast<double>(progress),
            event.entity.y+(targetY-event.entity.y)*static_cast<double>(progress),
            event.entity.z+(targetZ-event.entity.z)*static_cast<double>(progress)});
    }
    return samples;
}

} // namespace mc
