#pragma once

#include "hydra/host_transport.hpp"

#include <optional>
#include <string>

namespace hydra::seatui {

// Capability-restricted host adapter for a single Seat launcher. Deliberately
// exposes no generic/global RuntimeHost command surface.
class SeatHostClient {
public:
    explicit SeatHostClient(SeatId seatId);

    bool connect(std::uint32_t timeoutMs = hostipc::kDefaultHostIpcTimeoutMs,
                 std::string* error = nullptr);
    void close() noexcept;
    bool connected() const noexcept;
    SeatId seatId() const noexcept { return seatId_; }

    std::optional<runtime::HostRuntimeSnapshot> resnapshot(
        std::uint32_t timeoutMs = hostipc::kDefaultHostIpcTimeoutMs,
        std::string* error = nullptr);
    std::optional<runtime::SeatGameCommandResult> assign(
        const runtime::SeatGameBinding& binding,
        std::uint32_t timeoutMs = hostipc::kDefaultHostIpcTimeoutMs,
        std::string* error = nullptr,
        std::optional<hostipc::ErrorPayload>* protocolError = nullptr);
    std::optional<runtime::SeatGameCommandResult> start(
        std::uint32_t timeoutMs = hostipc::kDefaultHostIpcTimeoutMs,
        std::string* error = nullptr,
        std::optional<hostipc::ErrorPayload>* protocolError = nullptr);
    std::optional<runtime::SeatGameCommandResult> endPlaying(
        std::uint32_t timeoutMs = hostipc::kDefaultHostIpcTimeoutMs,
        std::string* error = nullptr,
        std::optional<hostipc::ErrorPayload>* protocolError = nullptr);

private:
    std::optional<runtime::SeatGameCommandResult> command(
        hostipc::MessageType type,
        std::optional<runtime::SeatGameBinding> binding,
        std::uint32_t timeoutMs,
        std::string* error,
        std::optional<hostipc::ErrorPayload>* protocolError);

    SeatId seatId_{0};
    hostipc::HostControlClient client_;
};

} // namespace hydra::seatui
