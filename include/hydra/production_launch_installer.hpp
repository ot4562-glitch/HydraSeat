#pragma once

#include "hydra/host_transport.hpp"
#include "hydra/seat_launch_flow.hpp"

namespace hydra::production {

// Production P7 -> Host bridge. It never installs a plan from UI-owned state
// directly: every call resnapshots Host authority, reads the Host-owned registry
// revision, and submits the full typed P6 provider plan bound to the exact next
// Seat lifecycle generation.
class HostProviderPlanInstaller final : public seatui::ISeatLaunchPlanInstaller {
public:
    explicit HostProviderPlanInstaller(hostipc::HostControlClient& client)
        : client_(client) {}

    bool install(SeatId seatId,
                 const plan::ProviderAwareLaunchPlan& fullPlan,
                 const plan::SeatProviderLaunchPlan& seatPlan,
                 std::string& error) override;
    bool rollback(SeatId seatId, std::string& error) noexcept override;

private:
    hostipc::HostControlClient& client_;
};

} // namespace hydra::production
