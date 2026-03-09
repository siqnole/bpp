#pragma once
#include "../../command.h"
#include "../../embed_style.h"
#include <dpp/dpp.h>

namespace commands {
namespace utility {

// Helper function to build server info embed
inline dpp::embed build_serverinfo_embed(const dpp::guild& guild, dpp::cluster& bot) {
    ::std::string description = "**name:** " + guild.name + "\n";
    description += "**id:** " + ::std::to_string(guild.id) + "\n";
    description += "**owner:** <@" + ::std::to_string(guild.owner_id) + ">\n";
    description += "**created:** <t:" + ::std::to_string((int64_t)guild.id.get_creation_time()) + ":R>\n";
    description += "**members:** " + ::std::to_string(guild.member_count) + "\n";
    description += "**roles:** " + ::std::to_string(guild.roles.size()) + "\n";
    description += "**channels:** " + ::std::to_string(guild.channels.size()) + "\n";
    
    // Shard information
    auto shards = bot.get_shards();
    uint32_t shard_count = shards.size();
    uint32_t shard_id = (guild.id >> 22) % shard_count;
    description += "**shard:** " + ::std::to_string(shard_id) + "/" + ::std::to_string(shard_count) + "\n";
    
    // Vanity URL
    if (!guild.vanity_url_code.empty()) {
        description += "**vanity:** discord.gg/" + guild.vanity_url_code + "\n";
    }
    
    // Boost information
    if (guild.premium_subscription_count > 0) {
        description += "**boosts:** " + ::std::to_string(guild.premium_subscription_count);
        description += " (level " + ::std::to_string(guild.premium_tier) + ")\n";
    }
    
    // Verification level
    ::std::string verification;
    switch((int)guild.verification_level) {
        case 0: verification = "none"; break;
        case 1: verification = "low"; break;
        case 2: verification = "medium"; break;
        case 3: verification = "high"; break;
        case 4: verification = "very high"; break;
        default: verification = "unknown"; break;
    }
    description += "**verification:** " + verification + "\n\n";
    
    // Add banner, splash and discovery splash links
    ::std::string banner_url = guild.get_banner_url(512, dpp::i_webp);
    ::std::string splash_url = guild.get_splash_url(512, dpp::i_webp);
    ::std::string discovery_url = guild.get_discovery_splash_url(512, dpp::i_webp);
    if (!banner_url.empty() || !splash_url.empty() || !discovery_url.empty()) {
        bool first = true;
        if (!banner_url.empty()) {
            description += "[server banner](" + banner_url + ")";
            first = false;
        }
        if (!splash_url.empty()) {
            if (!first) description += " | ";
            description += "[splash banner](" + splash_url + ")";
            first = false;
        }
        if (!discovery_url.empty()) {
            if (!first) description += " | ";
            description += "[invite banner](" + discovery_url + ")";
        }
    }

    auto embed = bronx::create_embed(description);
    
    ::std::string icon_url = guild.get_icon_url();
    if (!icon_url.empty()) {
        embed.set_thumbnail(icon_url);
    }
    
    return embed;
}

inline Command* get_serverinfo_command() {
    static Command serverinfo("serverinfo", "display information about the server", "utility", {"si", "guildinfo"}, true,
        [](dpp::cluster& bot, const dpp::message_create_t& event, const ::std::vector<::std::string>& args) {
            // Use cached guild data for accurate member count (requires Guild Members Intent)
            dpp::guild* guild_ptr = dpp::find_guild(event.msg.guild_id);
            if (!guild_ptr) {
                bronx::send_message(bot, event, bronx::error("couldn't fetch server information"));
                return;
            }
            
            auto embed = build_serverinfo_embed(*guild_ptr, bot);
            bronx::add_invoker_footer(embed, event.msg.author);
            bronx::send_message(bot, event, embed);
        },
        [](dpp::cluster& bot, const dpp::slashcommand_t& event) {
            // Use cached guild data for accurate member count (requires Guild Members Intent)
            dpp::guild* guild_ptr = dpp::find_guild(event.command.guild_id);
            if (!guild_ptr) {
                auto embed = bronx::error("couldn't fetch server information");
                event.reply(dpp::message().add_embed(embed));
                return;
            }
            
            auto embed = build_serverinfo_embed(*guild_ptr, bot);
            bronx::add_invoker_footer(embed, event.command.get_issuing_user());
            event.reply(dpp::message().add_embed(embed));
        });
    
    return &serverinfo;
}

} // namespace utility
} // namespace commands
