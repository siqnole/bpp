#pragma once
#include "../../core/database.h"
#include "../../core/types.h"
#include <optional>
#include <vector>

namespace bronx {
namespace db {

// Modmail Config
std::optional<ModmailConfig> get_modmail_config(Database* db, uint64_t guild_id);
bool set_modmail_config(Database* db, const ModmailConfig& config);

// Modmail Threads
std::optional<ModmailThread> get_any_active_modmail_thread(Database* db, uint64_t user_id);
std::optional<ModmailThread> get_active_modmail_thread_by_user(Database* db, uint64_t user_id, uint64_t guild_id);
std::optional<ModmailThread> get_modmail_thread_by_id(Database* db, uint64_t thread_id);
bool create_modmail_thread(Database* db, uint64_t guild_id, uint64_t user_id, uint64_t thread_id);
bool close_modmail_thread(Database* db, uint64_t thread_id);

} // namespace db
} // namespace bronx
