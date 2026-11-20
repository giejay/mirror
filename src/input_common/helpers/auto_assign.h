// SPDX-FileCopyrightText: Copyright 2026 Fritchii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace Common {
class ParamPackage;
}

namespace InputCommon {

class InputSubsystem;

/// Buttons a controller sends to the controller assignment window.
enum class MenuButton { Up, Down, Left, Right, Accept, Back, PreviousPage, NextPage };

/**
 * Automatically assigns connected game controllers (SDL devices) to player slots.
 *
 * Behaviour (only when Settings::values.auto_assign_controllers is enabled):
 *  - A player slot whose bindings already point at a connected controller keeps it, so
 *    controllers never jump between players while playing.
 *  - Every connected controller that is not bound to any slot is given the first free slot
 *    (up to auto_assign_max_players). Its bindings are taken from the input profile named in
 *    auto_assign_profile (re-targeted to the new controller), or, when no profile is set or it
 *    can't be read, from SDL's gamepad database (positional Xbox -> Switch layout).
 *  - With auto_assign_require_input, a controller only counts once it has pressed a button, so
 *    virtual pads left behind by streaming tools (Sunshine, Steam) never take a slot.
 *  - Slots 2..N whose controller went away are disconnected, so games don't see ghost players.
 *    Their bindings are kept so the same controller resumes the slot when it reconnects.
 *
 * Only Settings::values.players is modified. The caller must reload the emulated controllers
 * (HIDCore::ReloadInputDevices) when this returns true.
 *
 * @returns true when any player configuration was changed.
 */
bool AutoAssignControllers(const InputSubsystem& input_subsystem);

/// Returns the names of the input profiles stored in <config>/input/*.ini.
std::vector<std::string> GetAutoAssignProfileNames();

/**
 * Binds a controller (a device ParamPackage from GetInputDevices) to one player slot, using
 * the given input profile or, when empty, the automatic layout. If the controller was in another
 * slot, the two slots swap. Only Settings::values.players is changed.
 */
bool AssignControllerToPlayer(const InputSubsystem& input_subsystem,
                              const Common::ParamPackage& device, std::size_t player_index,
                              const std::string& profile_name);

/// Player slot (0-7) the controller is currently bound to, if any.
std::optional<std::size_t> FindPlayerForDevice(const Common::ParamPackage& device);

/// Name of the connected controller bound to the player slot, or empty.
std::string GetPlayerControllerName(const InputSubsystem& input_subsystem,
                                    std::size_t player_index);

} // namespace InputCommon
