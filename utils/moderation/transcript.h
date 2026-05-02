#pragma once
#include <dpp/dpp.h>
#include <string>
#include <functional>

namespace bronx {
namespace moderation {

/**
 * @brief Generates a transcript for a modmail thread and uploads it to the log channel.
 * 
 * @param bot Reference to the dpp::cluster
 * @param thread_id The ID of the modmail thread (channel)
 * @param log_channel_id The ID of the channel to upload the transcript to
 * @param user_id The ID of the user the thread was with
 * @param callback Callback for success/failure
 */
void create_modmail_transcript(dpp::cluster& bot, uint64_t thread_id, uint64_t log_channel_id, uint64_t user_id, std::function<void(bool)> callback = nullptr);

} // namespace moderation
} // namespace bronx
