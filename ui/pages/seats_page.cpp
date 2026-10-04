#include "ui/pages/seats_page.hpp"
#include "ui/application_library.hpp"
#include "ui/ui_settings.hpp"

#include <QByteArray>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QMessageBox>
#include <QSettings>
#include <QVariant>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace hydra::ui {
namespace {

QSettings settingsStore() {
    return QSettings(
        QSettings::NativeFormat,
        QSettings::UserScope,
        QStringLiteral("HydraSeat"),
        QStringLiteral("HydraSeat"));
}

QString seatApplicationKey(std::uint32_t seatId) {
    return QStringLiteral("seats/%1/applicationPath").arg(seatId);
}

QString browseApplicationSentinel() {
    return QStringLiteral("hydraseat://browse-application");
}

} // namespace

SeatsPage::SeatsPage(
    std::shared_ptr<HostControlClient> hostControl,
    QWidget* parent)
    : QWidget(parent), m_hostControl(std::move(hostControl)) {

    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setStyleSheet("QScrollArea { border: none; background-color: transparent; }");
    auto* container = new QWidget();
    container->setStyleSheet("background-color: transparent;");
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(32, 32, 32, 32);
    layout->setSpacing(24);

    auto* title = new QLabel("Seats Management", container);
    title->setStyleSheet("font-size: 28px; font-weight: bold; color: #F5F5F5; font-family: 'Segoe UI', sans-serif;");
    layout->addWidget(title);

    auto* subtitle = new QLabel("Configure hardware assignments and application states", container);
    subtitle->setStyleSheet("font-size: 14px; color: #B5B5B5; font-family: 'Segoe UI', sans-serif; margin-bottom: 8px;");
    layout->addWidget(subtitle);

    auto* seatsLayout = new QHBoxLayout();
    seatsLayout->setSpacing(16);

    seatsLayout->addWidget(buildSeat(1, m_seat1));
    seatsLayout->addWidget(buildSeat(2, m_seat2));
    seatsLayout->addStretch();

    layout->addLayout(seatsLayout);
    layout->addStretch();
    scrollArea->setWidget(container);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->addWidget(scrollArea);
}

