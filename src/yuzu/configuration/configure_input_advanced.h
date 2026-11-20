// SPDX-FileCopyrightText: Copyright 2020 yuzu Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <array>
#include <memory>
#include <QWidget>

class QCheckBox;
class QColor;
class QComboBox;
class QPushButton;
class QSpinBox;

namespace Ui {
class ConfigureInputAdvanced;
}

namespace Core::HID {
class HIDCore;
} // namespace Core::HID

class ConfigureInputAdvanced : public QWidget {
    Q_OBJECT

public:
    explicit ConfigureInputAdvanced(Core::HID::HIDCore& hid_core_, QWidget* parent = nullptr);
    ~ConfigureInputAdvanced() override;

    void ApplyConfiguration();

signals:
    void CallDebugControllerDialog();
    void CallMouseConfigDialog();
    void CallTouchscreenConfigDialog();
    void CallMotionTouchConfigDialog();
    void CallRingControllerDialog();
    void CallCameraDialog();

private:
    void changeEvent(QEvent* event) override;
    void RetranslateUI();
    void UpdateUIEnabled();

    void OnControllerButtonClick(std::size_t player_idx, std::size_t button_idx);

    void LoadConfiguration();
    void SetupAutoAssignUI();

    std::unique_ptr<Ui::ConfigureInputAdvanced> ui;

    QCheckBox* auto_assign_enabled{};
    QCheckBox* auto_assign_require_input{};
    QComboBox* auto_assign_profile{};
    QSpinBox* auto_assign_max_players{};

    std::array<std::array<QColor, 4>, 8> controllers_colors;
    std::array<std::array<QPushButton*, 4>, 8> controllers_color_buttons;

    Core::HID::HIDCore& hid_core;
};
