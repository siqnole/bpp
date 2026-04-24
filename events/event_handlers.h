#pragma once

#include <dpp/dpp.h>
#include "../database/core/database.h"
#include "../performance/optimized_command_handler.h"
#include "../performance/async_stat_writer.h"
#include "../performance/xp_batch_writer.h"
#include "../performance/snipe_cache.h"
#include "../performance/message_cache.h"
#include <atomic>

// This flag indicates when the initial guild loading is complete
extern std::atomic<bool> g_initial_load_complete;

namespace commands {
    struct BotStats;
    extern BotStats global_stats;
}

void register_event_handlers(
    dpp::cluster& bot,
    OptimizedCommandHandler& cmd_handler,
    bronx::db::Database& db,
    bronx::perf::AsyncStatWriter& async_stat_writer,
    bronx::snipe::MessageCache& message_cache,
    bronx::snipe::SnipeCache& snipe_cache,
    bronx::xp::XpBatchWriter& xp_batch_writer,
    bool verbose_events
);