QWidget* SeatsPage::buildSeat(std::uint32_t seatId, SeatWidgets& w) {
    auto* frame = new QFrame(this);
    frame->setStyleSheet("background-color: #151515; border-radius: 6px; padding: 20px; border: 1px solid #292929;");
    frame->setMinimumWidth(400);
    frame->setMaximumWidth(500);
    
    auto* layout = new QVBoxLayout(frame);
    layout->setContentsMargins(0,0,0,0);
    layout->setSpacing(16);

    auto* headerLayout = new QHBoxLayout();
    auto* title = new QLabel(QString("SEAT %1").arg(seatId), frame);
    title->setStyleSheet("font-size: 16px; font-weight: bold; color: #F5F5F5; border: none;");
    w.stateBadge = new QLabel("Available", frame);
    w.stateBadge->setStyleSheet("font-size: 13px; font-weight: bold; color: #777777; border: none; margin-left: 12px;");
    headerLayout->addWidget(title);
    headerLayout->addWidget(w.stateBadge);
    headerLayout->addStretch();
    layout->addLayout(headerLayout);

    auto createRow = [&](const QString& labelText, QComboBox*& combo) {
        auto* l = new QLabel(labelText, frame);
        l->setStyleSheet("font-size: 11px; font-weight: bold; color: #777777; margin-bottom: 2px; border: none; text-transform: uppercase;");
        combo = new QComboBox(frame);
        combo->setStyleSheet("QComboBox { background-color: #0A0A0A; color: #F5F5F5; border: 1px solid #333333; border-radius: 4px; padding: 4px 8px; font-size: 13px; } QComboBox::drop-down { border: none; } QComboBox:disabled { color: #777777; background-color: #151515; }");
        layout->addWidget(l);
        layout->addWidget(combo);
    };

    createRow("APPLICATION", w.appCombo);
    createRow("DISPLAY", w.displayCombo);
    createRow("KEYBOARD", w.keyboardCombo);
    createRow("MOUSE", w.mouseCombo);
    
    auto* ctrlLabel = new QLabel("CONTROLLER", frame);
    ctrlLabel->setStyleSheet("font-size: 11px; font-weight: bold; color: #777777; margin-bottom: 2px; border: none; text-transform: uppercase;");
    layout->addWidget(ctrlLabel);
    
    auto* ctrlLayout = new QHBoxLayout();
    w.ctrlPhysCombo = new QComboBox(frame);
    w.ctrlSrcCombo = new QComboBox(frame);
    w.ctrlPhysCombo->setStyleSheet("QComboBox { background-color: #0A0A0A; color: #F5F5F5; border: 1px solid #333333; border-radius: 4px; padding: 4px 8px; font-size: 13px; } QComboBox::drop-down { border: none; } QComboBox:disabled { color: #777777; background-color: #151515; }");
    w.ctrlSrcCombo->setStyleSheet("QComboBox { background-color: #0A0A0A; color: #F5F5F5; border: 1px solid #333333; border-radius: 4px; padding: 4px 8px; font-size: 13px; } QComboBox::drop-down { border: none; } QComboBox:disabled { color: #777777; background-color: #151515; }");
    ctrlLayout->addWidget(w.ctrlPhysCombo);
    ctrlLayout->addWidget(w.ctrlSrcCombo);
    layout->addLayout(ctrlLayout);

    createRow("AUDIO", w.audioCombo);

    auto* btnLayout = new QHBoxLayout();
    
    auto styleBtnPrimary = "QPushButton { background-color: #E10600; color: #F5F5F5; font-size: 14px; font-weight: bold; padding: 8px 16px; border-radius: 4px; border: none; } QPushButton:hover { background-color: #FF0A04; } QPushButton:disabled { background-color: #333333; color: #777777; }";
    auto styleBtnSecondary = "QPushButton { background-color: #292929; color: #F5F5F5; font-size: 14px; font-weight: bold; padding: 8px 16px; border-radius: 4px; border: 1px solid #333333; } QPushButton:hover { background-color: #333333; } QPushButton:disabled { background-color: #151515; color: #777777; border: 1px solid #222222; }";

    w.configureBtn = new QPushButton("Configure Seat", frame);
    w.configureBtn->setStyleSheet(styleBtnSecondary);
    w.launchBtn = new QPushButton("Launch", frame);
    w.launchBtn->setStyleSheet(styleBtnPrimary);
    w.stopBtn = new QPushButton("Stop", frame);
    w.stopBtn->setStyleSheet(styleBtnSecondary);
    w.reconfigureBtn = new QPushButton("Reconfigure", frame);
    w.reconfigureBtn->setStyleSheet(styleBtnSecondary);

    btnLayout->addWidget(w.configureBtn);
    btnLayout->addWidget(w.launchBtn);
    btnLayout->addWidget(w.stopBtn);
    btnLayout->addWidget(w.reconfigureBtn);
    layout->addLayout(btnLayout);

    w.feedbackLabel = new QLabel(frame);
    w.feedbackLabel->setStyleSheet("font-size: 12px; color: #B5B5B5; border: none; margin-top: 8px;");
    w.feedbackLabel->setWordWrap(true);
    w.feedbackLabel->setVisible(false);
    layout->addWidget(w.feedbackLabel);

    connect(
        w.appCombo,
        qOverload<int>(&QComboBox::currentIndexChanged),
        this,
        [this, seatId](int) { onApplicationSelectionChanged(seatId); });
    connect(w.configureBtn, &QPushButton::clicked, this, [this, seatId]() { onConfigureRequested(seatId); });
    connect(w.launchBtn, &QPushButton::clicked, this, [this, seatId]() { onLaunchRequested(seatId); });
    connect(w.stopBtn, &QPushButton::clicked, this, [this, seatId]() { onStopRequested(seatId); });
    connect(w.reconfigureBtn, &QPushButton::clicked, this, [this, seatId]() { onConfigureRequested(seatId); });

    return frame;
}

const hydra::hostipc::SeatSnapshot* SeatsPage::seatSnapshot(
    std::uint32_t seatId) const noexcept {
    if (!m_lastPayload.hostSnapshot) return nullptr;
    if (seatId > 0 && seatId <= m_lastPayload.hostSnapshot->seats.size()) {
        return &m_lastPayload.hostSnapshot->seats[seatId - 1u];
    }
    return nullptr;
}

void SeatsPage::updateState(const EngineStatePayload& payload) {
    m_lastPayload = payload;
    // Populate first so button enablement reflects the persisted/current
    // application in the same refresh cycle.
    populateCombos(1, m_seat1);
    populateCombos(2, m_seat2);
    updateSeatData(1, m_seat1);
    updateSeatData(2, m_seat2);
}

