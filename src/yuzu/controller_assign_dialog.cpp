// SPDX-FileCopyrightText: Copyright 2026 Fritchii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>

#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QStyle>
#include <QVBoxLayout>

#include "common/settings.h"
#include "input_common/helpers/auto_assign.h"
#include "input_common/main.h"
#include "yuzu/controller_assign_dialog.h"

namespace {

QFont Scaled(QFont font, double pixel_size, bool bold = false) {
    font.setPixelSize(static_cast<int>(pixel_size));
    font.setBold(bold);
    return font;
}

} // namespace

ControllerAssignDialog::ControllerAssignDialog(InputCommon::InputSubsystem& input_subsystem_,
                                               Common::ParamPackage device_, QWidget* parent)
    : QDialog(parent), input_subsystem{input_subsystem_}, device{std::move(device_)} {
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setModal(true);

    profiles.emplace_back(tr("Automatic"), QString{});
    for (const auto& name : InputCommon::GetAutoAssignProfileNames()) {
        const auto qname = QString::fromStdString(name);
        profiles.emplace_back(qname, qname);
    }
    const auto default_profile =
        QString::fromStdString(Settings::values.auto_assign_profile.GetValue());
    for (std::size_t i = 0; i < profiles.size(); ++i) {
        if (profiles[i].second == default_profile) {
            profile = static_cast<int>(i);
        }
    }
    const auto current_player = InputCommon::FindPlayerForDevice(device);
    player = current_player ? static_cast<int>(*current_player) : 0;

    auto* card = new QWidget(this);
    card->setObjectName(QStringLiteral("assignCard"));
    card->setAttribute(Qt::WA_StyledBackground);

    auto* title = new QLabel(tr("Assign controller"));
    title->setObjectName(QStringLiteral("assignTitle"));
    auto* controller_name =
        new QLabel(QString::fromStdString(device.Get("display", "Controller")));
    controller_name->setObjectName(QStringLiteral("assignSubtitle"));

    auto* players_grid = new QGridLayout;
    players_grid->setSpacing(16);
    for (int i = 0; i < 8; ++i) {
        auto* tile = new QPushButton;
        tile->setObjectName(QStringLiteral("assignTile"));
        tile->setFocusPolicy(Qt::NoFocus);
        tile->setMinimumSize(150, 110);
        tile->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        const auto occupant = InputCommon::GetPlayerControllerName(input_subsystem, static_cast<std::size_t>(i));
        tile->setText(tr("Player %1").arg(i + 1) + QStringLiteral("\n") +
                      (occupant.empty() ? tr("free") : QString::fromStdString(occupant)));
        connect(tile, &QPushButton::clicked, this, [this, i] {
            player = i;
            row = Row::Player;
            UpdateView();
        });
        players_grid->addWidget(tile, i / 4, i % 4);
        player_tiles[static_cast<std::size_t>(i)] = tile;
    }

    profile_label = new QLabel(tr("Profile"));
    profile_label->setObjectName(QStringLiteral("assignSection"));
    profile_box = new QPushButton;
    profile_box->setObjectName(QStringLiteral("assignProfile"));
    profile_box->setFocusPolicy(Qt::NoFocus);
    connect(profile_box, &QPushButton::clicked, this, [this] { ChangeProfile(1); });

    confirm_button = new QPushButton(tr("Ⓐ  Confirm"));
    cancel_button = new QPushButton(tr("Ⓑ  Cancel"));
    for (auto* b : {confirm_button, cancel_button}) {
        b->setObjectName(QStringLiteral("assignButton"));
        b->setFocusPolicy(Qt::NoFocus);
        b->setMinimumHeight(64);
    }
    connect(confirm_button, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancel_button, &QPushButton::clicked, this, &QDialog::reject);

    auto* players_label = new QLabel(tr("Player"));
    players_label->setObjectName(QStringLiteral("assignSection"));

    auto* hint = new QLabel(tr("D-pad: player    LB / RB: profile    Ⓐ: confirm    Ⓑ: cancel"));
    hint->setObjectName(QStringLiteral("assignHint"));
    hint->setAlignment(Qt::AlignCenter);

    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(16);
    buttons->addWidget(confirm_button);
    buttons->addWidget(cancel_button);

    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(40, 32, 40, 32);
    layout->setSpacing(14);
    layout->addWidget(title);
    layout->addWidget(controller_name);
    layout->addSpacing(8);
    layout->addWidget(players_label);
    layout->addLayout(players_grid, 1);
    layout->addSpacing(8);
    layout->addWidget(profile_label);
    layout->addWidget(profile_box);
    layout->addSpacing(8);
    layout->addLayout(buttons);
    layout->addWidget(hint);

    auto* outer = new QVBoxLayout(this);
    outer->addStretch(1);
    auto* center = new QHBoxLayout;
    center->addStretch(1);
    center->addWidget(card, 8);
    center->addStretch(1);
    outer->addLayout(center, 8);
    outer->addStretch(1);

    input_subsystem.SetMenuDevice(device);
    connect(&poll_timer, &QTimer::timeout, this, [this] { PollController(); });
    poll_timer.start(16);
    UpdateView();
}

