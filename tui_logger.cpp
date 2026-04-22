#include "tui_logger.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <unistd.h>
#include <fcntl.h>
#include <regex>
#include <thread>
#include <csignal>

using namespace ftxui;

namespace bronx {
namespace tui {

TuiLogger& TuiLogger::get() {
    static TuiLogger instance;
    return instance;
}

void TuiLogger::init() {
    // 1. Create pipe for stderr
    if (pipe(pipe_stderr_) == -1) {
        return;
    }

    // 2. Duplicate original
    old_stderr_ = dup(STDERR_FILENO);

    // 3. Redirect stderr to the write end of our pipe
    dup2(pipe_stderr_[1], STDERR_FILENO);

    // 4. Start reader thread for stderr
    reader_threads_.emplace_back(&TuiLogger::pipe_reader_thread, this, pipe_stderr_[0], LogLevel::ERROR);
}

void TuiLogger::restore_redirection() {
    if (old_stderr_ != -1) {
        dup2(old_stderr_, STDERR_FILENO);
        close(old_stderr_);
        old_stderr_ = -1;
    }
}

TuiLogger::~TuiLogger() {
    stop();
    restore_redirection();
}

void TuiLogger::stop() {
    running_ = false;

    // Shutdown pipe by closing write end first
    if (pipe_stderr_[1] != -1) { close(pipe_stderr_[1]); pipe_stderr_[1] = -1; }

    for (auto& t : reader_threads_) {
        if (t.joinable()) t.join();
    }
}

void TuiLogger::pipe_reader_thread(int pipe_fd, LogLevel level) {
    char buffer[4096];
    std::string overflow;

    while (true) {
        ssize_t bytes_read = read(pipe_fd, buffer, sizeof(buffer) - 1);
        if (bytes_read <= 0) break;

        buffer[bytes_read] = '\0';
        std::string incoming = overflow + std::string(buffer);
        overflow.clear();

        size_t start = 0;
        size_t end;
        while ((end = incoming.find('\n', start)) != std::string::npos) {
            std::string line = incoming.substr(start, end - start);
            if (!line.empty()) {
                add_log(level, line);
            }
            start = end + 1;
        }
        if (start < incoming.length()) {
            overflow = incoming.substr(start);
        }
    }
}

std::string TuiLogger::strip_ansi(const std::string& str) {
    static const std::regex ansi_regex("\x1B\\[[0-9;?]*[a-zA-Z]");
    return std::regex_replace(str, ansi_regex, "");
}

std::string TuiLogger::get_current_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%H:%M:%S");
    return ss.str();
}

void TuiLogger::add_log(LogLevel level, const std::string& message) {
    std::lock_guard<std::mutex> lock(buffer_mtx_);
    
    LogEntry entry {
        level,
        get_current_timestamp(),
        strip_ansi(message)
    };

    std::deque<LogEntry>* target = &info_logs_;
    if (level == LogLevel::ERROR) target = &error_logs_;
    else if (level == LogLevel::DEBUG) target = &debug_logs_;

    target->push_back(std::move(entry));
    if (target->size() > MAX_LOGS) {
        target->pop_front();
    }
}

void TuiLogger::update_stats(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock(stats_mtx_);
    stats_[key] = value;
}

Element TuiLogger::render_log_pane(const std::string& title, const std::deque<LogEntry>& entries, Color pane_color) {
    Elements lines;
    for (const auto& entry : entries) {
        Color msg_color = Color::White;
        if (entry.level == LogLevel::ERROR) msg_color = Color::Red;
        else if (entry.level == LogLevel::DEBUG) msg_color = Color::Yellow;

        lines.push_back(hbox({
            text("[" + entry.timestamp + "] ") | color(Color::GrayDark),
            paragraph(entry.message) | color(msg_color)
        }));
    }
    
    return window(text(title) | bold | color(pane_color),
                  vbox(std::move(lines)) | vscroll_indicator | frame | flex);
}

Element TuiLogger::render_stats_bar() {
    std::lock_guard<std::mutex> lock(stats_mtx_);
    Elements stat_entries;
    for (const auto& [key, value] : stats_) {
        stat_entries.push_back(text(key + ": ") | bold | color(Color::Cyan));
        stat_entries.push_back(text(value + "  ") | color(Color::White));
    }
    return hbox(std::move(stat_entries)) | border;
}

void TuiLogger::run() {
    auto screen = ScreenInteractive::TerminalOutput();
    running_ = true;

    auto component = Renderer([&] {
        std::lock_guard<std::mutex> lock(buffer_mtx_);
        
        auto info_pane = render_log_pane(" MAIN LOGS ", info_logs_, Color::Green);
        auto debug_pane = render_log_pane(" DEBUG (Developer) ", debug_logs_, Color::Yellow);
        auto error_pane = render_log_pane(" ERRORS / WARNINGS ", error_logs_, Color::Red);

        Elements bottom_panes;
        if (show_debug_) {
            bottom_panes.push_back(debug_pane | flex);
        }
        bottom_panes.push_back(error_pane | flex);

        return vbox({
            text(" BRONX BOT DASHBOARD ") | bold | center | bgcolor(Color::Blue) | color(Color::White),
            info_pane | flex,
            hbox(std::move(bottom_panes)) | size(HEIGHT, EQUAL, show_debug_ ? 12 : 8),
            render_stats_bar()
        });
    });

    auto main_component = CatchEvent(component, [&](Event event) {
        if (event == Event::Character('q') || event == Event::Escape) {
            screen.Exit();
            return true;
        }
        if (event == Event::Character('d')) {
            toggle_debug();
            return true;
        }
        return false;
    });

    // Background thread to force refresh the UI periodically if logs arrive
    std::thread refresh_thread([&] {
        while (running_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            screen.PostEvent(Event::Custom);
        }
    });

    screen.Loop(main_component);
    
    running_ = false;
    if (refresh_thread.joinable()) refresh_thread.join();
}

} // namespace tui
} // namespace bronx