void SeatsPage::refreshApplications() {
    populateCombos(1, m_seat1);
    populateCombos(2, m_seat2);
    updateSeatData(1, m_seat1);
    updateSeatData(2, m_seat2);
}

void SeatsPage::updateSeatData(std::uint32_t seatId, SeatWidgets& w) {
    const auto* snapshot = seatSnapshot(seatId);
    const bool hasApplication =
        w.appCombo && !w.appCombo->currentData().toString().isEmpty();

    if (!snapshot) {
        w.stateBadge->setText("Host unavailable");
        w.stateBadge->setStyleSheet("font-size: 13px; font-weight: bold; color: #777777; border: none; margin-left: 12px;");
        w.configureBtn->setDisabled(true);
        w.launchBtn->setDisabled(true);
        w.stopBtn->setDisabled(true);
        w.reconfigureBtn->setDisabled(true);
        if (w.appCombo) w.appCombo->setDisabled(true);
        return;
    }

    if (snapshot->active && snapshot->processOwned) {
        w.stateBadge->setText("Running");
        w.stateBadge->setStyleSheet("font-size: 13px; font-weight: bold; color: #E10600; border: none; margin-left: 12px;");

        w.configureBtn->setVisible(false);
        w.launchBtn->setVisible(false);
        w.stopBtn->setVisible(true);
        w.reconfigureBtn->setVisible(true);

        w.stopBtn->setDisabled(false);
        w.reconfigureBtn->setDisabled(true);
        w.reconfigureBtn->setToolTip(
            "Stop the running Seat application before changing hardware.");
        if (w.appCombo) w.appCombo->setDisabled(true);
        if (w.displayCombo) w.displayCombo->setDisabled(true);
        if (w.keyboardCombo) w.keyboardCombo->setDisabled(true);
        if (w.mouseCombo) w.mouseCombo->setDisabled(true);
        if (w.ctrlPhysCombo) w.ctrlPhysCombo->setDisabled(true);
        if (w.ctrlSrcCombo) w.ctrlSrcCombo->setDisabled(true);
    } else {
        w.stateBadge->setText("Ready");
        w.stateBadge->setStyleSheet("font-size: 13px; font-weight: bold; color: #777777; border: none; margin-left: 12px;");

        w.configureBtn->setVisible(true);
        w.launchBtn->setVisible(true);
        w.stopBtn->setVisible(false);
        w.reconfigureBtn->setVisible(false);

        w.configureBtn->setDisabled(false);
        w.launchBtn->setDisabled(!hasApplication);
        w.reconfigureBtn->setToolTip({});
        if (w.appCombo) w.appCombo->setDisabled(w.appCombo->count() <= 1);
        if (w.displayCombo) w.displayCombo->setDisabled(w.displayCombo->count() <= 1);
        if (w.keyboardCombo) w.keyboardCombo->setDisabled(w.keyboardCombo->count() <= 1);
        if (w.mouseCombo) w.mouseCombo->setDisabled(w.mouseCombo->count() <= 1);
        if (w.ctrlPhysCombo) w.ctrlPhysCombo->setDisabled(false);
        if (w.ctrlSrcCombo) w.ctrlSrcCombo->setDisabled(false);
    }
}

