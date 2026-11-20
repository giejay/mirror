// SPDX-FileCopyrightText: Copyright 2026 Fritchii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <optional>
#include <set>
#include <string_view>
#include <utility>

#include <format>

#include "common/fs/fs.h"
#include "common/fs/path_util.h"
#include "common/logging.h"
#include "common/param_package.h"
#include "common/settings.h"
#include "input_common/helpers/auto_assign.h"
#include "input_common/main.h"

namespace InputCommon {

namespace {

namespace FS = Common::FS;

// Player slots 0-7 are the eight regular players. 8 is "Other" (debug) and 9 is handheld.
constexpr std::size_t MaxRegularPlayers = 8;
constexpr std::string_view SdlEngine = "sdl";

struct DeviceKey {
    std::string guid;
    int port{};

    auto operator<=>(const DeviceKey&) const = default;
};

std::optional<DeviceKey> KeyFromParam(const Common::ParamPackage& param) {
    if (param.Get("engine", "") != SdlEngine || !param.Has("guid")) {
        return std::nullopt;
    }
    return DeviceKey{param.Get("guid", ""), param.Get("port", 0)};
}

/// Returns the SDL device the player's bindings point at, if any.
std::optional<DeviceKey> GetBoundDevice(const Settings::PlayerInput& player) {
    for (const auto& button : player.buttons) {
        if (auto key = KeyFromParam(Common::ParamPackage{button})) {
            return key;
        }
    }
    for (const auto& analog : player.analogs) {
        if (auto key = KeyFromParam(Common::ParamPackage{analog})) {
            return key;
        }
    }
    return std::nullopt;
}

std::filesystem::path GetProfileDir() {
    return FS::GetEdenPath(FS::EdenPath::ConfigDir) / "input";
}

std::string Trim(std::string_view s) {
    const auto begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string_view::npos) {
        return {};
    }
    const auto end = s.find_last_not_of(" \t\r\n");
    return std::string{s.substr(begin, end - begin + 1)};
}

/// Minimal reader for the [Controls] section of an input profile written by the config code.
/// Returns key -> value with quotes stripped, mirroring Config::ReadStringSetting.
std::optional<Common::unordered_map<std::string, std::string>> ReadProfile(
    const std::string& profile_name) {
    const auto path = GetProfileDir() / std::format("{}.ini", profile_name);
    std::ifstream file{path};
    if (!file.is_open()) {
        return std::nullopt;
    }

    Common::unordered_map<std::string, std::string> values;
    std::string section;
    std::string line;
    while (std::getline(file, line)) {
        const auto trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#') {
            continue;
        }
        if (trimmed.front() == '[' && trimmed.back() == ']') {
            section = trimmed.substr(1, trimmed.size() - 2);
            continue;
        }
        if (section != "Controls") {
            continue;
        }
        const auto eq = trimmed.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        std::string key = Trim(std::string_view{trimmed}.substr(0, eq));
        std::string value = Trim(std::string_view{trimmed}.substr(eq + 1));
        std::erase(value, '"');
        for (std::size_t pos; (pos = value.find("//")) != std::string::npos;) {
            value.replace(pos, 2, "/");
        }
        values.insert_or_assign(std::move(key), std::move(value));
    }
    return values;
}

/// Points an SDL binding (and any nested SDL bindings) at a different controller.
std::string Retarget(const std::string& serialized, const DeviceKey& device) {
    if (serialized.empty()) {
        return serialized;
    }
    Common::ParamPackage param{serialized};
    if (param.Get("engine", "") != SdlEngine) {
        // Keyboard/mouse bindings in a profile make no sense for an auto-assigned controller.
        return {};
    }
    param.Set("guid", device.guid);
    param.Set("port", device.port);
    // Sticks built from buttons or modifiers carry nested param packages.
    for (const char* nested : {"up", "down", "left", "right", "modifier"}) {
        if (!param.Has(nested)) {
            continue;
        }
        Common::ParamPackage inner{param.Get(nested, "")};
        if (inner.Get("engine", "") == SdlEngine) {
            inner.Set("guid", device.guid);
            inner.Set("port", device.port);
            param.Set(nested, inner.Serialize());
        }
    }
    return param.Serialize();
}

