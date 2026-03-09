#pragma once

#include "antispam_config.h"
#include "antispam_api.h"
#include <dpp/dpp.h>

namespace commands {
namespace quiet_moderation {

// Public API for antispam (implemented in .cpp files)
void register_antispam(dpp::cluster& bot);
Command* get_antispam_command();

} // namespace quiet_moderation
} // namespace commands
