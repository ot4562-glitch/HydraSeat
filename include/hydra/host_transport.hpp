#pragma once

#include "hydra/host_protocol.hpp"
#include "hydra/runtime_host.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace hydra::hostipc {

constexpr std::uint32_t kDefaultHostIpcTimeoutMs = 3000u;

std::wstring currentHostPipeName();

struct SubscriptionStart {
    runtime::HostRuntimeSnapshot snapshot;
    std::uint64_t correlationId{0};
};

struct ReceivedEvent {
    std::optional<runtime::RuntimeTransition> event;
    std::optional<ErrorPayload> error;
};

// Generic UI/CLI client adapter. It owns no runtime component and treats the
// host snapshot as authoritative; a timeout/disconnect never implies a state.
class HostControlClient {
public:
    HostControlClient();
    ~HostControlClient();
    HostControlClient(const HostControlClient&) = delete;
    HostControlClient& operator=(const HostControlClient&) = delete;
    HostControlClient(HostControlClient&&) noexcept;
    HostControlClient& operator=(HostControlClient&&) noexcept;

    bool connect(ClientRole role,
                 std::uint32_t timeoutMs = kDefaultHostIpcTimeoutMs,
                 std::string* error = nullptr);
    void close() noexcept;
    bool connected() const noexcept;
    ClientRole role() const noexcept;

    std::optional<runtime::HostRuntimeSnapshot> getSnapshot(
        std::uint32_t timeoutMs = kDefaultHostIpcTimeoutMs,
        std::string* error = nullptr);
    std::optional<runtime::RuntimeCommandResult> command(
        MessageType requestType,
        std::uint32_t timeoutMs = kDefaultHostIpcTimeoutMs,
        std::string* error = nullptr,
        std::optional<ErrorPayload>* protocolError = nullptr);
    bool ping(std::uint64_t nonce,
              std::uint32_t timeoutMs = kDefaultHostIpcTimeoutMs,
              std::string* error = nullptr);

    std::optional<SubscriptionStart> beginSubscription(
        std::uint64_t afterSequence,
        std::uint32_t maxEvents,
        std::uint32_t timeoutMs = kDefaultHostIpcTimeoutMs,
        std::string* error = nullptr);
    ReceivedEvent readSubscriptionEvent(
        std::uint32_t timeoutMs,
        std::string* error = nullptr);

    // Low-level correlated transaction used by protocol/process tests and future
    // adapters that need an externally supplied correlation ID. Callers still
    // receive a single response and cannot bypass server-side permissions.
    std::optional<Frame> transact(
        MessageType requestType, std::span<const std::byte> payload,
        std::uint64_t correlationId,
        std::uint32_t timeoutMs = kDefaultHostIpcTimeoutMs,
        std::string* error = nullptr);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

class HostControlServer {
public:
    explicit HostControlServer(runtime::RuntimeHost& host);
    ~HostControlServer();
    HostControlServer(const HostControlServer&) = delete;
    HostControlServer& operator=(const HostControlServer&) = delete;

    // Blocking accept loop. It returns after ExitHostWhenIdle is accepted or on
    // fatal transport setup failure. Connected clients are bounded threads.
    bool serve(std::string* error = nullptr);
    void requestStop() noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace hydra::hostipc