bool ApplyProfile(Settings::PlayerInput& player, const DeviceKey& device,
                  const std::string& profile_name) {
    const auto profile = ReadProfile(profile_name);
    if (!profile) {
        LOG_WARNING(Input, "Auto-assign: input profile '{}' could not be read, using automap",
                    profile_name);
        return false;
    }

    // A value is only meaningful when it isn't flagged as the (keyboard) default.
    const auto get = [&](const std::string& key) -> std::string {
        const auto default_it = profile->find(key + "\\default");
        if (default_it != profile->end() && default_it->second == "true") {
            return {};
        }
        const auto it = profile->find(key);
        return it != profile->end() ? it->second : std::string{};
    };

    bool any_sdl_binding = false;
    for (std::size_t i = 0; i < player.buttons.size(); ++i) {
        player.buttons[i] = Retarget(get(Settings::NativeButton::mapping[i]), device);
        any_sdl_binding |= !player.buttons[i].empty();
    }
    for (std::size_t i = 0; i < player.analogs.size(); ++i) {
        player.analogs[i] = Retarget(get(Settings::NativeAnalog::mapping[i]), device);
        any_sdl_binding |= !player.analogs[i].empty();
    }
    for (std::size_t i = 0; i < player.motions.size(); ++i) {
        player.motions[i] = Retarget(get(Settings::NativeMotion::mapping[i]), device);
    }

    if (!any_sdl_binding) {
        LOG_WARNING(Input, "Auto-assign: profile '{}' has no controller bindings, using automap",
                    profile_name);
        return false;
    }
    return true;
}

void ApplyAutomap(const InputSubsystem& input, Settings::PlayerInput& player,
                  const Common::ParamPackage& device) {
    for (auto& button : player.buttons) {
        button.clear();
    }
    for (auto& analog : player.analogs) {
        analog.clear();
    }
    for (auto& motion : player.motions) {
        motion.clear();
    }

    for (const auto& [index, param] : input.GetButtonMappingForDevice(device)) {
        player.buttons[static_cast<std::size_t>(index)] = param.Serialize();
    }
    for (const auto& [index, param] : input.GetAnalogMappingForDevice(device)) {
        player.analogs[static_cast<std::size_t>(index)] = param.Serialize();
    }
    for (const auto& [index, param] : input.GetMotionMappingForDevice(device)) {
        player.motions[static_cast<std::size_t>(index)] = param.Serialize();
    }
}

/// Writes bindings for the controller into the player. Returns true when the profile was used.
bool ApplyMapping(const InputSubsystem& input, Settings::PlayerInput& player,
                  const DeviceKey& key, const Common::ParamPackage& device,
                  const std::string& profile_name) {
    const bool used_profile = !profile_name.empty() && ApplyProfile(player, key, profile_name);
    if (!used_profile) {
        ApplyAutomap(input, player, device);
    }
    player.connected = true;
    if (player.controller_type == Settings::ControllerType::Handheld) {
        player.controller_type = Settings::ControllerType::ProController;
    }
    return used_profile;
}

bool IsActive(const Common::ParamPackage& device) {
    return !Settings::values.auto_assign_require_input.GetValue() ||
           device.Get("active", "1") == "1";
}

/// Connected SDL controllers (only the ones that pressed a button when that is required).
std::vector<std::pair<DeviceKey, Common::ParamPackage>> GetUsableDevices(
    const InputSubsystem& input) {
    std::vector<std::pair<DeviceKey, Common::ParamPackage>> devices;
    for (const auto& device : input.GetInputDevices()) {
        if (device.Has("guid2") || !IsActive(device)) {
            continue; // Combined dual Joy-Con entries are listed as single halves too.
        }
        if (auto key = KeyFromParam(device)) {
            devices.emplace_back(std::move(*key), device);
        }
    }
    std::sort(devices.begin(), devices.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    return devices;
}

} // Anonymous namespace

std::vector<std::string> GetAutoAssignProfileNames() {
    std::vector<std::string> names;
    const auto dir = GetProfileDir();
    if (!FS::IsDir(dir)) {
        return names;
    }
    FS::IterateDirEntries(
        dir,
        [&names](const std::filesystem::path& full_path) {
            if (full_path.extension() == ".ini") {
                names.push_back(FS::PathToUTF8String(full_path.stem()));
            }
            return true;
        },
        FS::DirEntryFilter::File);
    std::sort(names.begin(), names.end());
    return names;
}

