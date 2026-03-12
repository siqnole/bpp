#pragma once
// ============================================================
//  chart_renderer.h — Cairo + FreeType PNG chart generation
//  renders bar / line charts into in-memory PNG std::string
//  suitable for dpp::message::add_file()
// ============================================================

#include <cairo/cairo.h>
#include <png.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <sstream>
#include <iostream>
#include <cstring>

namespace bronx {
namespace chart {

// ── colour helpers ─────────────────────────────────────────────
struct RGBA { double r, g, b, a; };

// soft palette (matches dashboard / embed_style)
inline const RGBA COL_BG        = {0.04, 0.04, 0.04, 1.0};     // #0a0a0a
inline const RGBA COL_CARD      = {0.09, 0.09, 0.11, 1.0};     // #171719
inline const RGBA COL_BORDER    = {1.0, 1.0, 1.0, 0.06};
inline const RGBA COL_TEXT      = {1.0, 1.0, 1.0, 0.85};
inline const RGBA COL_DIM       = {1.0, 1.0, 1.0, 0.45};
inline const RGBA COL_GRID      = {1.0, 1.0, 1.0, 0.04};
inline const RGBA COL_ACCENT    = {0.706, 0.655, 0.839, 1.0};  // #b4a7d6
inline const RGBA COL_GREEN     = {0.063, 0.725, 0.506, 1.0};  // #10b981
inline const RGBA COL_RED       = {0.937, 0.408, 0.271, 1.0};  // #ef4444
inline const RGBA COL_BLUE      = {0.231, 0.510, 0.965, 1.0};  // #3b82f6
inline const RGBA COL_CYAN      = {0.024, 0.714, 0.831, 1.0};  // #06b6d4
inline const RGBA COL_YELLOW    = {0.961, 0.620, 0.043, 1.0};  // #f59e0b

inline const std::vector<RGBA> PALETTE = {
    COL_ACCENT, {0.545, 0.361, 0.965, 1.0}, COL_GREEN, COL_YELLOW,
    COL_RED, COL_CYAN, {0.925, 0.282, 0.600, 1.0}, {0.078, 0.722, 0.651, 1.0}
};

inline void set_colour(cairo_t* cr, const RGBA& c) {
    cairo_set_source_rgba(cr, c.r, c.g, c.b, c.a);
}

// ── PNG write-to-memory callback ───────────────────────────────
struct PngBuffer {
    std::string data;
};
inline cairo_status_t png_write_cb(void* closure, const unsigned char* data, unsigned int length) {
    auto* buf = static_cast<PngBuffer*>(closure);
    buf->data.append(reinterpret_cast<const char*>(data), length);
    return CAIRO_STATUS_SUCCESS;
}

// ── text helpers (cairo toy font — no pango needed) ────────────
inline void set_font(cairo_t* cr, double size, bool bold = false) {
    cairo_select_font_face(cr, "sans-serif",
        CAIRO_FONT_SLANT_NORMAL,
        bold ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, size);
}

inline double text_width(cairo_t* cr, const std::string& s) {
    cairo_text_extents_t ext;
    cairo_text_extents(cr, s.c_str(), &ext);
    return ext.x_advance;
}

// ── rounded rectangle path ─────────────────────────────────────
inline void round_rect(cairo_t* cr, double x, double y, double w, double h, double r) {
    cairo_new_sub_path(cr);
    cairo_arc(cr, x + w - r, y + r,     r, -M_PI / 2, 0);
    cairo_arc(cr, x + w - r, y + h - r, r, 0,          M_PI / 2);
    cairo_arc(cr, x + r,     y + h - r, r, M_PI / 2,   M_PI);
    cairo_arc(cr, x + r,     y + r,     r, M_PI,        3 * M_PI / 2);
    cairo_close_path(cr);
}

// ── number formatting ──────────────────────────────────────────
inline std::string fmt_num(int64_t n) {
    if (n >= 1000000) return std::to_string(n / 1000000) + "." + std::to_string((n % 1000000) / 100000) + "M";
    if (n >= 1000) return std::to_string(n / 1000) + "." + std::to_string((n % 1000) / 100) + "K";
    return std::to_string(n);
}

// ── chart data types ───────────────────────────────────────────
struct BarItem  { std::string label; int64_t value; };
struct LinePoint { std::string label; double value; };
struct LineSeries { std::string name; RGBA colour; std::vector<double> values; };

// ========================================================================
//  render_horizontal_bar_chart
//  top-N commands style: horizontal bars with labels + values
// ========================================================================
inline std::string render_horizontal_bar_chart(
        const std::string& title,
        const std::vector<BarItem>& items,
        int width = 600, int height = 0)
{
    if (items.empty()) return {};

    const int pad = 20;
    const int title_h = title.empty() ? 0 : 36;
    const int bar_h = 28;
    const int gap = 10;
    const int label_w = 120;
    if (height <= 0) height = pad * 2 + title_h + (int)items.size() * (bar_h + gap);

    auto* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    auto* cr = cairo_create(surface);

    // background
    round_rect(cr, 0, 0, width, height, 12);
    set_colour(cr, COL_CARD);
    cairo_fill(cr);

    // title
    double y = pad;
    if (!title.empty()) {
        set_font(cr, 14, true);
        set_colour(cr, COL_TEXT);
        cairo_move_to(cr, pad, y + 14);
        cairo_show_text(cr, title.c_str());
        y += title_h;
    }

    int64_t max_val = 1;
    for (auto& item : items) max_val = std::max(max_val, item.value);

    const double bar_area_w = width - pad * 2 - label_w - 60;

    for (size_t i = 0; i < items.size(); ++i) {
        double by = y + i * (bar_h + gap);
        const auto& col = PALETTE[i % PALETTE.size()];

        // label
        set_font(cr, 11, false);
        set_colour(cr, COL_DIM);
        // truncate label to fit
        std::string lbl = items[i].label;
        if (lbl.size() > 16) lbl = lbl.substr(0, 14) + "..";
        cairo_move_to(cr, pad, by + bar_h * 0.65);
        cairo_show_text(cr, lbl.c_str());

        // bar
        double bw = (static_cast<double>(items[i].value) / max_val) * bar_area_w;
        if (bw < 4) bw = 4;
        double bx = pad + label_w;
        round_rect(cr, bx, by + 2, bw, bar_h - 4, 4);
        cairo_set_source_rgba(cr, col.r, col.g, col.b, 0.75);
        cairo_fill(cr);

        // value text
        set_font(cr, 11, true);
        set_colour(cr, COL_TEXT);
        std::string val_str = fmt_num(items[i].value);
        cairo_move_to(cr, bx + bw + 8, by + bar_h * 0.65);
        cairo_show_text(cr, val_str.c_str());
    }

    PngBuffer buf;
    cairo_surface_write_to_png_stream(surface, png_write_cb, &buf);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    return buf.data;
}

// ========================================================================
//  render_line_chart
//  multi-series line chart (messages, joins/leaves, active users)
// ========================================================================
inline std::string render_line_chart(
        const std::string& title,
        const std::vector<std::string>& labels,
        const std::vector<LineSeries>& series,
        int width = 600, int height = 300)
{
    if (labels.empty() || series.empty()) return {};

    auto* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    auto* cr = cairo_create(surface);

    // background
    round_rect(cr, 0, 0, width, height, 12);
    set_colour(cr, COL_CARD);
    cairo_fill(cr);

    const int pad_l = 58, pad_r = 20, pad_t = 48, pad_b = 40;
    double chart_w = width - pad_l - pad_r;
    double chart_h = height - pad_t - pad_b;

    // title
    if (!title.empty()) {
        set_font(cr, 14, true);
        set_colour(cr, COL_TEXT);
        cairo_move_to(cr, pad_l, 28);
        cairo_show_text(cr, title.c_str());
    }

    // find max value across all series
    double max_val = 1;
    for (auto& s : series)
        for (auto v : s.values) max_val = std::max(max_val, v);
    // round up to nice number
    double nice = std::pow(10, std::floor(std::log10(max_val)));
    max_val = std::ceil(max_val / nice) * nice;
    if (max_val < 5) max_val = 5;

    // grid lines (5 horizontal)
    for (int i = 0; i <= 4; ++i) {
        double gy = pad_t + chart_h * (1.0 - i / 4.0);
        set_colour(cr, COL_GRID);
        cairo_set_line_width(cr, 1);
        cairo_move_to(cr, pad_l, gy);
        cairo_line_to(cr, width - pad_r, gy);
        cairo_stroke(cr);

        // y-axis label
        set_font(cr, 10, false);
        set_colour(cr, COL_DIM);
        std::string yl = fmt_num(static_cast<int64_t>(max_val * i / 4.0));
        cairo_move_to(cr, pad_l - text_width(cr, yl) - 6, gy + 4);
        cairo_show_text(cr, yl.c_str());
    }

    // x-axis labels (show at most 10)
    int step = std::max(1, (int)labels.size() / 10);
    set_font(cr, 9, false);
    for (size_t i = 0; i < labels.size(); i += step) {
        double lx = pad_l + (double)i / (labels.size() - 1) * chart_w;
        set_colour(cr, COL_DIM);
        cairo_move_to(cr, lx - text_width(cr, labels[i]) / 2, height - pad_b + 16);
        cairo_show_text(cr, labels[i].c_str());
    }

    // draw each series
    for (auto& s : series) {
        if (s.values.size() < 2) continue;
        cairo_set_source_rgba(cr, s.colour.r, s.colour.g, s.colour.b, 0.9);
        cairo_set_line_width(cr, 2);

        for (size_t i = 0; i < s.values.size(); ++i) {
            double px = pad_l + (double)i / (s.values.size() - 1) * chart_w;
            double py = pad_t + chart_h * (1.0 - s.values[i] / max_val);
            if (i == 0) cairo_move_to(cr, px, py);
            else        cairo_line_to(cr, px, py);
        }
        cairo_stroke_preserve(cr);

        // fill under with low alpha
        double last_x = pad_l + chart_w;
        cairo_line_to(cr, last_x, pad_t + chart_h);
        cairo_line_to(cr, pad_l, pad_t + chart_h);
        cairo_close_path(cr);
        cairo_set_source_rgba(cr, s.colour.r, s.colour.g, s.colour.b, 0.12);
        cairo_fill(cr);

        // dots
        cairo_set_source_rgba(cr, s.colour.r, s.colour.g, s.colour.b, 1.0);
        for (size_t i = 0; i < s.values.size(); ++i) {
            double px = pad_l + (double)i / (s.values.size() - 1) * chart_w;
            double py = pad_t + chart_h * (1.0 - s.values[i] / max_val);
            cairo_arc(cr, px, py, 3, 0, 2 * M_PI);
            cairo_fill(cr);
        }
    }

    // legend (bottom-left, compact)
    if (series.size() > 1) {
        double lx = pad_l;
        double ly = height - 8;
        set_font(cr, 10, false);
        for (auto& s : series) {
            cairo_set_source_rgba(cr, s.colour.r, s.colour.g, s.colour.b, 1.0);
            cairo_arc(cr, lx + 5, ly - 4, 4, 0, 2 * M_PI);
            cairo_fill(cr);
            lx += 14;
            set_colour(cr, COL_DIM);
            cairo_move_to(cr, lx, ly);
            cairo_show_text(cr, s.name.c_str());
            lx += text_width(cr, s.name) + 16;
        }
    }

    PngBuffer buf;
    cairo_surface_write_to_png_stream(surface, png_write_cb, &buf);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    return buf.data;
}

// ========================================================================
//  render_summary_card
//  wide card with 4 big stat numbers (like dashboard stat-cards)
// ========================================================================
inline std::string render_summary_card(
    const std::string& title,
    const std::vector<std::pair<std::string, std::string>>& stats,  // label, value
    int width = 600, int height = 160)
{
    auto* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, width, height);
    auto* cr = cairo_create(surface);

