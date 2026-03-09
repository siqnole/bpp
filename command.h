#pragma once
#include <dpp/dpp.h>
#include <string>
#include <vector>
#include <functional>


// Represents a single subcommand or flag for detailed help display
struct CommandUsageEntry {
    std::string syntax;       // e.g. "buy <ore>"  or  "-c <channel>"
    std::string explanation;  // e.g. "Purchase a mining claim for the specified ore type"
};

struct Command {
    std::string name;
    std::string description;
    std::string category;
    std::vector<std::string> aliases;
    bool is_slash_command;
    std::vector<dpp::command_option> options;
    std::function<void(dpp::cluster&, const dpp::message_create_t&, const std::vector<std::string>&)> text_handler;
    std::function<void(dpp::cluster&, const dpp::slashcommand_t&)> slash_handler;

    // --- extended help fields (optional — populated after construction) ---
    std::string extended_description;                // longer explanation shown in detailed help
    std::string detailed_usage;                      // full usage line override (shown instead of auto-generated)
    std::vector<CommandUsageEntry> subcommands;      // subcommand / action entries
    std::vector<CommandUsageEntry> flags;             // flag / option entries
    std::vector<std::string> examples;               // concrete usage examples
    std::string notes;                               // extra tips or caveats
    
    Command(const std::string& n, const std::string& desc, const std::string& cat,
            std::vector<std::string> al, bool is_slash,
            std::function<void(dpp::cluster&, const dpp::message_create_t&, const std::vector<std::string>&)> th,
            std::function<void(dpp::cluster&, const dpp::slashcommand_t&)> sh = nullptr,
            std::vector<dpp::command_option> opts = {})
        : name(n), description(desc), category(cat), aliases(al), is_slash_command(is_slash), 
          text_handler(th), slash_handler(sh), options(opts) {}

    // Returns true when extended help fields have been populated
    bool has_extended_help() const {
        return !extended_description.empty() || !subcommands.empty() || !flags.empty() || !examples.empty();
    }
};
