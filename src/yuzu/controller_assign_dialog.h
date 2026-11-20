// SPDX-FileCopyrightText: Copyright 2026 Fritchii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include <QDialog>
#include <QString>
#include <QTimer>

#include "common/param_package.h"

class QLabel;
class QPushButton;

namespace InputCommon {
class InputSubsystem;
}

/// Opened with L + R + Start: a full-window overlay that picks the player slot and profile for
/// the controller that pressed it. That controller drives it (D-pad picks the player, LB/RB the
/// profile, A confirms, B cancels);
/// mouse and keyboard work too.
class ControllerAssignDialog : public QDialog {
public:
    explicit ControllerAssignDialog(InputCommon::InputSubsystem& input_subsystem,
                                    Common::ParamPackage device, QWidget* parent = nullptr);
    ~ControllerAssignDialog() override;

    std::size_t SelectedPlayer() const;
    std::string SelectedProfile() const;

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    enum class Row { Player, Buttons };

    void PollController();
    void Move(int dx, int dy);
    void ChangeProfile(int delta);
    void Activate();
    void UpdateView();

    InputCommon::InputSubsystem& input_subsystem;
    Common::ParamPackage device;

    std::array<QPushButton*, 8> player_tiles{};
    QLabel* profile_label{};
    QPushButton* profile_box{};
    QPushButton* confirm_button{};
    QPushButton* cancel_button{};
    QTimer poll_timer;

    std::vector<std::pair<QString, QString>> profiles; // label, profile name ("" = automatic)
    int player{};
    int profile{};
    int button{}; // 0 = confirm, 1 = cancel
    Row row{Row::Player};
};
