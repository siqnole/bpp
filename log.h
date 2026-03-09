#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Colored debug-log tag macros for journalctl output
//
// Each module gets its own emoji + color so you can visually scan the log and
// instantly tell apart fishing, blackjack, roulette, inventory, etc.
//
// Usage (string literal concatenation – no comma after the macro):
//   std::cout << DBG_BJ "handle_blackjack_hit for user " << uid << "\n";
//
// Produces in the terminal / journalctl (with ANSI color):
//   [DBG] 🃏 handle_blackjack_hit for user 12345
// ─────────────────────────────────────────────────────────────────────────────

// Generic debug tag (light cyan)
#define DBG_TAG     "\033[2m[\033[36mDBG\033[2m]\033[0m "

// Per-module colored tags
#define DBG_FISH    "\033[2m[\033[36mDBG\033[2m]\033[0m \033[34m\xF0\x9F\x90\x9F\033[0m "
#define DBG_BJ      "\033[2m[\033[36mDBG\033[2m]\033[0m \033[33m\xF0\x9F\x83\x8F\033[0m "
#define DBG_ROUL    "\033[2m[\033[36mDBG\033[2m]\033[0m \033[35m\xF0\x9F\x8E\xB0\033[0m "
#define DBG_GAMB    "\033[2m[\033[36mDBG\033[2m]\033[0m \033[33m\xF0\x9F\x8E\xB2\033[0m "
#define DBG_INV     "\033[2m[\033[36mDBG\033[2m]\033[0m \033[32m\xF0\x9F\x93\xA6\033[0m "
#define DBG_DB      "\033[2m[\033[36mDBG\033[2m]\033[0m \033[36m\xF0\x9F\x97\x84\033[0m "

// Inline highlight helpers (wrap a value in color, then reset)
#define CLR_USER    "\033[1;36m"   // bold cyan for user IDs
#define CLR_VAL     "\033[32m"     // green for values
#define CLR_KEY     "\033[33m"     // yellow for keys/names
#define CLR_ERR     "\033[1;31m"   // bold red for errors
#define CLR_WARN    "\033[33m"     // yellow for warnings
#define CLR_DIM     "\033[2m"      // dim for less-important info
#define CLR_RST     "\033[0m"      // reset
