#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include "utility_helpers.h"
#include <dpp/dpp.h>
#include <vector>
#include <map>
#include <set>
#include <sstream>

namespace commands {
namespace utility {

// Poll vote tracking structure
struct PollData {
    ::std::string question;
    ::std::vector<::std::string> options;
    ::std::map<int, ::std::set<uint64_t>> votes; // option_index -> set of user_ids
    ::std::vector<dpp::snowflake> required_roles;
    ::std::vector<dpp::snowflake> excluded_roles;
    uint64_t creator_id;
    dpp::snowflake message_id;
    dpp::snowflake channel_id;
};

// Global poll storage: message_id -> PollData
static ::std::map<uint64_t, PollData> active_polls;

// Poll command (slash only)
inline Command* get_poll_command() {
    static Command poll("poll", "create a poll with optional role-based voting restrictions", "utility", {}, true,
        nullptr, // No text command handler
        [](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            ::std::string question = ::std::get<::std::string>(event.get_parameter("question"));
            ::std::string option1 = ::std::get<::std::string>(event.get_parameter("option1"));
            ::std::string option2 = ::std::get<::std::string>(event.get_parameter("option2"));
            
            ::std::vector<::std::string> options;
            options.push_back(option1);
            options.push_back(option2);
            
            // Get optional options 3-10
            for (int i = 3; i <= 10; i++) {
                try {
                    auto opt = event.get_parameter("option" + ::std::to_string(i));
                    if (::std::holds_alternative<::std::string>(opt)) {
                        ::std::string option_value = ::std::get<::std::string>(opt);
                        if (!option_value.empty()) {
                            options.push_back(option_value);
                        }
                    }
                } catch (...) {
                    break;
                }
            }
            
            // Get role restrictions
            ::std::vector<dpp::snowflake> required_roles;
            ::std::vector<dpp::snowflake> excluded_roles;
            
            // Get up to 3 required roles
            for (int i = 1; i <= 3; i++) {
                try {
                    ::std::string param_name = (i == 1) ? "require_role" : "require_role" + ::std::to_string(i);
                    auto require_param = event.get_parameter(param_name);
                    if (::std::holds_alternative<dpp::snowflake>(require_param)) {
                        required_roles.push_back(::std::get<dpp::snowflake>(require_param));
                    }
                } catch (...) {}
            }
            
            // Get up to 3 excluded roles
            for (int i = 1; i <= 3; i++) {
                try {
                    ::std::string param_name = (i == 1) ? "exclude_role" : "exclude_role" + ::std::to_string(i);
                    auto exclude_param = event.get_parameter(param_name);
                    if (::std::holds_alternative<dpp::snowflake>(exclude_param)) {
                        excluded_roles.push_back(::std::get<dpp::snowflake>(exclude_param));
                    }
                } catch (...) {}
            }
            
            // Build poll embed
            ::std::string description = "**" + question + "**\n\n";
            
            // Add options with emoji and progress bars (initially 0%)
            ::std::vector<::std::string> number_emojis = {"1️⃣", "2️⃣", "3️⃣", "4️⃣", "5️⃣", "6️⃣", "7️⃣", "8️⃣", "9️⃣", "🔟"};
            for (size_t i = 0; i < options.size() && i < 10; i++) {
                description += number_emojis[i] + " **" + options[i] + "**\n";
                description += "`" + create_progress_bar(0, 15) + "` 0 votes\n";
            }
            
            description += "\n**total votes:** 0\n";
            
            // Add role restrictions info
            if (!required_roles.empty() || !excluded_roles.empty()) {
                description += "\n**voting restrictions:**\n";
                if (!required_roles.empty()) {
                    description += "must have: ";
                    for (size_t i = 0; i < required_roles.size(); i++) {
                        description += "<@&" + ::std::to_string(required_roles[i]) + ">";
                        if (i < required_roles.size() - 1) description += ", ";
                    }
                    description += "\n";
                }
                if (!excluded_roles.empty()) {
                    description += "cannot have: ";
                    for (size_t i = 0; i < excluded_roles.size(); i++) {
                        description += "<@&" + ::std::to_string(excluded_roles[i]) + ">";
                        if (i < excluded_roles.size() - 1) description += ", ";
                    }
                    description += "\n";
                }
            }
            
            description += "\n*Poll created by <@" + ::std::to_string(event.command.get_issuing_user().id) + ">*";
            
            auto embed = bronx::create_embed(description);
            embed.set_color(0x5865F2);
            
            // Create buttons for voting
            ::std::vector<dpp::component> rows;
            dpp::component current_row;
            current_row.set_type(dpp::cot_action_row);
            
            for (size_t i = 0; i < options.size() && i < 10; i++) {
                dpp::component btn;
                btn.set_type(dpp::cot_button);
                btn.set_style(dpp::cos_primary);
                btn.set_emoji(number_emojis[i]);
                
                // Encode role restrictions in custom_id
                // Format: poll_vote_<creator_id>_<option_index>_<req_count>_<req_role1>_<req_role2>..._<exc_count>_<exc_role1>...
                ::std::string custom_id = "poll_vote_" + ::std::to_string(event.command.get_issuing_user().id) + "_" + ::std::to_string(i);
                custom_id += "_" + ::std::to_string(required_roles.size());
                for (const auto& role : required_roles) {
                    custom_id += "_" + ::std::to_string(role);
                }
                custom_id += "_" + ::std::to_string(excluded_roles.size());
                for (const auto& role : excluded_roles) {
                    custom_id += "_" + ::std::to_string(role);
                }
                btn.set_id(custom_id);
                
                current_row.add_component(btn);
                
                // Discord allows max 5 buttons per row
                if ((i + 1) % 5 == 0 || i == options.size() - 1) {
                    rows.push_back(current_row);
                    current_row = dpp::component();
                    current_row.set_type(dpp::cot_action_row);
                }
            }
            
            dpp::message msg;
            msg.add_embed(embed);
            for (const auto& row : rows) {
                msg.add_component(row);
            }
            
            // Store poll data with temporary ID (will update after reply)
            PollData poll;
            poll.question = question;
            poll.options = options;
            poll.required_roles = required_roles;
            poll.excluded_roles = excluded_roles;
            poll.creator_id = event.command.get_issuing_user().id;
            poll.channel_id = event.command.channel_id;
            
            // Reply and get message ID from interaction
            event.reply(msg, [poll, &bot](const dpp::confirmation_callback_t& callback) mutable {
                if (callback.is_error()) return;
                
                // For interaction replies, we need to get the message via webhook
                // The message ID is in the interaction token
                // Store with interaction ID for now
            });
            
            // Store poll with command ID as key (interactions store message info differently)
            active_polls[event.command.id] = poll;
        },
        {
            dpp::command_option(dpp::co_string, "question", "The poll question", true),
            dpp::command_option(dpp::co_string, "option1", "First option", true),
            dpp::command_option(dpp::co_string, "option2", "Second option", true),
            dpp::command_option(dpp::co_string, "option3", "Third option", false),
            dpp::command_option(dpp::co_string, "option4", "Fourth option", false),
            dpp::command_option(dpp::co_string, "option5", "Fifth option", false),
            dpp::command_option(dpp::co_string, "option6", "Sixth option", false),
            dpp::command_option(dpp::co_string, "option7", "Seventh option", false),
            dpp::command_option(dpp::co_string, "option8", "Eighth option", false),
            dpp::command_option(dpp::co_string, "option9", "Ninth option", false),
            dpp::command_option(dpp::co_string, "option10", "Tenth option", false),
            dpp::command_option(dpp::co_role, "require_role", "Users must have this role to vote", false),
            dpp::command_option(dpp::co_role, "require_role2", "Users must have this role to vote (additional)", false),
            dpp::command_option(dpp::co_role, "require_role3", "Users must have this role to vote (additional)", false),
            dpp::command_option(dpp::co_role, "exclude_role", "Users with this role cannot vote", false),
            dpp::command_option(dpp::co_role, "exclude_role2", "Users with this role cannot vote (additional)", false),
            dpp::command_option(dpp::co_role, "exclude_role3", "Users with this role cannot vote (additional)", false)
        });
    
    return &poll;
}

// Register poll interaction handlers
inline void register_poll_interactions(dpp::cluster& bot) {
    // Handle poll voting
    bot.on_button_click([&bot](const dpp::button_click_t& event) {
        ::std::string custom_id = event.custom_id;
        
        if (custom_id.find("poll_vote_") == 0) {
            // Parse custom_id: poll_vote_<creator_id>_<option_index>_<req_count>_<req_roles...>_<exc_count>_<exc_roles...>
            ::std::vector<::std::string> parts;
            ::std::stringstream ss(custom_id);
            ::std::string part;
            while (::std::getline(ss, part, '_')) {
                parts.push_back(part);
            }
            
            if (parts.size() < 4) return;
            
            // parts[0] = "poll"
            // parts[1] = "vote"
            // parts[2] = creator_id
            // parts[3] = option_index
            // parts[4] = req_count (if exists)
            // parts[5...] = req_roles and exc_count and exc_roles
            
            int option_index = ::std::stoi(parts[3]);
            
            // Check for role restrictions
            ::std::vector<dpp::snowflake> required_roles;
            ::std::vector<dpp::snowflake> excluded_roles;
            
            if (parts.size() > 4) {
                int req_count = ::std::stoi(parts[4]);
                size_t current_idx = 5;
                
                // Parse required roles
                for (int i = 0; i < req_count && current_idx < parts.size(); i++, current_idx++) {
                    required_roles.push_back(::std::stoull(parts[current_idx]));
                }
                
                // Parse excluded roles count
                int exc_count = 0;
                if (current_idx < parts.size()) {
                    exc_count = ::std::stoi(parts[current_idx]);
                    current_idx++;
                }
                
                // Parse excluded roles
                for (int i = 0; i < exc_count && current_idx < parts.size(); i++, current_idx++) {
                    excluded_roles.push_back(::std::stoull(parts[current_idx]));
                }
            }
            
            // If no role restrictions, just confirm the vote immediately
            if (required_roles.empty() && excluded_roles.empty()) {
                // Find the poll data using the interaction's message ID
                // For component interactions, we need to check the message.interaction.id
                uint64_t poll_id = 0;
                uint64_t interaction_msg_id = event.command.message_id;
                
                // Try to find poll by checking all active polls for matching message
                for (auto& [pid, pdata] : active_polls) {
                    // Check if this interaction is for this poll's message
                    if (interaction_msg_id != 0 && pdata.message_id == interaction_msg_id) {
                        poll_id = pid;
                        break;
                    }
                    // Fallback: check if the creator and channel match
                    if (pdata.channel_id == event.command.channel_id) {
                        poll_id = pid;
                        // Update the message_id if we didn't have it
                        pdata.message_id = interaction_msg_id;
                        break;
                    }
                }
                
                if (poll_id == 0 || active_polls.find(poll_id) == active_polls.end()) {
                    event.reply(dpp::ir_channel_message_with_source,
                        dpp::message().add_embed(bronx::error("Poll data not found")).set_flags(dpp::m_ephemeral));
                    return;
                }
                
                PollData& poll = active_polls[poll_id];
                uint64_t user_id = event.command.get_issuing_user().id;
                
                // Remove previous vote if exists
                for (auto& [opt_idx, voters] : poll.votes) {
                    voters.erase(user_id);
                }
                
                // Add new vote
                poll.votes[option_index].insert(user_id);
                
                // Calculate vote percentages
                int total_votes = 0;
                for (const auto& [opt_idx, voters] : poll.votes) {
                    total_votes += voters.size();
                }
                
                // Build updated embed
                ::std::string description = "**" + poll.question + "**\n\n";
                ::std::vector<::std::string> number_emojis = {"1️⃣", "2️⃣", "3️⃣", "4️⃣", "5️⃣", "6️⃣", "7️⃣", "8️⃣", "9️⃣", "🔟"};
                
                for (size_t i = 0; i < poll.options.size() && i < 10; i++) {
                    int vote_count = poll.votes[i].size();
                    int percentage = (total_votes > 0) ? (vote_count * 100 / total_votes) : 0;
                    
                    description += number_emojis[i] + " **" + poll.options[i] + "**\n";
                    description += "`" + create_progress_bar(percentage, 15) + "` " + ::std::to_string(vote_count) + " votes\n";
                }
                
                description += "\n**total votes:** " + ::std::to_string(total_votes) + "\n";
                
                // Add role restrictions info if present
                if (!poll.required_roles.empty() || !poll.excluded_roles.empty()) {
                    description += "\n**voting restrictions:**\n";
                    if (!poll.required_roles.empty()) {
                        description += "must have: ";
                        for (size_t i = 0; i < poll.required_roles.size(); i++) {
                            description += "<@&" + ::std::to_string(poll.required_roles[i]) + ">";
                            if (i < poll.required_roles.size() - 1) description += ", ";
                        }
                        description += "\n";
                    }
                    if (!poll.excluded_roles.empty()) {
                        description += "cannot have: ";
                        for (size_t i = 0; i < poll.excluded_roles.size(); i++) {
                            description += "<@&" + ::std::to_string(poll.excluded_roles[i]) + ">";
                            if (i < poll.excluded_roles.size() - 1) description += ", ";
                        }
                        description += "\n";
                    }
                }
                
                description += "\n*Poll created by <@" + ::std::to_string(poll.creator_id) + ">*";
                
                auto embed = bronx::create_embed(description);
                embed.set_color(0x5865F2);
                
                // Get the poll message and update it
                bot.message_get(poll.message_id, poll.channel_id, [&bot, embed](const dpp::confirmation_callback_t& get_callback) {
                    if (!get_callback.is_error()) {
                        auto original_msg = get_callback.get<dpp::message>();
                        original_msg.embeds.clear();
                        original_msg.add_embed(embed);
                        bronx::safe_message_edit(bot, original_msg);
                    }
                });
                
                // Send ephemeral confirmation
                ::std::string emoji = number_emojis[option_index];
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::success("You voted for option " + emoji)).set_flags(dpp::m_ephemeral));
                return;
            }
            
            // Get the guild member to check roles
            dpp::snowflake guild_id = event.command.guild_id;
            dpp::snowflake user_id = event.command.get_issuing_user().id;
            
            bot.guild_get_member(guild_id, user_id, [&bot, event, required_roles, excluded_roles, option_index](const dpp::confirmation_callback_t& callback) {
                if (callback.is_error()) {
                    event.reply(dpp::ir_channel_message_with_source,
                        dpp::message().add_embed(bronx::error("Failed to verify your roles")).set_flags(dpp::m_ephemeral));
                    return;
                }
                
                auto member = ::std::get<dpp::guild_member>(callback.value);
                const auto& user_roles = member.get_roles();
                
                // Check if user has all required roles
                if (!required_roles.empty()) {
                    for (const auto& req_role : required_roles) {
                        bool has_role = false;
                        for (const auto& user_role : user_roles) {
                            if (user_role == req_role) {
                                has_role = true;
                                break;
                            }
                        }
                        if (!has_role) {
                            event.reply(dpp::ir_channel_message_with_source,
                                dpp::message().add_embed(bronx::error("You don't have all the required roles to vote in this poll")).set_flags(dpp::m_ephemeral));
                            return;
                        }
                    }
                }
                
                // Check if user has any excluded roles
                if (!excluded_roles.empty()) {
                    for (const auto& exc_role : excluded_roles) {
                        for (const auto& user_role : user_roles) {
                            if (user_role == exc_role) {
                                event.reply(dpp::ir_channel_message_with_source,
                                    dpp::message().add_embed(bronx::error("You cannot vote in this poll with your current roles")).set_flags(dpp::m_ephemeral));
                                return;
                            }
                        }
                    }
                }
                
                // Vote is valid - update poll
                uint64_t poll_id = 0;
                uint64_t interaction_msg_id = event.command.message_id;
                
                // Try to find poll by checking all active polls
                for (auto& [pid, pdata] : active_polls) {
                    if (interaction_msg_id != 0 && pdata.message_id == interaction_msg_id) {
                        poll_id = pid;
                        break;
                    }
                    if (pdata.channel_id == event.command.channel_id) {
                        poll_id = pid;
                        pdata.message_id = interaction_msg_id;
                        break;
                    }
                }
                
                if (poll_id == 0 || active_polls.find(poll_id) == active_polls.end()) {
                    event.reply(dpp::ir_channel_message_with_source,
                        dpp::message().add_embed(bronx::error("Poll data not found")).set_flags(dpp::m_ephemeral));
                    return;
                }
                
                PollData& poll = active_polls[poll_id];
                uint64_t user_id = event.command.get_issuing_user().id;
                
                // Remove previous vote if exists
                for (auto& [opt_idx, voters] : poll.votes) {
                    voters.erase(user_id);
                }
                
                // Add new vote
                poll.votes[option_index].insert(user_id);
                
                // Calculate vote percentages
                int total_votes = 0;
                for (const auto& [opt_idx, voters] : poll.votes) {
                    total_votes += voters.size();
                }
                
                // Build updated embed
                ::std::string description = "**" + poll.question + "**\n\n";
                ::std::vector<::std::string> number_emojis = {"1️⃣", "2️⃣", "3️⃣", "4️⃣", "5️⃣", "6️⃣", "7️⃣", "8️⃣", "9️⃣", "🔟"};
                
                for (size_t i = 0; i < poll.options.size() && i < 10; i++) {
                    int vote_count = poll.votes[i].size();
                    int percentage = (total_votes > 0) ? (vote_count * 100 / total_votes) : 0;
                    
                    description += number_emojis[i] + " **" + poll.options[i] + "**\n";
                    description += "`" + create_progress_bar(percentage, 15) + "` " + ::std::to_string(vote_count) + " votes\n";
                }
                
                description += "\n**total votes:** " + ::std::to_string(total_votes) + "\n";
                
                // Add role restrictions info
                if (!poll.required_roles.empty() || !poll.excluded_roles.empty()) {
                    description += "\n**voting restrictions:**\n";
                    if (!poll.required_roles.empty()) {
                        description += "must have: ";
                        for (size_t i = 0; i < poll.required_roles.size(); i++) {
                            description += "<@&" + ::std::to_string(poll.required_roles[i]) + ">";
                            if (i < poll.required_roles.size() - 1) description += ", ";
                        }
                        description += "\n";
                    }
                    if (!poll.excluded_roles.empty()) {
                        description += "cannot have: ";
                        for (size_t i = 0; i < poll.excluded_roles.size(); i++) {
                            description += "<@&" + ::std::to_string(poll.excluded_roles[i]) + ">";
                            if (i < poll.excluded_roles.size() - 1) description += ", ";
                        }
                        description += "\n";
                    }
                }
                
                description += "\n*Poll created by <@" + ::std::to_string(poll.creator_id) + ">*";
                
                auto embed = bronx::create_embed(description);
                embed.set_color(0x5865F2);
                
                // Get the poll message and update it
                bot.message_get(poll.message_id, poll.channel_id, [&bot, embed](const dpp::confirmation_callback_t& get_callback) {
                    if (!get_callback.is_error()) {
                        auto original_msg = get_callback.get<dpp::message>();
                        original_msg.embeds.clear();
                        original_msg.add_embed(embed);
                        bronx::safe_message_edit(bot, original_msg);
                    }
                });
                
                // Send ephemeral confirmation
                ::std::string emoji = number_emojis[option_index];
                
                event.reply(dpp::ir_channel_message_with_source,
                    dpp::message().add_embed(bronx::success("You voted for option " + emoji)).set_flags(dpp::m_ephemeral));
            });
        }
    });
}

} // namespace utility
} // namespace commands
