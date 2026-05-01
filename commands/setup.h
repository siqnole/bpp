#pragma once
#include <dpp/dpp.h>
#include <string>
#include <map>
#include <mutex>
#include "../database/core/database.h"

namespace commands {
namespace setup {

using namespace bronx::db;

struct SetupState {
    std::string step = "welcome";
    std::map<std::string, std::string> config;
    uint64_t admin_id = 0;
};

extern std::map<uint64_t, SetupState> active_setups;
extern std::recursive_mutex setup_mutex;

/**
 * @brief Checks if a user has Administrator permission in a guild.
 */
bool has_admin_permission(dpp::cluster& bot, uint64_t guild_id, uint64_t user_id);

/**
 * @brief Sends the initial welcome message with setup buttons.
 */
void send_welcome_message(dpp::cluster& bot, uint64_t guild_id, uint64_t channel_id, uint64_t admin_id);

/**
 * @brief Main entry point for setup button interactions.
 */
void handle_setup_button(dpp::cluster& bot, const dpp::button_click_t& event, Database* db);

// Helper steps (internal but exposed for modularity if needed)
void send_economy_setup(dpp::cluster& bot, const dpp::button_click_t& event, uint64_t guild_id);
void send_features_setup(dpp::cluster& bot, const dpp::button_click_t& event, uint64_t guild_id, const std::string& economy_mode);
void send_leveling_setup(dpp::cluster& bot, const dpp::button_click_t& event, uint64_t guild_id, Database* db);
void send_moderation_setup(dpp::cluster& bot, const dpp::button_click_t& event, uint64_t guild_id, Database* db);
void send_moderation_toggles(dpp::cluster& bot, const dpp::button_click_t& event, uint64_t guild_id);
void send_moderation_customization(dpp::cluster& bot, const dpp::button_click_t& event, uint64_t guild_id);
void send_prefix_setup(dpp::cluster& bot, const dpp::button_click_t& event, uint64_t guild_id, Database* db);
void send_completion(dpp::cluster& bot, const dpp::button_click_t& event, uint64_t guild_id, Database* db);
void handle_skip_setup(dpp::cluster& bot, const dpp::button_click_t& event, uint64_t guild_id);

} // namespace setup
} // namespace commands