void SeatsPage::populateCombos(
    std::uint32_t seatId,
    SeatWidgets& w) {
    if (!w.appCombo || !w.displayCombo || !w.keyboardCombo || !w.mouseCombo ||
        !w.ctrlPhysCombo || !w.ctrlSrcCombo || !w.audioCombo) {
        return;
    }

    if (!w.appCombo->hasFocus()) {
        const QString previous = w.appCombo->currentData().toString();
        auto settings = settingsStore();
        const QString persisted =
            settings.value(seatApplicationKey(seatId)).toString();
        const auto applications = ApplicationLibrary::load();

        w.appCombo->blockSignals(true);
        w.appCombo->clear();
        w.appCombo->addItem("-- Select application --", QString());

        for (const auto& app : applications) {
            w.appCombo->addItem(app.title, app.executablePath);
            const int index = w.appCombo->count() - 1;
            w.appCombo->setItemData(index, app.arguments, Qt::UserRole + 1);
            w.appCombo->setItemData(
                index, app.workingDirectory, Qt::UserRole + 2);
        }

        // Preserve the existing combo layout while making Seat setup
        // self-contained: a user can select an executable here directly.
        w.appCombo->addItem("Browse...", browseApplicationSentinel());

        const QString wanted =
            !previous.isEmpty() ? previous : persisted;
        const int index = w.appCombo->findData(wanted);
        w.appCombo->setCurrentIndex(index >= 0 ? index : 0);
        w.appCombo->setEnabled(true);
        w.appCombo->blockSignals(false);
    }

    // Never rewrite a hardware combo while the user is actively choosing a value.
    if (w.displayCombo->hasFocus() || w.keyboardCombo->hasFocus() ||
        w.mouseCombo->hasFocus() || w.ctrlPhysCombo->hasFocus() ||
        w.ctrlSrcCombo->hasFocus()) {
        return;
    }

    QString assignedDisplay;
    QString assignedKeyboard;
    QString assignedMouse;
    if (seatId > 0 && seatId <= m_lastPayload.seatHardware.size()) {
        const auto& assigned = m_lastPayload.seatHardware[seatId - 1u];
        if (assigned) {
            assignedDisplay =
                QString::fromUtf8(assigned->displayIdUtf8.c_str());
            assignedKeyboard =
                QString::fromUtf8(assigned->keyboardIdUtf8.c_str());
            assignedMouse =
                QString::fromUtf8(assigned->mouseIdUtf8.c_str());
        }
    }

    const auto repopulateHardware =
        [](QComboBox* combo,
           const std::vector<hydra::DeviceInfo>& devices,
           const QString& assignedId,
           const QString& emptyText) {
            const QString previous = combo->currentData().toString();

            combo->blockSignals(true);
            combo->clear();
            combo->addItem(
                devices.empty() ? emptyText : QString("-- Select --"),
                QString());

            for (const auto& device : devices) {
                const QString id = QString::fromStdWString(device.id);
                const QString name = QString::fromStdWString(device.name);
                combo->addItem(name, id);
            }

            QString wanted = previous;
            if (wanted.isEmpty()) wanted = assignedId;
            const int index = combo->findData(wanted);
            if (index >= 0) combo->setCurrentIndex(index);
            combo->setEnabled(!devices.empty());
            combo->blockSignals(false);
        };

    repopulateHardware(
        w.displayCombo,
        m_lastPayload.displays,
        assignedDisplay,
        "No physical display detected");
    repopulateHardware(
        w.keyboardCombo,
        m_lastPayload.keyboards,
        assignedKeyboard,
        "No confirmed keyboard detected");
    repopulateHardware(
        w.mouseCombo,
        m_lastPayload.mice,
        assignedMouse,
        "No confirmed mouse detected");

    // Per-process audio routing only becomes meaningful after a Seat game has
    // launched. Do not present this field as if it were already part of the
    // persistent Seat hardware assignment.
    w.audioCombo->blockSignals(true);
    w.audioCombo->clear();
    w.audioCombo->addItem("Use Audio page after launch");
    w.audioCombo->setEnabled(false);
    w.audioCombo->blockSignals(false);

    const QString previousPhysical = w.ctrlPhysCombo->currentData().toString();
    const QVariant previousSource = w.ctrlSrcCombo->currentData();

    w.ctrlPhysCombo->blockSignals(true);
    w.ctrlSrcCombo->blockSignals(true);
    w.ctrlPhysCombo->clear();
    w.ctrlSrcCombo->clear();

    w.ctrlPhysCombo->addItem("-- None --", QString());
    for (const auto& phys :
         m_lastPayload.controllerInventory.physicalControllers) {
        w.ctrlPhysCombo->addItem(
            QString::fromStdWString(phys.displayName),
            QString::fromStdWString(phys.persistentId));
    }

    w.ctrlSrcCombo->addItem("-- None --", QVariant());
    for (const auto& src : m_lastPayload.controllerInventory.sources) {
        if (!src.connected || !src.runtimeXInputSlot) continue;
        w.ctrlSrcCombo->addItem(
            QString("%1 (XInput %2)")
                .arg(QString::fromStdWString(src.displayName))
                .arg(static_cast<int>(*src.runtimeXInputSlot)),
            QVariant::fromValue(static_cast<int>(*src.runtimeXInputSlot)));
    }

    const int physicalIndex =
        w.ctrlPhysCombo->findData(previousPhysical);
    if (physicalIndex >= 0) {
        w.ctrlPhysCombo->setCurrentIndex(physicalIndex);
    }

    const int sourceIndex = w.ctrlSrcCombo->findData(previousSource);
    if (sourceIndex >= 0) {
        w.ctrlSrcCombo->setCurrentIndex(sourceIndex);
    }

    w.ctrlPhysCombo->blockSignals(false);
    w.ctrlSrcCombo->blockSignals(false);
}
void SeatsPage::onConfigureRequested(std::uint32_t seatId) {
    SeatWidgets& w = (seatId == 1u) ? m_seat1 : m_seat2;
    w.feedbackLabel->setVisible(false);

    if (!m_hostControl) {
        w.feedbackLabel->setText("Canonical host control is unavailable.");
        w.feedbackLabel->setVisible(true);
        return;
    }

    const QString displayId = w.displayCombo->currentData().toString();
    const QString keyboardId = w.keyboardCombo->currentData().toString();
    const QString mouseId = w.mouseCombo->currentData().toString();
    if (displayId.isEmpty() || keyboardId.isEmpty() || mouseId.isEmpty()) {
        w.feedbackLabel->setText(
            "Select a confirmed display, keyboard, and mouse before configuring the Seat.");
        w.feedbackLabel->setStyleSheet(
            "font-size: 12px; color: #E10600; border: none;");
        w.feedbackLabel->setVisible(true);
        return;
    }

    const QByteArray displayUtf8 = displayId.toUtf8();
    const QByteArray keyboardUtf8 = keyboardId.toUtf8();
    const QByteArray mouseUtf8 = mouseId.toUtf8();

    std::string error;
    const auto assignment = m_hostControl->assignSeatHardware(
        seatId,
        std::string(displayUtf8.constData(), displayUtf8.size()),
        std::string(keyboardUtf8.constData(), keyboardUtf8.size()),
        std::string(mouseUtf8.constData(), mouseUtf8.size()),
        &error);
    if (!assignment) {
        w.feedbackLabel->setText(
            QString("Hardware assignment failed: %1")
                .arg(QString::fromStdString(error)));
        w.feedbackLabel->setStyleSheet(
            "font-size: 12px; color: #E10600; border: none;");
        w.feedbackLabel->setVisible(true);
        return;
    }

    const QString physicalControllerId =
        w.ctrlPhysCombo->currentData().toString();
    bool sourceOk = false;
    const int sourceSlot = w.ctrlSrcCombo->currentData().toInt(&sourceOk);
    const bool hasPhysicalController = !physicalControllerId.isEmpty();
    const bool hasControllerSource =
        sourceOk && sourceSlot >= 0 && sourceSlot < 4;

    if (hasPhysicalController != hasControllerSource) {
        w.feedbackLabel->setText(
            "For controller use, select both the physical controller and its connected XInput source.");
        w.feedbackLabel->setStyleSheet(
            "font-size: 12px; color: #E10600; border: none;");
        w.feedbackLabel->setVisible(true);
        return;
    }

    if (hasPhysicalController) {
        const QByteArray controllerUtf8 = physicalControllerId.toUtf8();
        const auto paired = m_hostControl->pairController(
            seatId,
            std::string(controllerUtf8.constData(), controllerUtf8.size()),
            static_cast<std::uint8_t>(sourceSlot),
            &error);
        if (!paired) {
            w.feedbackLabel->setText(
                QString("Hardware saved, but controller pairing failed: %1")
                    .arg(QString::fromStdString(error)));
            w.feedbackLabel->setStyleSheet(
                "font-size: 12px; color: #E10600; border: none;");
            w.feedbackLabel->setVisible(true);
            return;
        }
    }

    w.feedbackLabel->setText(
        hasPhysicalController
            ? "Seat hardware and controller assignment saved."
            : "Seat hardware assignment saved.");
    w.feedbackLabel->setStyleSheet(
        "font-size: 12px; color: #B5B5B5; border: none;");
    w.feedbackLabel->setVisible(true);
}
void SeatsPage::onApplicationSelectionChanged(std::uint32_t seatId) {
    SeatWidgets& w = (seatId == 1u) ? m_seat1 : m_seat2;
    if (!w.appCombo) return;

    const QString selected = w.appCombo->currentData().toString();
    if (selected == browseApplicationSentinel()) {
        const QString executablePath = QFileDialog::getOpenFileName(
            this,
            "Select game executable",
            {},
            "Windows applications (*.exe);;All files (*.*)");
        if (executablePath.isEmpty()) {
            w.appCombo->setCurrentIndex(0);
            updateSeatData(seatId, w);
            return;
        }

        const QFileInfo executable(executablePath);
        ApplicationLibrary::remember(ApplicationLaunchEntry{
            executable.completeBaseName(),
            executable.absoluteFilePath(),
            {},
            executable.absolutePath(),
        });
        auto settings = settingsStore();
        settings.setValue(
            seatApplicationKey(seatId), executable.absoluteFilePath());
        settings.sync();
        refreshApplications();
        return;
    }

    auto settings = settingsStore();
    settings.setValue(seatApplicationKey(seatId), selected);
    settings.sync();
    updateSeatData(seatId, w);
}

