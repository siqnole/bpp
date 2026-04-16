#pragma once

#include <dpp/dpp.h>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include "../../command.h"
#include "../../embed_style.h"
#include "../../feature_gate.h"
#include "../../database/core/database.h"
#include "../../database/operations/moderation/feature_flag_operations.h"

namespace commands {

// ────────────────────────────────────────────────────────────────────────
// !feature command — owner-only runtime feature flag management
//
// Subcommands:
//   !feature list                                  — show all flags
//   !feature set <name> <mode> [reason...]         — set flag mode (enabled|disabled|whitelist)
//   !feature delete <name>                         — remove a flag (returns to default enabled)
//   !feature wl add <name> <guild_id>              — add guild to whitelist
//   !feature wl remove <name> <guild_id>           — remove guild from whitelist
//   !feature reload                                — force reload from DB
// ────────────────────────────────────────────────────────────────────────

inline Command* get_feature_command(bronx::db::Database* db) {
    static Command cmd(
        "feature", "manage runtime feature flags", "owner",
        {"ff", "featureflag", "killswitch"}, false,
        [db](dpp::cluster& bot, const dpp::message_create_t& event, const std::vector<std::string>& args) {
            // Owner-only
            extern bool is_owner(uint64_t);
            if (!is_owner(event.msg.author.id)) {
                bronx::send_message(bot, event, bronx::error("owner only"));
                return;
            }

            if (args.empty()) {
                // Show usage
                auto embed = bronx::create_embed(
                    "**feature flag management**\n\n"
                    "`feature list` — show all flags\n"
                    "`feature set <name> <mode> [reason]` — set flag mode\n"
                    "`feature delete <name>` — remove flag\n"
                    "`feature wl add <name> <guild_id>` — whitelist a guild\n"
                    "`feature wl remove <name> <guild_id>` — un-whitelist\n"
                    "`feature reload` — force reload cache\n\n"
                    "**modes:** `enabled` (everyone), `disabled` (nobody), `whitelist` (only whitelisted guilds)",
                    bronx::COLOR_INFO
                );
                embed.set_title("⚑ Feature Flags");
                bronx::send_message(bot, event, embed);
                return;
            }

            std::string sub = args[0];
            std::transform(sub.begin(), sub.end(), sub.begin(), ::tolower);

            // ── LIST ────────────────────────────────────────────────
            if (sub == "list" || sub == "ls") {
                auto features = bronx::FeatureGate::get().get_all();

                if (features.empty()) {
                    bronx::send_message(bot, event,
                        bronx::info("no feature flags configured — all features are in default (enabled) state"));
                    return;
                }

                std::ostringstream oss;
                for (auto& [name, state] : features) {
                    std::string mode_emoji;
                    switch (state.mode) {
                        case bronx::FeatureMode::ENABLED:   mode_emoji = "🟢"; break;
                        case bronx::FeatureMode::DISABLED:  mode_emoji = "🔴"; break;
                        case bronx::FeatureMode::WHITELIST: mode_emoji = "🟡"; break;
                    }
                    oss << mode_emoji << " **" << name << "** — `"
                        << bronx::feature_mode_to_string(state.mode) << "`";
                    if (!state.reason.empty()) {
                        oss << " — *" << state.reason << "*";
                    }
                    if (state.mode == bronx::FeatureMode::WHITELIST && !state.whitelist.empty()) {
                        oss << "\n   └ whitelist: ";
                        int i = 0;
                        for (uint64_t gid : state.whitelist) {
                            if (i++ > 0) oss << ", ";
                            oss << "`" << gid << "`";
                        }
                    }
                    oss << "\n";
                }

                auto embed = bronx::create_embed(oss.str(), bronx::COLOR_INFO);
                embed.set_title("⚑ Feature Flags (" + std::to_string(features.size()) + ")");
                bronx::send_message(bot, event, embed);
                return;
            }

            // ── SET ─────────────────────────────────────────────────
            if (sub == "set") {
                if (args.size() < 3) {
                    bronx::send_message(bot, event,
                        bronx::error("usage: `feature set <name> <enabled|disabled|whitelist> [reason]`"));
                    return;
                }

                std::string name = args[1];
                std::string mode_str = args[2];
                std::transform(mode_str.begin(), mode_str.end(), mode_str.begin(), ::tolower);

                // Build reason from remaining args
                std::string reason;
                for (size_t i = 3; i < args.size(); i++) {
                    if (!reason.empty()) reason += " ";
                    reason += args[i];
                }

                auto mode = bronx::string_to_feature_mode(mode_str);

                if (bronx::FeatureGate::get().set_mode(name, mode, reason)) {
                    std::string mode_emoji = mode == bronx::FeatureMode::ENABLED ? "🟢" :
                                             mode == bronx::FeatureMode::DISABLED ? "🔴" : "🟡";
                    std::string msg = mode_emoji + " **" + name + "** → `"
                                      + bronx::feature_mode_to_string(mode) + "`";
                    if (!reason.empty()) msg += "\nreason: *" + reason + "*";

                    if (mode == bronx::FeatureMode::DISABLED) {
                        msg += "\n\n⚠️ **all guilds** are now blocked from using this feature";
                    } else if (mode == bronx::FeatureMode::WHITELIST) {
                        msg += "\n\n⚠️ only whitelisted guilds can use this feature\n"
                               "use `feature wl add " + name + " <guild_id>` to whitelist guilds";
                    }

                    bronx::send_message(bot, event, bronx::success(msg));
                } else {
                    bronx::send_message(bot, event, bronx::error("failed to set feature flag"));
                }
                return;
            }

            // ── DELETE ──────────────────────────────────────────────
            if (sub == "delete" || sub == "remove" || sub == "rm") {
                if (args.size() < 2) {
                    bronx::send_message(bot, event, bronx::error("usage: `feature delete <name>`"));
                    return;
                }

                std::string name = args[1];
                if (bronx::FeatureGate::get().remove_feature(name)) {
                    bronx::send_message(bot, event,
                        bronx::success("🗑️ **" + name + "** deleted — feature returns to default (enabled)"));
                } else {
                    bronx::send_message(bot, event, bronx::error("failed to delete feature flag"));
                }
                return;
            }

            // ── WHITELIST ───────────────────────────────────────────
            if (sub == "wl" || sub == "whitelist") {
                if (args.size() < 4) {
                    bronx::send_message(bot, event,
                        bronx::error("usage: `feature wl <add|remove> <name> <guild_id>`"));
                    return;
                }

                std::string action = args[1];
                std::transform(action.begin(), action.end(), action.begin(), ::tolower);
                std::string name = args[2];
                uint64_t guild_id = 0;
                try { guild_id = std::stoull(args[3]); }
                catch (...) {
                    bronx::send_message(bot, event, bronx::error("invalid guild id"));
                    return;
                }

                if (action == "add") {
                    if (bronx::FeatureGate::get().add_whitelist(name, guild_id)) {
                        bronx::send_message(bot, event,
                            bronx::success("added `" + std::to_string(guild_id)
                                + "` to **" + name + "** whitelist"));
                    } else {
                        bronx::send_message(bot, event, bronx::error("failed to add to whitelist"));
                    }
                } else if (action == "remove" || action == "rm") {
                    if (bronx::FeatureGate::get().remove_whitelist(name, guild_id)) {
                        bronx::send_message(bot, event,
                            bronx::success("removed `" + std::to_string(guild_id)
                                + "` from **" + name + "** whitelist"));
                    } else {
                        bronx::send_message(bot, event, bronx::error("failed to remove from whitelist"));
                    }
                } else {
                    bronx::send_message(bot, event,
                        bronx::error("usage: `feature wl <add|remove> <name> <guild_id>`"));
                }
                return;
            }

            // ── RELOAD ──────────────────────────────────────────────
            if (sub == "reload" || sub == "refresh") {
                bronx::FeatureGate::get().reload();
                bronx::send_message(bot, event,
                    bronx::success("🔄 feature flag cache reloaded from database"));
                return;
            }

            bronx::send_message(bot, event,
                bronx::error("unknown subcommand `" + sub + "` — try `feature` for help"));
        }
    );

    // extended help
    cmd.extended_description = 
        "runtime feature flag system for gating commands and features.\n\n"
        "**use cases:**\n"
        "• kill switch — disable a broken command globally\n"
        "• beta gating — restrict a feature to whitelisted servers\n"
        "• gradual rollout — whitelist servers one at a time\n\n"
        "**modes:**\n"
        "🟢 `enabled` — available to all guilds (default)\n"
        "🔴 `disabled` — blocked everywhere (kill switch)\n"
        "🟡 `whitelist` — only whitelisted guilds can use it";
    
    cmd.examples = {
        "feature set fishing disabled critical bug in production",
        "feature set logging whitelist beta testing phase",
        "feature wl add logging 123456789012345678",
        "feature set fishing enabled bug fixed",
        "feature delete logging"
    };

    return &cmd;
}

} // namespace commands