bool AutoAssignControllers(const InputSubsystem& input_subsystem) {
    if (!Settings::values.auto_assign_controllers.GetValue()) {
        return false;
    }

    const std::size_t max_players = std::clamp<std::size_t>(
        Settings::values.auto_assign_max_players.GetValue(), 1, MaxRegularPlayers);
    const std::string profile_name = Settings::values.auto_assign_profile.GetValue();
    auto& players = Settings::values.players.GetValue();

    // Usable SDL controllers, in a stable order (by GUID, then by port).
    const auto connected = GetUsableDevices(input_subsystem);

    const auto is_connected = [&connected](const DeviceKey& key) {
        return std::any_of(connected.begin(), connected.end(),
                           [&key](const auto& entry) { return entry.first == key; });
    };

    bool changed = false;
    std::set<DeviceKey> in_use;
    std::array<bool, MaxRegularPlayers> slot_live{};

    // Pass 1: keep slots whose controller is still here.
    for (std::size_t i = 0; i < max_players; ++i) {
        auto& player = players[i];
        const auto bound = GetBoundDevice(player);
        if (bound && is_connected(*bound) && !in_use.contains(*bound)) {
            in_use.insert(*bound);
            slot_live[i] = true;
            if (!player.connected) {
                player.connected = true;
                changed = true;
            }
        }
    }

    // Pass 2: hand out the remaining controllers to the free slots, lowest slot first.
    std::size_t next_slot = 0;
    for (const auto& [key, device] : connected) {
        if (in_use.contains(key)) {
            continue;
        }
        while (next_slot < max_players && slot_live[next_slot]) {
            ++next_slot;
        }
        if (next_slot >= max_players) {
            break;
        }

        auto& player = players[next_slot];
        const bool used_profile = ApplyMapping(input_subsystem, player, key, device, profile_name);

        LOG_INFO(Input, "Auto-assign: '{}' -> Player {} ({})", device.Get("display", "?"),
                 next_slot + 1, used_profile ? std::format("profile {}", profile_name)
                                             : std::string{"automap"});

        in_use.insert(key);
        slot_live[next_slot] = true;
        changed = true;
    }

    // Pass 3: players 2..N that lost their controller are disconnected (bindings are kept so
    // the same controller resumes the slot on reconnect). Player 1 always stays connected.
    for (std::size_t i = 1; i < max_players; ++i) {
        auto& player = players[i];
        if (slot_live[i] || !player.connected) {
            continue;
        }
        if (GetBoundDevice(player)) {
            player.connected = false;
            changed = true;
            LOG_INFO(Input, "Auto-assign: Player {} controller is gone, disconnecting", i + 1);
        }
    }

    return changed;
}

std::optional<std::size_t> FindPlayerForDevice(const Common::ParamPackage& device) {
    const auto key = KeyFromParam(device);
    if (!key) {
        return std::nullopt;
    }
    const auto& players = Settings::values.players.GetValue();
    for (std::size_t i = 0; i < MaxRegularPlayers; ++i) {
        if (GetBoundDevice(players[i]) == key) {
            return i;
        }
    }
    return std::nullopt;
}

std::string GetPlayerControllerName(const InputSubsystem& input_subsystem,
                                    std::size_t player_index) {
    if (player_index >= MaxRegularPlayers) {
        return {};
    }
    const auto bound = GetBoundDevice(Settings::values.players.GetValue()[player_index]);
    if (!bound) {
        return {};
    }
    for (const auto& device : input_subsystem.GetInputDevices()) {
        if (KeyFromParam(device) == bound && !device.Has("guid2")) {
            return device.Get("display", "");
        }
    }
    return {};
}

bool AssignControllerToPlayer(const InputSubsystem& input_subsystem,
                              const Common::ParamPackage& device, std::size_t player_index,
                              const std::string& profile_name) {
    const auto key = KeyFromParam(device);
    if (!key || player_index >= MaxRegularPlayers) {
        return false;
    }
    auto& players = Settings::values.players.GetValue();
    const auto old_index = FindPlayerForDevice(device);

    const Settings::PlayerInput previous = players[player_index];
    const bool used_profile =
        ApplyMapping(input_subsystem, players[player_index], *key, device, profile_name);

    if (old_index && *old_index != player_index) {
        // Swap: the controller that had the target slot moves to the slot this one came from.
        auto& old_slot = players[*old_index];
        old_slot.buttons = previous.buttons;
        old_slot.analogs = previous.analogs;
        old_slot.motions = previous.motions;
        const auto previous_device = GetBoundDevice(previous);
        bool previous_usable = false;
        if (previous_device) {
            for (const auto& [usable_key, usable] : GetUsableDevices(input_subsystem)) {
                previous_usable |= usable_key == *previous_device;
            }
        }
        old_slot.connected = *old_index == 0 || previous_usable;
    }

    LOG_INFO(Input, "Controller assign: '{}' -> Player {} ({})", device.Get("display", "?"),
             player_index + 1,
             used_profile ? std::format("profile {}", profile_name) : std::string{"automap"});
    return true;
}

} // namespace InputCommon
