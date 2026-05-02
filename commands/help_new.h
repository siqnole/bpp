#pragma once
#include "../command.h"
#include "../command_handler.h"
#include "../embed_style.h"
#include <dpp/dpp.h>
#include <map>
#include <set>

namespace commands {
struct HelpEntry {
  const Command *cmd;
  int subcommand_idx; // -1 for main command, >= 0 for subcommand
};

// Build a flattened list of all commands and subcommands in a category
inline std::vector<HelpEntry>
get_category_help_entries(const std::vector<Command *> &cmds) {
  std::vector<HelpEntry> entries;
  std::set<std::string> seen;
  for (auto *cmd : cmds) {
    if (seen.find(cmd->name) != seen.end())
      continue;
    seen.insert(cmd->name);

    // Add main command
    entries.push_back({cmd, -1});

    // Add subcommands
    for (size_t i = 0; i < cmd->subcommands.size(); i++) {
      entries.push_back({cmd, (int)i});
    }
  }
  return entries;
}

// Helper function to generate usage string from command options
inline ::std::string generate_usage_string(const Command *cmd,
                                           const ::std::string &prefix) {
  // If the command has a hand-written detailed_usage, prefer it
  if (!cmd->detailed_usage.empty()) {
    return cmd->detailed_usage;
  }

  ::std::string usage = prefix + cmd->name;

  for (const auto &option : cmd->options) {
    if (option.required) {
      usage += " <" + option.name + ">";
    } else {
      usage += " [" + option.name + "]";
    }
  }

  return usage;
}

// Build a rich detail description for an individual command or subcommand.
// Used by text help, slash help, and the paginated embed.
inline ::std::string build_command_detail(const Command *cmd,
                                          const ::std::string &category,
                                          const ::std::string &prefix,
                                          int subcommand_idx = -1) {
  // If we're looking at a specific subcommand
  if (subcommand_idx >= 0 && subcommand_idx < (int)cmd->subcommands.size()) {
    const auto &sc = cmd->subcommands[subcommand_idx];
    
    // Extract base name for the header (e.g. "ban <user>" -> "ban")
    size_t space = sc.syntax.find(' ');
    std::string sc_name = (space == std::string::npos) ? sc.syntax : sc.syntax.substr(0, space);

    ::std::string d = "**" + cmd->name + " " + sc_name + "**\n";
    d += sc.explanation + "\n\n";
    
    d += "**category:** " + category + "\n";
    d += "**usage:** `" + prefix + cmd->name + " " + sc.syntax + "`\n";
    
    // Show parent command context
    d += "**parent command:** `" + prefix + cmd->name + "`\n";
    
    // Inherit flags/examples/notes from parent if they exist, or show them if specific to sub?
    // Usually these are on the parent in this architecture.
    if (!cmd->examples.empty()) {
        d += "\n**examples:**\n";
        for (const auto &ex : cmd->examples) {
            // Only show examples that contain this subcommand
            if (ex.find(sc_name) != std::string::npos) {
                d += "> `" + ex + "`\n";
            }
        }
    }
    
    if (!cmd->notes.empty()) {
        d += "\n**note:** " + cmd->notes + "\n";
    }

    return d;
  }

  // Original command detail logic
  ::std::string d = "**" + cmd->name + "**\n";

  // Use extended description when available, otherwise fall back to short one
  if (!cmd->extended_description.empty()) {
    d += cmd->extended_description + "\n\n";
  } else {
    d += cmd->description + "\n\n";
  }

  d += "**category:** " + category + "\n";
  d += "**usage:** `" + generate_usage_string(cmd, prefix) + "`\n";

  if (!cmd->aliases.empty()) {
    d += "**aliases:** ";
    for (size_t i = 0; i < cmd->aliases.size(); i++) {
      d += "`" + cmd->aliases[i] + "`";
      if (i < cmd->aliases.size() - 1)
        d += ", ";
    }
    d += "\n";
  }

  d += "**type:** " +
       ::std::string(cmd->is_slash_command ? "text + slash" : "text only") +
       "\n";

  // Subcommands / actions
  if (!cmd->subcommands.empty()) {
    d += "\n**subcommands:**\n";
    for (const auto &sc : cmd->subcommands) {
      d += "> `" + sc.syntax + "` — " + sc.explanation + "\n";
    }
  }

  // Flags / options
  if (!cmd->flags.empty()) {
    d += "\n**flags:**\n";
    for (const auto &fl : cmd->flags) {
      d += "> `" + fl.syntax + "` — " + fl.explanation + "\n";
    }
  }

  // Examples
  if (!cmd->examples.empty()) {
    d += "\n**examples:**\n";
    for (const auto &ex : cmd->examples) {
      d += "> `" + ex + "`\n";
    }
  }

  // Notes
  if (!cmd->notes.empty()) {
    d += "\n**note:** " + cmd->notes + "\n";
  }

  return d;
}

inline Command *create_help_command(CommandHandler *handler) {
  static Command *help_cmd = new Command(
      "help", "display all available commands", "utility", {"h", "cmds"}, true,
      // Text command handler
      [handler](dpp::cluster &bot, const dpp::message_create_t &event,
                const ::std::vector<::std::string> &args) {
        auto categories = handler->get_commands_by_category();

        // If specific command or module requested
        if (!args.empty()) {
          ::std::string input = args[0];
          ::std::transform(input.begin(), input.end(), input.begin(),
                           ::tolower);

          // First, check if it's a module/category name
          for (const auto &[category, cmds] : categories) {
            ::std::string category_lower = category;
            ::std::transform(category_lower.begin(), category_lower.end(),
                             category_lower.begin(), ::tolower);

            if (category_lower == input) {
              // Build list of commands in this module
              ::std::string description = "**" + category + " module**\n\n";

              ::std::set<::std::string> seen_commands;
              ::std::vector<Command *> unique_commands;
              for (const auto &cmd : cmds) {
                if (seen_commands.find(cmd->name) == seen_commands.end()) {
                  seen_commands.insert(cmd->name);
                  unique_commands.push_back(cmd);
                }
              }

              auto entries = get_category_help_entries(cmds);
              description += "**commands (" +
                             ::std::to_string(entries.size()) + "):**\n";
              for (const auto &entry : entries) {
                if (entry.subcommand_idx == -1) {
                  description += "`" + entry.cmd->name + "` - " + entry.cmd->description + "\n";
                } else {
                  const auto& sc = entry.cmd->subcommands[entry.subcommand_idx];
                  size_t space = sc.syntax.find(' ');
                  std::string sc_name = (space == std::string::npos) ? sc.syntax : sc.syntax.substr(0, space);
                  description += "`" + entry.cmd->name + " " + sc_name + "` - " + sc.explanation + "\n";
                }
              }

              auto embed = bronx::create_embed(description);
              bronx::add_invoker_footer(embed, event.msg.author);

              // Add module navigation buttons
              ::std::vector<::std::string> category_list;
              for (const auto &[cat, _] : categories) {
                category_list.push_back(cat);
              }

              // Find current category index
              int current_idx = 0;
              for (size_t i = 0; i < category_list.size(); i++) {
                if (category_list[i] == category) {
                  current_idx = i;
                  break;
                }
              }

              dpp::component nav_row;
              nav_row.add_component(
                  dpp::component()
                      .set_type(dpp::cot_button)
                      .set_emoji("◀️")
                      .set_label("prev module")
                      .set_style(dpp::cos_primary)
                      .set_id("help_module_prev_" +
                              ::std::to_string(event.msg.author.id) + "_" +
                              ::std::to_string(current_idx)));
              nav_row.add_component(
                  dpp::component()
                      .set_type(dpp::cot_button)
                      .set_emoji("▶️")
                      .set_label("next module")
                      .set_style(dpp::cos_primary)
                      .set_id("help_module_next_" +
                              ::std::to_string(event.msg.author.id) + "_" +
                              ::std::to_string(current_idx)));

              dpp::message msg(event.msg.channel_id, embed);
              msg.add_component(nav_row);
              bot.message_create(
                  msg, [ch = event.msg.channel_id, gid = event.msg.guild_id](
                           const dpp::confirmation_callback_t &cb) {
                    if (cb.is_error()) {
                      std::cerr
                          << "[help] failed to send help detail in channel "
                          << ch << " (guild " << gid
                          << "): " << cb.get_error().code << " - "
                          << cb.get_error().message << "\n";
                    }
                  });
              return;
            }
          }

          // Not a module, search for command by name or alias
          for (const auto &[category, cmds] : categories) {
            for (const auto &cmd : cmds) {
              // Check if matches command name or any alias
              bool matches = (cmd->name == input);
              if (!matches) {
                for (const auto &alias : cmd->aliases) {
                  if (alias == input) {
                    matches = true;
                    break;
                  }
                }
              }

              if (matches) {
                // If they provided a subcommand as second argument
                if (args.size() > 1) {
                  std::string sub_input = args[1];
                  std::transform(sub_input.begin(), sub_input.end(), sub_input.begin(), ::tolower);
                  
                  for (size_t i = 0; i < cmd->subcommands.size(); i++) {
                    const auto& sc = cmd->subcommands[i];
                    size_t space = sc.syntax.find(' ');
                    std::string sc_name = (space == std::string::npos) ? sc.syntax : sc.syntax.substr(0, space);
                    std::string sc_name_lower = sc_name;
                    std::transform(sc_name_lower.begin(), sc_name_lower.end(), sc_name_lower.begin(), ::tolower);
                    
                    if (sc_name_lower == sub_input) {
                      ::std::string description = build_command_detail(cmd, category, handler->get_prefix(), (int)i);
                      auto embed = bronx::create_embed(description);
                      bronx::add_invoker_footer(embed, event.msg.author);
                      bot.message_create(dpp::message(event.msg.channel_id, embed));
                      return;
                    }
                  }
                }

                ::std::string description =
                    build_command_detail(cmd, category, handler->get_prefix());

                auto embed = bronx::create_embed(description);
                bronx::add_invoker_footer(embed, event.msg.author);
                bot.message_create(dpp::message(event.msg.channel_id, embed));
                return;
              }
            }
          }

          // Neither module nor command found
          bot.message_create(dpp::message(
              event.msg.channel_id,
              bronx::error("module or command `" + input + "` not found")));
          return;
        }

        // Create main help embed
        ::std::string description = "**bronx**\n\n";
        description +=
            "use the dropdown below to explore commands by category\n";
        description += "or use `" + handler->get_prefix() +
                       "help <command>` for details\n\n";

        // Count commands
        int total_commands = 0;
        for (const auto &[category, cmds] : categories) {
          ::std::set<::std::string> unique_cmds;
          for (const auto &cmd : cmds) {
            unique_cmds.insert(cmd->name);
          }
          total_commands += unique_cmds.size();
        }

        description +=
            "**categories:** " + ::std::to_string(categories.size()) + "\n";
        description += "**commands:** " + ::std::to_string(total_commands);

        auto embed =
            bronx::create_embed(description)
                .set_thumbnail("https://cdn.discordapp.com/emojis/"
                               "1234567890.png"); // Bot avatar would go here
        bronx::add_invoker_footer(embed, event.msg.author);

        // Create select menu for categories
        dpp::component select_menu;
        select_menu.set_type(dpp::cot_selectmenu)
            .set_placeholder("select a category")
            .set_id("help_category_" + ::std::to_string(event.msg.author.id));

        for (const auto &[category, cmds] : categories) {
          ::std::set<::std::string> unique_cmds;
          for (const auto &cmd : cmds) {
            unique_cmds.insert(cmd->name);
          }

          select_menu.add_select_option(dpp::select_option(
              category, category,
              ::std::to_string(unique_cmds.size()) + " commands"));
        }

        dpp::message msg(event.msg.channel_id, embed);
        msg.add_component(dpp::component().add_component(select_menu));

        bot.message_create(
            msg, [ch = event.msg.channel_id, gid = event.msg.guild_id](
                     const dpp::confirmation_callback_t &cb) {
              if (cb.is_error()) {
                std::cerr << "[help] failed to send help menu in channel " << ch
                          << " (guild " << gid << "): " << cb.get_error().code
                          << " - " << cb.get_error().message << "\n";
              }
            });
      },
      // Slash command handler
      [handler](dpp::cluster &bot, const dpp::slashcommand_t &event) {
        auto categories = handler->get_commands_by_category();

        // Check for command parameter
        auto cmd_param = event.get_parameter("command");
        if (::std::holds_alternative<::std::string>(cmd_param)) {
          ::std::string input = ::std::get<::std::string>(cmd_param);
          ::std::transform(input.begin(), input.end(), input.begin(),
                           ::tolower);

          // First, check if it's a module/category name
          for (const auto &[category, cmds] : categories) {
            ::std::string category_lower = category;
            ::std::transform(category_lower.begin(), category_lower.end(),
                             category_lower.begin(), ::tolower);

            if (category_lower == input) {
              // Build list of commands in this module
              ::std::string description = "**" + category + " module**\n\n";

              ::std::set<::std::string> seen_commands;
              ::std::vector<Command *> unique_commands;
              for (const auto &cmd : cmds) {
                if (seen_commands.find(cmd->name) == seen_commands.end()) {
                  seen_commands.insert(cmd->name);
                  unique_commands.push_back(cmd);
                }
              }

              description += "**commands (" +
                             ::std::to_string(unique_commands.size()) +
                             "):**\n";
              for (const auto &cmd : unique_commands) {
                description +=
                    "`" + cmd->name + "` - " + cmd->description + "\n";
              }

              auto embed = bronx::create_embed(description);
              bronx::add_invoker_footer(embed,
                                        event.command.get_issuing_user());

              // Add module navigation buttons
              ::std::vector<::std::string> category_list;
              for (const auto &[cat, _] : categories) {
                category_list.push_back(cat);
              }

              // Find current category index
              int current_idx = 0;
              for (size_t i = 0; i < category_list.size(); i++) {
                if (category_list[i] == category) {
                  current_idx = i;
                  break;
                }
              }

              dpp::component nav_row;
              nav_row.add_component(
                  dpp::component()
                      .set_type(dpp::cot_button)
                      .set_emoji("◀️")
                      .set_label("prev module")
                      .set_style(dpp::cos_primary)
                      .set_id("help_module_prev_" +
                              ::std::to_string(
                                  event.command.get_issuing_user().id) +
                              "_" + ::std::to_string(current_idx)));
              nav_row.add_component(
                  dpp::component()
                      .set_type(dpp::cot_button)
                      .set_emoji("▶️")
                      .set_label("next module")
                      .set_style(dpp::cos_primary)
                      .set_id("help_module_next_" +
                              ::std::to_string(
                                  event.command.get_issuing_user().id) +
                              "_" + ::std::to_string(current_idx)));

              dpp::message msg;
              msg.add_embed(embed);
              msg.add_component(nav_row);
              event.reply(msg);
              return;
            }
          }

          // Not a module, search for command by name
          for (const auto &[category, cmds] : categories) {
            for (const auto &cmd : cmds) {
              if (cmd->name == input) {
                ::std::string description =
                    build_command_detail(cmd, category, handler->get_prefix());

                auto embed = bronx::create_embed(description);
                bronx::add_invoker_footer(embed,
                                          event.command.get_issuing_user());
                event.reply(dpp::message().add_embed(embed));
                return;
              }
            }
          }

          // Neither module nor command found
          event.reply(dpp::message().add_embed(
              bronx::error("module or command `" + input + "` not found")));
          return;
        }

        // Create main help embed
        ::std::string description = "**bronx**\n\n";
        description +=
            "use the dropdown below to explore commands by category\n";
        description += "or use `/help <command>` for details\n\n";

        // Count commands
        int total_commands = 0;
        for (const auto &[category, cmds] : categories) {
          ::std::set<::std::string> unique_cmds;
          for (const auto &cmd : cmds) {
            unique_cmds.insert(cmd->name);
          }
          total_commands += unique_cmds.size();
        }

        description +=
            "**categories:** " + ::std::to_string(categories.size()) + "\n";
        description += "**commands:** " + ::std::to_string(total_commands);

        auto embed = bronx::create_embed(description);
        bronx::add_invoker_footer(embed, event.command.get_issuing_user());

        // Create select menu for categories
        dpp::component select_menu;
        select_menu.set_type(dpp::cot_selectmenu)
            .set_placeholder("select a category")
            .set_id("help_category_" +
                    ::std::to_string(event.command.get_issuing_user().id));

        for (const auto &[category, cmds] : categories) {
          ::std::set<::std::string> unique_cmds;
          for (const auto &cmd : cmds) {
            unique_cmds.insert(cmd->name);
          }

          select_menu.add_select_option(dpp::select_option(
              category, category,
              ::std::to_string(unique_cmds.size()) + " commands"));
        }

        dpp::message msg;
        msg.add_embed(embed);
        msg.add_component(dpp::component().add_component(select_menu));

        event.reply(msg);
      },
      {dpp::command_option(dpp::co_string, "command",
                           "get detailed info about a specific command",
                           false)});

  return help_cmd;
}

// Register the select menu handler
inline void register_help_interactions(dpp::cluster &bot,
                                       CommandHandler *handler) {
  bot.on_select_click([handler, &bot](const dpp::select_click_t &event) {
    // Check if this is a help category selection
    if (event.custom_id.find("help_category_") != 0)
      return;

    // Extract user ID from custom_id (kept for menu ID uniqueness, no longer
    // restricts access)
    ::std::string user_id_str =
        event.custom_id.substr(14); // "help_category_".length()

    ::std::string selected_category = event.values[0];
    auto categories = handler->get_commands_by_category();

    if (categories.find(selected_category) == categories.end()) {
      event.reply(dpp::ir_update_message,
                  dpp::message().add_embed(bronx::error("category not found")));
      return;
    }

    // Build entry list for this category
    auto entries = get_category_help_entries(categories[selected_category]);

    // Check if category has entries
    if (entries.empty()) {
      event.reply(dpp::ir_update_message,
                  dpp::message().add_embed(
                      bronx::error("no commands found in category")));
      return;
    }

    // Start at page 0
    int page = 0;

    // Build detailed embed for first entry
    auto entry = entries[page];
    ::std::string description =
        build_command_detail(entry.cmd, selected_category, handler->get_prefix(), entry.subcommand_idx);
    description += "\n`" + ::std::to_string(page + 1) + "/" +
                   ::std::to_string(entries.size()) + "`";

    auto embed = bronx::create_embed(description);
    bronx::add_invoker_footer(embed, event.command.usr);

    // Create navigation buttons
    dpp::component nav_row;
    nav_row.add_component(dpp::component()
                              .set_type(dpp::cot_button)
                              .set_emoji("◀️")
                              .set_style(dpp::cos_primary)
                              .set_id("help_prev_" + user_id_str + "_" +
                                      selected_category + "_0"));
    nav_row.add_component(dpp::component()
                              .set_type(dpp::cot_button)
                              .set_emoji("▶️")
                              .set_style(dpp::cos_primary)
                              .set_id("help_next_" + user_id_str + "_" +
                                      selected_category + "_0"));

    // Keep the select menu
    dpp::component select_menu;
    select_menu.set_type(dpp::cot_selectmenu)
        .set_placeholder("select a category")
        .set_id("help_category_" + user_id_str);

    for (const auto &[category, cmds] : categories) {
      ::std::set<::std::string> unique_cmds;
      for (const auto &cmd_item : cmds) {
        unique_cmds.insert(cmd_item->name);
      }

      select_menu.add_select_option(dpp::select_option(
          category, category,
          ::std::to_string(unique_cmds.size()) + " commands"));
    }

    dpp::message msg;
    msg.add_embed(embed);
    msg.add_component(nav_row);
    msg.add_component(dpp::component().add_component(select_menu));

    event.reply(dpp::ir_update_message, msg);
  });

  // Handle navigation buttons
  bot.on_button_click([handler, &bot](const dpp::button_click_t &event) {
    // Handle module navigation buttons
    if (event.custom_id.find("help_module_prev_") == 0 ||
        event.custom_id.find("help_module_next_") == 0) {
      bool is_next = (event.custom_id.find("help_module_next_") == 0);
      size_t prefix_len =
          is_next ? 17 : 17; // "help_module_next_" or "help_module_prev_"
      ::std::string remainder = event.custom_id.substr(prefix_len);

      size_t underscore_pos = remainder.find('_');
      ::std::string user_id_str = remainder.substr(0, underscore_pos);
      int current_idx = ::std::stoi(remainder.substr(underscore_pos + 1));

      auto categories = handler->get_commands_by_category();
      ::std::vector<::std::string> category_list;
      for (const auto &[cat, _] : categories) {
        category_list.push_back(cat);
      }

      // Calculate new index with wrap-around
      int new_idx = current_idx;
      if (is_next) {
        new_idx = (current_idx + 1) % category_list.size();
      } else {
        new_idx =
            (current_idx - 1 + category_list.size()) % category_list.size();
      }

      ::std::string category = category_list[new_idx];

      // Build list of commands in this module
      ::std::string description = "**" + category + " module**\n\n";

      ::std::set<::std::string> seen_commands;
      ::std::vector<Command *> unique_commands;
      for (const auto &cmd : categories[category]) {
        if (seen_commands.find(cmd->name) == seen_commands.end()) {
          seen_commands.insert(cmd->name);
          unique_commands.push_back(cmd);
        }
      }

      description +=
          "**commands (" + ::std::to_string(unique_commands.size()) + "):**\n";
      for (const auto &cmd : unique_commands) {
        description += "`" + cmd->name + "` - " + cmd->description + "\n";
      }

      auto embed = bronx::create_embed(description);
      bronx::add_invoker_footer(embed, event.command.usr);

      // Update navigation buttons
      dpp::component nav_row;
      nav_row.add_component(dpp::component()
                                .set_type(dpp::cot_button)
                                .set_emoji("◀️")
                                .set_label("prev module")
                                .set_style(dpp::cos_primary)
                                .set_id("help_module_prev_" + user_id_str +
                                        "_" + ::std::to_string(new_idx)));
      nav_row.add_component(dpp::component()
                                .set_type(dpp::cot_button)
                                .set_emoji("▶️")
                                .set_label("next module")
                                .set_style(dpp::cos_primary)
                                .set_id("help_module_next_" + user_id_str +
                                        "_" + ::std::to_string(new_idx)));

      dpp::message msg;
      msg.add_embed(embed);
      msg.add_component(nav_row);

      event.reply(dpp::ir_update_message, msg);
      return;
    }

    if (event.custom_id.find("help_prev_") != 0 &&
        event.custom_id.find("help_next_") != 0)
      return;

    // Parse custom_id: help_prev/next_<user_id>_<category>_<current_page>
    bool is_next = (event.custom_id.find("help_next_") == 0);
    size_t prefix_len = is_next ? 10 : 10; // "help_next_" or "help_prev_"
    ::std::string remainder = event.custom_id.substr(prefix_len);

    size_t first_underscore = remainder.find('_');
    ::std::string user_id_str = remainder.substr(0, first_underscore);
    remainder = remainder.substr(first_underscore + 1);

    size_t last_underscore = remainder.rfind('_');
    ::std::string selected_category = remainder.substr(0, last_underscore);
    int current_page = ::std::stoi(remainder.substr(last_underscore + 1));

    dpp::snowflake expected_user_id = ::std::stoull(user_id_str);
    (void)expected_user_id; // no longer restricting to invoker only

    auto categories = handler->get_commands_by_category();

    auto entries = get_category_help_entries(categories[selected_category]);

    // Check if category has entries
    if (entries.empty()) {
      event.reply(dpp::ir_update_message,
                  dpp::message().add_embed(
                      bronx::error("no commands found in category")));
      return;
    }

    // Calculate new page with wrap-around
    int new_page = current_page;
    if (is_next) {
      new_page = (current_page + 1) % entries.size();
    } else {
      new_page = (current_page - 1 + entries.size()) %
                 entries.size();
    }

    // Build detailed embed for current entry
    auto entry = entries[new_page];
    ::std::string description =
        build_command_detail(entry.cmd, selected_category, handler->get_prefix(), entry.subcommand_idx);
    description += "\n`" + ::std::to_string(new_page + 1) + "/" +
                   ::std::to_string(entries.size()) + "`";

    auto embed = bronx::create_embed(description);
    bronx::add_invoker_footer(embed, event.command.usr);

    // Create navigation buttons with updated page
    dpp::component nav_row;
    nav_row.add_component(dpp::component()
                              .set_type(dpp::cot_button)
                              .set_emoji("◀️")
                              .set_style(dpp::cos_primary)
                              .set_id("help_prev_" + user_id_str + "_" +
                                      selected_category + "_" +
                                      ::std::to_string(new_page)));
    nav_row.add_component(dpp::component()
                              .set_type(dpp::cot_button)
                              .set_emoji("▶️")
                              .set_style(dpp::cos_primary)
                              .set_id("help_next_" + user_id_str + "_" +
                                      selected_category + "_" +
                                      ::std::to_string(new_page)));

    // Keep the select menu
    dpp::component select_menu;
    select_menu.set_type(dpp::cot_selectmenu)
        .set_placeholder("select a category")
        .set_id("help_category_" + user_id_str);

    for (const auto &[category, cmds] : categories) {
      ::std::set<::std::string> unique_cmds;
      for (const auto &cmd_item : cmds) {
        unique_cmds.insert(cmd_item->name);
      }

      select_menu.add_select_option(dpp::select_option(
          category, category,
          ::std::to_string(unique_cmds.size()) + " commands"));
    }

    dpp::message msg;
    msg.add_embed(embed);
    msg.add_component(nav_row);
    msg.add_component(dpp::component().add_component(select_menu));

    event.reply(dpp::ir_update_message, msg);
  });
}

} // namespace commands
