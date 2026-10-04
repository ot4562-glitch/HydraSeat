#include "ui/engine_poller.hpp"
#include "ui/host_bootstrap.hpp"

#include <QString>

#include <algorithm>
#include <objbase.h>
#include <utility>
#include <windows.h>

namespace hydra::ui {


namespace {

std::wstring fromUtf8(const std::string& value) {
    return QString::fromUtf8(
               value.data(), static_cast<qsizetype>(value.size()))
        .toStdWString();
}

void applyHostHardwareInventory(
    EngineStatePayload& payload,
    const hydra::hostipc::HardwareInventory& inventory) {
    payload.displays.clear();
    payload.keyboards.clear();
    payload.mice.clear();
    payload.controllers.clear();

    for (const auto& record : inventory.devices) {
        hydra::DeviceInfo device;
        device.id = fromUtf8(record.stableIdUtf8);
        device.name = fromUtf8(record.displayNameUtf8);
        if (device.id.empty()) continue;

        switch (record.kind) {
        case hydra::hostipc::HardwareDeviceKind::Display:
            device.type = hydra::DeviceType::Display;
            payload.displays.push_back(std::move(device));
            break;
        case hydra::hostipc::HardwareDeviceKind::Keyboard:
            device.type = hydra::DeviceType::Keyboard;
            payload.keyboards.push_back(std::move(device));
            break;
        case hydra::hostipc::HardwareDeviceKind::Mouse:
            device.type = hydra::DeviceType::Mouse;
            payload.mice.push_back(std::move(device));
            break;
        case hydra::hostipc::HardwareDeviceKind::Controller:
            device.type = hydra::DeviceType::Controller;
            payload.controllers.push_back(std::move(device));
            break;
        }
    }
}

void keepHostOwnedAudioSessions(EngineStatePayload& payload) {
    if (!payload.hostSnapshot) {
        payload.audioSessions.clear();
        return;
    }

    std::vector<hydra::windows::AudioSessionObservation> filtered;
    for (const auto& session : payload.audioSessions) {
        if (!session.processIdentity) continue;

        bool owned = false;
        for (const auto& seat : payload.hostSnapshot->seats) {
            if (!seat.processOwned || seat.processId == 0 ||
                seat.processCreationIdentity == 0) {
                continue;
            }
            if (seat.processId == session.processIdentity->pid &&
                seat.processCreationIdentity ==
                    session.processIdentity->creationIdentity) {
                owned = true;
                break;
            }
        }
        if (!owned) continue;

        const auto existing = std::find_if(
            filtered.begin(), filtered.end(),
            [&](const auto& candidate) {
                return candidate.processIdentity &&
                       *candidate.processIdentity == *session.processIdentity;
            });
        if (existing == filtered.end()) {
            filtered.push_back(session);
        } else if (
            existing->state != hydra::windows::AudioSessionState::Active &&
            session.state == hydra::windows::AudioSessionState::Active) {
            *existing = session;
        }
    }

    payload.audioSessions = std::move(filtered);
}

} // namespace

void EnginePollerWorker::doPoll() {
    EngineStatePayload payload;


    std::string hostError;
    if (!m_hostClient.connected()) {
        if (!connectCanonicalHost(
                m_hostClient,
                hydra::hostipc::ClientRole::ReadOnly,
                &hostError)) {
            payload.hostError = std::move(hostError);
        }
    }
    if (m_hostClient.connected()) {
        auto snapshot = m_hostClient.getSnapshot(
            hydra::hostipc::kDefaultHostPipeTimeoutMs,
            &hostError);
        if (snapshot) {
            // A valid authority snapshot proves the canonical host connection
            // itself is alive. Inventory failures are subsystem failures and
            // must not make the status bar claim that the host disappeared.
            payload.hostConnected = true;
            payload.hostSnapshot = std::move(snapshot);

            std::string inventoryError;
            const auto inventory = m_hostClient.getHardwareInventory(
                hydra::hostipc::kDefaultHostPipeTimeoutMs,
                &inventoryError);
            if (!inventory) {
                payload.hardwareError = true;
                payload.hostError = inventoryError.empty()
                    ? "canonical host hardware inventory is unavailable"
                    : std::move(inventoryError);
            } else {
                payload.hardwareError = false;
                applyHostHardwareInventory(payload, *inventory);

                for (std::uint32_t seatId = 1;
                     seatId <= hydra::hostipc::kHostSeatCount;
                     ++seatId) {
                    std::string hardwareError;
                    payload.seatHardware[seatId - 1u] =
                        m_hostClient.getSeatHardware(
                            seatId,
                            hydra::hostipc::kDefaultHostPipeTimeoutMs,
                            &hardwareError);
                    if (!payload.seatHardware[seatId - 1u] &&
                        payload.hostError.empty()) {
                        payload.hostError = std::move(hardwareError);
                    }
                }
            }
        } else {
            payload.hardwareError = true;
            payload.hostError = std::move(hostError);
            m_hostClient.close();
        }
    }

    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool comInitialized = SUCCEEDED(hr);
    if (!comInitialized) {
        payload.audioEndpointError = true;
        payload.audioSessionError = true;
        emit pollCompleted(payload);
        return;
    }

    struct ComUninitializer {
        bool active;
        ~ComUninitializer() {
            if (active) CoUninitialize();
        }
    } comUninit{comInitialized};


    const auto endpointsResult =
        hydra::windows::AudioEndpointInventory::enumerateRenderEndpoints();
    if (endpointsResult.isSuccess()) {
        payload.audioEndpoints = *endpointsResult.endpoints;
    } else {
        payload.audioEndpointError = true;
    }

    const auto sessionsResult =
        hydra::windows::AudioSessionObserver::enumerateSessions();
    if (sessionsResult.isSuccess()) {
        payload.audioSessions = sessionsResult.sessions;
        keepHostOwnedAudioSessions(payload);
    } else {
        payload.audioSessionError = true;
    }

    payload.controllerInventory = m_controllerInventory.scan();

    emit pollCompleted(payload);
}

EnginePoller::EnginePoller(QObject* parent)
    : QObject(parent) {
    qRegisterMetaType<hydra::ui::EngineStatePayload>(
        "hydra::ui::EngineStatePayload");

    m_worker = new EnginePollerWorker();
    m_worker->moveToThread(&m_workerThread);

    connect(
        &m_workerThread,
        &QThread::finished,
        m_worker,
        &QObject::deleteLater);
    connect(
        this,
        &EnginePoller::triggerPoll,
        m_worker,
        &EnginePollerWorker::doPoll,
        Qt::QueuedConnection);
    connect(
        m_worker,
        &EnginePollerWorker::pollCompleted,
        this,
        &EnginePoller::stateUpdated,
        Qt::QueuedConnection);

    m_triggerTimer = new QTimer(this);
    connect(
        m_triggerTimer,
        &QTimer::timeout,
        this,
        &EnginePoller::triggerPoll);

    m_workerThread.start();
}

EnginePoller::~EnginePoller() {
    stopPolling();
    m_workerThread.quit();
    m_workerThread.wait();
}

void EnginePoller::startPolling(int intervalMs) {
    emit triggerPoll();
    m_triggerTimer->start(intervalMs);
}

void EnginePoller::stopPolling() {
    m_triggerTimer->stop();
}

} // namespace hydra::ui
