#pragma once

#include <QCheckBox>
#include <QSpinBox>
#include <QWidget>

namespace hydra::ui {

class SettingsPage : public QWidget {
    Q_OBJECT

public:
    explicit SettingsPage(QWidget* parent = nullptr);

signals:
    void preferencesChanged();

private:
    void savePreferences();

    QCheckBox* m_startMinimized{nullptr};
    QCheckBox* m_confirmSeatStop{nullptr};
    QSpinBox* m_refreshInterval{nullptr};
};

} // namespace hydra::ui
