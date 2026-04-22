#pragma once

#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <iostream>
#include <chrono>
#include <atomic>
#include <ftxui/dom/elements.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/component/screen_interactive.hpp>

namespace bronx {
namespace tui {

enum class LogLevel {
    INFO,
    DEBUG,
    ERROR,
    DPP_LOG // Special level for DPP events
};

struct LogEntry {
    LogLevel level;
    std::string timestamp;
    std::string message;
};

/**
 * @brief High-performance TUI Logger that separates logs into multiple panes.
 * Hijacks stdout/stderr to route all output into the TUI.
 */
class TuiLogger {
public:
    static TuiLogger& get();

    void init();
    void add_log(LogLevel level, const std::string& message);
    
    // Core loop
    void run();
    void stop();
    bool is_running() const { return running_; }

    // Statistics update (called from background threads)
    void update_stats(const std::string& key, const std::string& value);
    void toggle_debug() { show_debug_ = !show_debug_; }

private:
    TuiLogger() = default;
    ~TuiLogger();

    // Rendering helpers
    ftxui::Element render_log_pane(const std::string& title, const std::deque<LogEntry>& entries, ftxui::Color pane_color);
    ftxui::Element render_stats_bar();
    
    std::string strip_ansi(const std::string& str);
    std::string get_current_timestamp();
    void restore_redirection();
    
    // Background reader for pipes
    void pipe_reader_thread(int pipe_fd, LogLevel level);

    // Data buffers
    std::deque<LogEntry> info_logs_;
    std::deque<LogEntry> debug_logs_;
    std::deque<LogEntry> error_logs_;
    std::mutex buffer_mtx_;
    
    const size_t MAX_LOGS = 1000;
    bool show_debug_ = true;

    // Stats storage
    std::map<std::string, std::string> stats_;
    std::mutex stats_mtx_;

    std::atomic<bool> running_{false};
    
    // Redirect pipes and original duplicates
    int pipe_stderr_[2] = {-1, -1};
    int old_stderr_ = -1;

    std::vector<std::thread> reader_threads_;
};

} // namespace tui
} // namespace bronx
