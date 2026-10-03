#include "ui/pages/settings_page.hpp"

#include "ui/ui_settings.hpp"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>

namespace hydra::ui {
namespace {

QLabel* sectionTitle(const QString& text, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setStyleSheet(
        "font-size: 12px; font-weight: bold; color: #777777; "
        "letter-spacing: 1px; margin-top: 12px;");
    return label;
}

QFrame* settingsRow(
    const QString& title,
    const QString& description,
    QWidget* control,
    QWidget* parent) {
    auto* frame = new QFrame(parent);
    frame->setMaximumWidth(760);
    frame->setStyleSheet(
        "QFrame { background-color: #151515; border-radius: 8px; "
        "border: 1px solid #292929; }"
        "QLabel { border: none; }");

    auto* row = new QHBoxLayout(frame);
    row->setContentsMargins(16, 12, 16, 12);
    row->setSpacing(16);

    auto* textColumn = new QVBoxLayout();
    textColumn->setSpacing(3);

    auto* titleLabel = new QLabel(title, frame);
    titleLabel->setStyleSheet(
        "font-size: 14px; font-weight: 600; color: #F5F5F5;");
    textColumn->addWidget(titleLabel);

    auto* descriptionLabel = new QLabel(description, frame);
    descriptionLabel->setWordWrap(true);
    descriptionLabel->setStyleSheet(
        "font-size: 12px; color: #8A8A8A;");
    textColumn->addWidget(descriptionLabel);

    row->addLayout(textColumn, 1);
    if (control) {
        row->addWidget(control, 0, Qt::AlignVCenter);
    }
    return frame;
}

} // namespace

SettingsPage::SettingsPage(QWidget* parent)
    : QWidget(parent) {
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    auto* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setStyleSheet(
        "QScrollArea { border: none; background-color: transparent; }");
    mainLayout->addWidget(scrollArea);

    auto* container = new QWidget(scrollArea);
    container->setStyleSheet("background-color: transparent;");
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(32, 32, 32, 32);
    layout->setSpacing(12);

    auto* title = new QLabel("Settings", container);
    title->setStyleSheet(
        "font-size: 28px; font-weight: bold; color: #F5F5F5;");
    layout->addWidget(title);

    auto* subtitle = new QLabel(
        "Preferences stored for this Windows user. Runtime authority remains in hydra_host.",
        container);
    subtitle->setWordWrap(true);
    subtitle->setStyleSheet(
        "font-size: 13px; color: #B5B5B5; margin-bottom: 8px;");
    layout->addWidget(subtitle);

    const auto settings = UiSettings::load();

    layout->addWidget(sectionTitle("GENERAL", container));

    m_startMinimized = new QCheckBox(container);
    m_startMinimized->setChecked(settings.startMinimized);
    m_startMinimized->setAccessibleName("Start HydraSeat minimized");
    layout->addWidget(settingsRow(
        "Start minimized",
        "Open HydraSeat minimized on the next application start.",
        m_startMinimized,
        container));

    m_confirmSeatStop = new QCheckBox(container);
    m_confirmSeatStop->setChecked(settings.confirmSeatStop);
    m_confirmSeatStop->setAccessibleName("Confirm before stopping a Seat game");
    layout->addWidget(settingsRow(
        "Confirm Seat stop",
        "Ask before terminating a host-owned Seat process tree.",
        m_confirmSeatStop,
        container));

    auto* installerManaged = new QLabel("Installer managed", container);
    installerManaged->setStyleSheet(
        "font-size: 12px; color: #777777; padding: 4px 8px; "
        "border: 1px solid #333333; border-radius: 4px;");
    layout->addWidget(settingsRow(
        "Start with Windows",
        "Autostart is an install-time privilege decision and is not changed by the UI.",
        installerManaged,
        container));

    layout->addWidget(sectionTitle("REFRESH", container));

    m_refreshInterval = new QSpinBox(container);
    m_refreshInterval->setRange(
        UiSettings::kMinimumRefreshIntervalMs,
        UiSettings::kMaximumRefreshIntervalMs);
    m_refreshInterval->setSingleStep(500);
    m_refreshInterval->setSuffix(" ms");
    m_refreshInterval->setValue(settings.refreshIntervalMs);
    m_refreshInterval->setAccessibleName("Engine refresh interval");
    m_refreshInterval->setStyleSheet(
        "QSpinBox { min-width: 110px; padding: 5px 8px; "
        "background-color: #101010; color: #F5F5F5; "
        "border: 1px solid #333333; border-radius: 5px; }");
    layout->addWidget(settingsRow(
        "Engine refresh interval",
        "Controls how often the UI refreshes host, device, controller, and audio observations.",
        m_refreshInterval,
        container));

    layout->addWidget(sectionTitle("APPEARANCE", container));

    auto* fixedTheme = new QLabel("Dark", container);
    fixedTheme->setStyleSheet(
        "font-size: 12px; color: #B5B5B5; padding: 4px 8px; "
        "border: 1px solid #333333; border-radius: 4px;");
    layout->addWidget(settingsRow(
        "Theme",
        "The pre-release candidate currently ships one tested high-contrast dark theme.",
        fixedTheme,
        container));

    layout->addStretch();
    scrollArea->setWidget(container);

    connect(
        m_startMinimized,
        &QCheckBox::toggled,
        this,
        [this](bool) { savePreferences(); });
    connect(
        m_confirmSeatStop,
        &QCheckBox::toggled,
        this,
        [this](bool) { savePreferences(); });
    connect(
        m_refreshInterval,
        qOverload<int>(&QSpinBox::valueChanged),
        this,
        [this](int) { savePreferences(); });
}

void SettingsPage::savePreferences() {
    UiSettingsSnapshot settings;
    settings.startMinimized =
        m_startMinimized && m_startMinimized->isChecked();
    settings.confirmSeatStop =
        !m_confirmSeatStop || m_confirmSeatStop->isChecked();
    settings.refreshIntervalMs =
        m_refreshInterval
            ? m_refreshInterval->value()
            : UiSettings::kDefaultRefreshIntervalMs;

    UiSettings::save(settings);
    emit preferencesChanged();
}

} // namespace hydra::ui