ControllerAssignDialog::~ControllerAssignDialog() {
    poll_timer.stop();
    input_subsystem.SetMenuDevice(std::nullopt);
}

std::size_t ControllerAssignDialog::SelectedPlayer() const {
    return static_cast<std::size_t>(player);
}

std::string ControllerAssignDialog::SelectedProfile() const {
    return profiles[static_cast<std::size_t>(profile)].second.toStdString();
}

void ControllerAssignDialog::showEvent(QShowEvent* event) {
    // Cover the window it was opened over (the game), or the screen when there is none.
    QRect area;
    if (auto* top = parentWidget() ? parentWidget()->window() : nullptr) {
        area = top->frameGeometry();
    } else if (auto* screen = QGuiApplication::primaryScreen()) {
        area = screen->geometry();
    }
    if (area.isValid()) {
        setGeometry(area);
    }

    // Scale text with the window so it reads from the couch.
    const double unit = std::clamp(area.height() / 40.0, 14.0, 34.0);
    const QColor accent = palette().color(QPalette::Highlight);
    const QColor on_accent = palette().color(QPalette::HighlightedText);
    setStyleSheet(QStringLiteral(R"(
        #assignCard { background: #17161c; border: 1px solid #2c2a35; border-radius: 28px; }
        #assignTitle { color: #f2f0f7; }
        #assignSubtitle, #assignSection, #assignHint { color: #9a97a6; }
        QPushButton#assignTile, QPushButton#assignProfile, QPushButton#assignButton {
            background: #211f28; color: #e8e6ef; border: 3px solid #2c2a35; border-radius: 18px;
            padding: 12px;
        }
        QPushButton#assignTile[chosen="true"], QPushButton#assignButton[chosen="true"] {
            background: %1; color: %2; border-color: %1;
        }
        QPushButton#assignTile[focusRing="true"], QPushButton#assignProfile[focusRing="true"],
        QPushButton#assignButton[focusRing="true"] { border-color: #f2f0f7; }
    )")
                      .arg(accent.name(), on_accent.name()));

    const QFont base = font();
    findChild<QLabel*>(QStringLiteral("assignTitle"))->setFont(Scaled(base, unit * 1.6, true));
    findChild<QLabel*>(QStringLiteral("assignSubtitle"))->setFont(Scaled(base, unit));
    for (auto* label : findChildren<QLabel*>(QStringLiteral("assignSection"))) {
        label->setFont(Scaled(base, unit * 0.85, true));
    }
    findChild<QLabel*>(QStringLiteral("assignHint"))->setFont(Scaled(base, unit * 0.8));
    for (auto* tile : player_tiles) {
        tile->setFont(Scaled(base, unit, true));
    }
    profile_box->setFont(Scaled(base, unit * 1.1, true));
    confirm_button->setFont(Scaled(base, unit, true));
    cancel_button->setFont(Scaled(base, unit, true));

    QDialog::showEvent(event);
    activateWindow();
    raise();
}

