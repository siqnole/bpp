#include "transcript.h"
#include <sstream>
#include <iomanip>
#include <chrono>

namespace bronx {
namespace moderation {

void create_modmail_transcript(dpp::cluster& bot, uint64_t thread_id, uint64_t log_channel_id, uint64_t user_id, std::function<void(bool)> callback) {
    bot.messages_get(thread_id, 0, 0, 0, 100, [&bot, thread_id, log_channel_id, user_id, callback](const dpp::confirmation_callback_t& cb) {
        if (cb.is_error()) {
            if (callback) callback(false);
            return;
        }

        auto messages = std::get<dpp::message_map>(cb.value);
        if (messages.empty()) {
            if (callback) callback(true);
            return;
        }

        // Sort messages by time (message_map is ordered by ID, which is chronological)
        std::stringstream ss;
        ss << "========================================================\n";
        ss << " MODMAIL TRANSCRIPT - USER ID: " << user_id << "\n";
        ss << " THREAD ID: " << thread_id << "\n";
        ss << " GENERATED: " << dpp::utility::current_date_time() << "\n";
        ss << "========================================================\n\n";

        for (auto const& [id, msg] : messages) {
            std::string timestamp = dpp::utility::current_date_time(); // Simple timestamp
            // Note: msg.sent is double in some versions, or time_t. 
            // In D++ 10.x it's time_t sent.
            
            char mbstr[100];
            if (std::strftime(mbstr, sizeof(mbstr), "%Y-%m-%d %H:%M:%S", std::localtime(&msg.sent))) {
                timestamp = mbstr;
            }

            ss << "[" << timestamp << "] " << msg.author.username << ": " << msg.content << "\n";
            for (auto const& embed : msg.embeds) {
                if (!embed.description.empty()) ss << "  [Embed Description]: " << embed.description << "\n";
                for (auto const& field : embed.fields) {
                    ss << "  [Embed Field] " << field.name << ": " << field.value << "\n";
                }
            }
            if (!msg.attachments.empty()) {
                ss << "  [Attachments]: ";
                for (auto const& att : msg.attachments) ss << att.url << " ";
                ss << "\n";
            }
            ss << "\n";
        }

        std::string transcript = ss.str();
        dpp::message m(log_channel_id, "");
        m.add_file("transcript_" + std::to_string(thread_id) + ".txt", transcript);
        
        dpp::embed e;
        e.set_title("Modmail Transcript")
         .set_color(0x3498db)
         .add_field("User", "<@" + std::to_string(user_id) + ">", true)
         .add_field("Thread", "<#" + std::to_string(thread_id) + ">", true)
         .set_timestamp(time(0));
        m.add_embed(e);

        bot.message_create(m, [callback](const dpp::confirmation_callback_t& res) {
            if (callback) callback(!res.is_error());
        });
    });
}

} // namespace moderation
} // namespace bronx
