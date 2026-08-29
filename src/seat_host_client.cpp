#include "hydra/seat_host_client.hpp"

namespace hydra::seatui {

SeatHostClient::SeatHostClient(SeatId seatId) : seatId_(seatId) {}

bool SeatHostClient::connect(std::uint32_t timeoutMs, std::string* error) {
    if (seatId_ == 0) {
        if (error != nullptr) *error = "Seat launcher cannot use reserved Seat 0";
        return false;
    }
    return client_.connectForSeat(hostipc::ClientRole::SeatControl,
                                  seatId_, timeoutMs, error);
}

void SeatHostClient::close() noexcept { client_.close(); }

bool SeatHostClient::connected() const noexcept { return client_.connected(); }

std::optional<runtime::HostRuntimeSnapshot> SeatHostClient::resnapshot(
    std::uint32_t timeoutMs, std::string* error) {
    return client_.getSnapshot(timeoutMs, error);
}

std::optional<runtime::SeatGameCommandResult> SeatHostClient::command(
    hostipc::MessageType type, std::optional<runtime::SeatGameBinding> binding,
    std::uint32_t timeoutMs, std::string* error,
    std::optional<hostipc::ErrorPayload>* protocolError) {
    hostipc::SeatGameCommandPayload payload{seatId_, std::move(binding)};
    return client_.seatGameCommand(type, payload, timeoutMs, error, protocolError);
}

std::optional<runtime::SeatGameCommandResult> SeatHostClient::assign(
    const runtime::SeatGameBinding& binding, std::uint32_t timeoutMs,
    std::string* error, std::optional<hostipc::ErrorPayload>* protocolError) {
    return command(hostipc::MessageType::AssignSeatGame, binding,
                   timeoutMs, error, protocolError);
}

std::optional<runtime::SeatGameCommandResult> SeatHostClient::start(
    std::uint32_t timeoutMs, std::string* error,
    std::optional<hostipc::ErrorPayload>* protocolError) {
    return command(hostipc::MessageType::StartSeatGame, std::nullopt,
                   timeoutMs, error, protocolError);
}

std::optional<runtime::SeatGameCommandResult> SeatHostClient::endPlaying(
    std::uint32_t timeoutMs, std::string* error,
    std::optional<hostipc::ErrorPayload>* protocolError) {
    return command(hostipc::MessageType::StopSeatGame, std::nullopt,
                   timeoutMs, error, protocolError);
}

} // namespace hydra::seatui