void ControllerAssignDialog::paintEvent(QPaintEvent* event) {
    QPainter painter(this);
    painter.fillRect(rect(), QColor(0, 0, 0, 170));
    QDialog::paintEvent(event);
}

void ControllerAssignDialog::keyPressEvent(QKeyEvent* event) {
    switch (event->key()) {
    case Qt::Key_Left:
        Move(-1, 0);
        break;
    case Qt::Key_Right:
        Move(1, 0);
        break;
    case Qt::Key_Up:
        Move(0, -1);
        break;
    case Qt::Key_Down:
        Move(0, 1);
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
        Activate();
        break;
    case Qt::Key_Escape:
        reject();
        break;
    case Qt::Key_Q:
    case Qt::Key_PageUp:
        ChangeProfile(-1);
        break;
    case Qt::Key_E:
    case Qt::Key_PageDown:
        ChangeProfile(1);
        break;
    default:
        QDialog::keyPressEvent(event);
    }
}

void ControllerAssignDialog::Move(int dx, int dy) {
    switch (row) {
    case Row::Player:
        if (dx != 0) {
            player = (player + dx + 8) % 8;
        } else if (dy < 0 && player >= 4) {
            player -= 4;
        } else if (dy > 0 && player < 4) {
            player += 4;
        } else if (dy > 0) {
            row = Row::Buttons;
        }
        break;
    case Row::Buttons:
        if (dx != 0) {
            button = button == 0 ? 1 : 0;
        } else if (dy < 0) {
            row = Row::Player;
        }
        break;
    }
    UpdateView();
}

void ControllerAssignDialog::ChangeProfile(int delta) {
    const int count = static_cast<int>(profiles.size());
    profile = (profile + delta + count) % count;
    UpdateView();
}

void ControllerAssignDialog::Activate() {
    if (row == Row::Buttons && button == 1) {
        reject();
    } else {
        accept();
    }
}

void ControllerAssignDialog::UpdateView() {
    const auto set_state = [](QPushButton* widget, bool chosen, bool focus_ring) {
        widget->setProperty("chosen", chosen);
        widget->setProperty("focusRing", focus_ring);
        widget->style()->unpolish(widget);
        widget->style()->polish(widget);
    };
    for (int i = 0; i < 8; ++i) {
        set_state(player_tiles[static_cast<std::size_t>(i)], i == player,
                  i == player && row == Row::Player);
    }
    const bool many = profiles.size() > 1;
    profile_box->setText((many ? QStringLiteral("LB  ◀     ") : QString{}) +
                         profiles[static_cast<std::size_t>(profile)].first +
                         (many ? QStringLiteral("     ▶  RB") : QString{}));
    set_state(profile_box, false, false);
    set_state(confirm_button, button == 0, row == Row::Buttons && button == 0);
    set_state(cancel_button, button == 1, row == Row::Buttons && button == 1);
}

void ControllerAssignDialog::PollController() {
    // The input timer of the main window doesn't run while this window is open, so pump SDL here.
    input_subsystem.PumpEvents();
    for (const auto pressed : input_subsystem.TakeMenuButtons()) {
        switch (pressed) {
        case InputCommon::MenuButton::Up:
            Move(0, -1);
            break;
        case InputCommon::MenuButton::Down:
            Move(0, 1);
            break;
        case InputCommon::MenuButton::Left:
            Move(-1, 0);
            break;
        case InputCommon::MenuButton::Right:
            Move(1, 0);
            break;
        case InputCommon::MenuButton::Accept:
            Activate();
            return;
        case InputCommon::MenuButton::Back:
            reject();
            return;
        case InputCommon::MenuButton::PreviousPage:
            ChangeProfile(-1);
            break;
        case InputCommon::MenuButton::NextPage:
            ChangeProfile(1);
            break;
        }
    }
}