    // background
    round_rect(cr, 0, 0, width, height, 12);
    set_colour(cr, COL_CARD);
    cairo_fill(cr);

    // title
    set_font(cr, 14, true);
    set_colour(cr, COL_TEXT);
    cairo_move_to(cr, 20, 28);
    cairo_show_text(cr, title.c_str());

    // stat boxes
    int n = (int)stats.size();
    if (n == 0) { cairo_destroy(cr); cairo_surface_destroy(surface); return {}; }
    double box_w = (width - 40.0) / n;

    for (int i = 0; i < n; ++i) {
        double bx = 20 + i * box_w;
        double by = 48;

        // subtle separator
        if (i > 0) {
            set_colour(cr, COL_BORDER);
            cairo_set_line_width(cr, 1);
            cairo_move_to(cr, bx, by);
            cairo_line_to(cr, bx, by + 80);
            cairo_stroke(cr);
        }

        // value
        set_font(cr, 24, true);
        const auto& col = PALETTE[i % PALETTE.size()];
        set_colour(cr, col);
        cairo_move_to(cr, bx + 12, by + 36);
        cairo_show_text(cr, stats[i].second.c_str());

        // label
        set_font(cr, 10, false);
        set_colour(cr, COL_DIM);
        cairo_move_to(cr, bx + 12, by + 58);
        cairo_show_text(cr, stats[i].first.c_str());
    }

    PngBuffer buf;
    cairo_surface_write_to_png_stream(surface, png_write_cb, &buf);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    return buf.data;
}

} // namespace chart
} // namespace bronx