void SeatsPage::onLaunchRequested(std::uint32_t seatId) {
    SeatWidgets& w = (seatId == 1u) ? m_seat1 : m_seat2;
    w.feedbackLabel->setVisible(false);

    if (!m_hostControl) {
        w.feedbackLabel->setText("Canonical host control is unavailable.");
        w.feedbackLabel->setVisible(true);
        return;
    }

    const QString executablePath = w.appCombo->currentData().toString();
    const int selectedIndex = w.appCombo->currentIndex();
    const QString arguments =
        w.appCombo->itemData(selectedIndex, Qt::UserRole + 1).toString();
    QString workingDirectory =
        w.appCombo->itemData(selectedIndex, Qt::UserRole + 2).toString();
    const QFileInfo executable(executablePath);

    if (executablePath.isEmpty() || !executable.isAbsolute() ||
        !executable.exists() || !executable.isFile()) {
        w.feedbackLabel->setText(
            "Select a valid application before launching.");
        w.feedbackLabel->setStyleSheet(
            "font-size: 12px; color: #E10600; border: none;");
        w.feedbackLabel->setVisible(true);
        return;
    }
    if (workingDirectory.isEmpty()) {
        workingDirectory = executable.absolutePath();
    }

    w.stateBadge->setText("Starting...");
    w.stateBadge->setStyleSheet(
        "font-size: 13px; font-weight: bold; color: #B5B5B5; "
        "border: none; margin-left: 12px;");

    const auto utf8 = [](const QString& value) {
        const QByteArray bytes = value.toUtf8();
        return std::string(
            bytes.constData(),
            static_cast<std::size_t>(bytes.size()));
    };

    std::string error;
    const auto result = m_hostControl->launchGame(
        seatId,
        utf8(w.appCombo->currentText()),
        utf8(executable.absoluteFilePath()),
        utf8(arguments),
        utf8(workingDirectory),
        &error);

    if (!result) {
        w.feedbackLabel->setText(
            QString("The application could not be launched. %1")
                .arg(QString::fromStdString(error)));
        w.feedbackLabel->setStyleSheet(
            "font-size: 12px; color: #E10600; border: none;");
        w.feedbackLabel->setVisible(true);
        updateSeatData(seatId, w);
        return;
    }

    m_lastPayload.hostSnapshot = *result;
    w.feedbackLabel->setText("Application launched on this Seat.");
    w.feedbackLabel->setStyleSheet(
        "font-size: 12px; color: #B5B5B5; border: none;");
    w.feedbackLabel->setVisible(true);
    updateSeatData(seatId, w);
}
void SeatsPage::onStopRequested(std::uint32_t seatId) {
    SeatWidgets& w = (seatId == 1u) ? m_seat1 : m_seat2;
    w.feedbackLabel->setVisible(false);

    if (!m_hostControl) {
        w.feedbackLabel->setText("Canonical host control is unavailable.");
        w.feedbackLabel->setVisible(true);
        return;
    }

    if (UiSettings::load().confirmSeatStop) {
        const auto answer = QMessageBox::question(
            this,
            "Stop Seat application",
            QString("Stop the application running on Seat %1?").arg(seatId),
            QMessageBox::Yes | QMessageBox::Cancel,
            QMessageBox::Cancel);
        if (answer != QMessageBox::Yes) return;
    }

    std::string error;
    const auto result = m_hostControl->stopGame(seatId, &error);
    
    if (!result) {
        w.feedbackLabel->setText(QString("The application could not be stopped. %1").arg(QString::fromStdString(error)));
        w.feedbackLabel->setStyleSheet("font-size: 12px; color: #E10600; border: none;");
        w.feedbackLabel->setVisible(true);
    }
}

} // namespace hydra::ui
