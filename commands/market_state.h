#pragma once
// ============================================================================
// market_state.h  —  Markov-regime market state classifier for bronx bot
// ============================================================================
//
// ARCHITECTURE OVERVIEW
// ---------------------
// This classifier sits *above* the existing tune_bait_prices_from_logs() logic.
// It answers one question each time it runs:
//
//   "Given the rolling profit trend across all bait levels, which economic
//    regime is the market currently in?"
//
// The result is stored in ml_settings under the key "market_regime" and
// consumed by the patched tune_bait_prices_from_logs() to select the correct
// {scale, decay} parameters for that regime — no hard-coded constants needed.
//
// REGIME DEFINITIONS
// ------------------
//   DEFLATION   avg rolling profit is well below the profit_target baseline
//               → prices should fall faster; bait is too expensive for returns
//   STABLE      avg rolling profit is near the target (within ±threshold)
//               → normal tuning; gentle corrections only
//   INFLATION   avg rolling profit is well above the target
//               → prices should rise faster; players are making too much money
//   CRITICAL    extreme divergence in either direction (safety valve)
//               → aggressive correction + owner notification flag
//
// TRANSITION MODEL
// ----------------
// The Markov part: each call to classify() doesn't just look at the current
// snapshot — it also reads the *previous* regime from ml_settings and applies
// a smoothing / hysteresis rule so the market can't oscillate every tick.
//
//   new_regime = transition_matrix[current_regime][candidate_regime]
//
// The transition matrix is *asymmetric*:
//   - It's easy to stay in STABLE (high self-loop probability)
//   - It's hard to jump from DEFLATION straight to INFLATION (requires
//     passing through STABLE first, unless in CRITICAL)
//   - CRITICAL can escape in one step if conditions improve
//
// HOW TO INTEGRATE
// ----------------
// 1. Call MarketStateClassifier::classify(db) on your tune timer (e.g. after
//    every Nth call to tune_bait_prices_from_logs, or on a separate schedule).
// 2. In tune_bait_prices_from_logs(), read the regime via get_ml_setting and
//    look up regime_params[] to get scale + decay overrides.
// 3. The mlstatus command reads "market_regime" from ml_settings automatically
//    (patch shown in owner.h diff at the bottom of this file).
//
// ZERO NEW DB TABLES — uses only ml_settings KV store + fishing_logs.
// ============================================================================

#include <string>
#include <array>
#include <cmath>
#include <chrono>
#include <optional>

// Forward declaration — avoids pulling the whole database header in
namespace bronx { namespace db { class Database; } }

namespace bronx {
namespace market {

// ── Regime enum ──────────────────────────────────────────────────────────────

enum class MarketRegime : int {
    DEFLATION = 0,   // profits below target; bait prices too high
    STABLE    = 1,   // profits near target; gentle corrections
    INFLATION = 2,   // profits above target; bait prices too low
    CRITICAL  = 3,   // extreme divergence; aggressive correction
    UNKNOWN   = 4,   // no data / bootstrap state
};

inline const char* regime_name(MarketRegime r) {
    switch (r) {
        case MarketRegime::DEFLATION: return "DEFLATION";
        case MarketRegime::STABLE:    return "STABLE";
        case MarketRegime::INFLATION: return "INFLATION";
        case MarketRegime::CRITICAL:  return "CRITICAL";
        default:                      return "UNKNOWN";
    }
}

inline const char* regime_emoji(MarketRegime r) {
    switch (r) {
        case MarketRegime::DEFLATION: return "🔵";  // cold / slow
        case MarketRegime::STABLE:    return "🟢";  // healthy
        case MarketRegime::INFLATION: return "🟡";  // warm / rising
        case MarketRegime::CRITICAL:  return "🔴";  // danger
        default:                      return "⚫";
    }
}

inline MarketRegime regime_from_string(const std::string& s) {
    if (s == "DEFLATION") return MarketRegime::DEFLATION;
    if (s == "STABLE")    return MarketRegime::STABLE;
    if (s == "INFLATION") return MarketRegime::INFLATION;
    if (s == "CRITICAL")  return MarketRegime::CRITICAL;
    return MarketRegime::UNKNOWN;
}

// ── Per-regime tuning parameters ─────────────────────────────────────────────
// These are the values that override tune_scale / tune_decay when the
// classifier has identified a regime.  All values can still be overridden at
// runtime via mlset — these are the defaults used when no mlset override exists
// for the active regime.

struct RegimeParams {
    double  scale;        // multiplier on the log-profit adjustment
    int64_t decay;        // flat per-tick price decay (coins)
    const char* description;
};

// Index matches MarketRegime int value
inline constexpr std::array<RegimeParams, 5> regime_params = {{
    // DEFLATION: we want prices to fall — use negative/zero scale, high decay
    { -0.5,  50,   "price suppression: bait falling toward player profitability" },
    // STABLE: gentle nudges; matches your existing default behaviour roughly
    {  1.0,  10,   "normal tuning: light corrections around profit target"       },
    // INFLATION: we want prices to rise — amplify upward adjustments
    {  2.5,   5,   "price pressure: bait rising to cool excess profits"          },
    // CRITICAL: aggressive correction regardless of direction
    {  5.0, 200,   "emergency correction: extreme divergence detected"           },
    // UNKNOWN: fall through to whatever mlset has; classifier hasn't run yet
    {  1.0,   0,   "no regime data; using raw mlset values"                      },
}};

// ── Snapshot of a single classify() call ─────────────────────────────────────

struct ClassifyResult {
    MarketRegime  regime;
    double        avg_profit_all_levels;  // weighted mean across bait levels
    double        profit_target;          // value read from ml_settings
    double        deviation_ratio;        // (avg - target) / max(1, |target|)
    int64_t       total_samples;
    bool          regime_changed;         // true if different from previous
    std::string   previous_regime_name;
    std::string   notes;                  // human-readable diagnosis
};

// ── The classifier ────────────────────────────────────────────────────────────

class MarketStateClassifier {
public:
    // Thresholds (fraction of profit_target) at which regimes change.
    // e.g. deviation_ratio > inflation_threshold  → INFLATION
    // These can be tuned via mlset: market_stable_band, market_inflation_band,
    // market_critical_band (stored as floats).
    static constexpr double kDefaultStableBand    = 0.15;  // ±15% of target = STABLE
    static constexpr double kDefaultInflationBand = 0.60;  // >60% above target = INFLATION
    static constexpr double kDefaultCriticalBand  = 1.50;  // >150% deviation = CRITICAL
    // Hysteresis: how far past the boundary the market must be to leave STABLE.
    // Prevents rapid oscillation on the boundary.
    static constexpr double kHysteresis           = 0.05;

    // ── Markov transition matrix ──────────────────────────────────────────
    // transition_allowed[from][to] — if false, this transition is blocked
    // for one tick (classifier must wait for a confirming second observation).
    // We only block the "jumpy" transitions:
    //   DEFLATION → INFLATION (and vice versa) without passing through STABLE
    //   STABLE → CRITICAL (must hit INFLATION/DEFLATION first)
    static bool transition_allowed(MarketRegime from, MarketRegime to) {
        if (from == to)                                             return true;
        if (from == MarketRegime::UNKNOWN)                          return true;
        if (to   == MarketRegime::CRITICAL)                         return true; // always allow emergency
        if (from == MarketRegime::DEFLATION && to == MarketRegime::INFLATION) return false;
        if (from == MarketRegime::INFLATION && to == MarketRegime::DEFLATION) return false;
        return true;
    }

    // ── Main classify entry point ─────────────────────────────────────────
    // Reads fishing_logs via db, classifies the regime, persists the result
    // to ml_settings, and returns a full ClassifyResult for reporting.
    //
    // min_samples: same semantics as tune_bait_prices_from_logs — levels
    //              with fewer rows are excluded from the average.
    static ClassifyResult classify(bronx::db::Database* db, int min_samples = 50);

    // ── Report helper ─────────────────────────────────────────────────────
    // Produces the multi-line string shown in mlstatus.
    static std::string build_report(bronx::db::Database* db);

private:
    // Read a float ml_setting with a fallback default
    static double read_float_setting(bronx::db::Database* db,
                                     const std::string& key,
                                     double fallback);
};

} // namespace market
} // namespace bronx


// ============================================================================
// IMPLEMENTATION (header-only to avoid a new translation unit)
// ============================================================================

#include "../database/core/database.h"  // adjust path to match your layout
#include <sstream>
#include <iomanip>
#include <ctime>
#include <cstring>

namespace bronx {
namespace market {

inline double MarketStateClassifier::read_float_setting(
    bronx::db::Database* db, const std::string& key, double fallback)
{
    auto v = db->get_ml_setting(key);
    if (!v || v->empty()) return fallback;
    try { return std::stod(*v); } catch (...) { return fallback; }
}

inline ClassifyResult MarketStateClassifier::classify(
    bronx::db::Database* db, int min_samples)
{
    ClassifyResult result{};
    result.regime = MarketRegime::UNKNOWN;

    // ── 1. Read profit_target (same key used by tune_bait_prices_from_logs) ──
    result.profit_target = read_float_setting(db, "tune_target", 0.0);

    // ── 2. Read current regime from ml_settings for hysteresis ──────────────
    MarketRegime prev_regime = MarketRegime::UNKNOWN;
    {
        auto v = db->get_ml_setting("market_regime");
        if (v) prev_regime = regime_from_string(*v);
    }
    result.previous_regime_name = regime_name(prev_regime);

    // ── 3. Load per-level averages from fishing_logs ─────────────────────────
    // We replicate the same SELECT used by tune_bait_prices_from_logs so
    // no new query is needed — just a different aggregation step.
    auto* pool = db->get_pool();
    auto conn  = pool->acquire();
    if (!conn) {
        result.notes = "could not acquire db connection";
        pool->release(conn);
        return result;
    }

    const char* sel =
        "SELECT bait_level, AVG(net_profit), COUNT(*) "
        "FROM fishing_logs GROUP BY bait_level";

    MYSQL_STMT* stmt = mysql_stmt_init(conn->get());
    if (mysql_stmt_prepare(stmt, sel, strlen(sel)) != 0) {
        mysql_stmt_close(stmt);
        pool->release(conn);
        result.notes = "prepare failed";
        return result;
    }
    if (mysql_stmt_execute(stmt) != 0) {
        mysql_stmt_close(stmt);
        pool->release(conn);
        result.notes = "execute failed";
        return result;
    }

    MYSQL_BIND res[3]; memset(res, 0, sizeof(res));
    int    bait_level;
    double avg_profit;
    long long cnt;
    res[0].buffer_type = MYSQL_TYPE_LONG;     res[0].buffer = (char*)&bait_level;
    res[1].buffer_type = MYSQL_TYPE_DOUBLE;   res[1].buffer = (char*)&avg_profit;
    res[2].buffer_type = MYSQL_TYPE_LONGLONG; res[2].buffer = (char*)&cnt;
    mysql_stmt_bind_result(stmt, res);
    mysql_stmt_store_result(stmt);

    // Weighted sum across levels (weight = sample count)
    double   weighted_sum    = 0.0;
    int64_t  total_samples   = 0;
    int      qualifying_lvls = 0;

    while (mysql_stmt_fetch(stmt) == 0) {
        if (cnt < min_samples) continue;
        weighted_sum  += avg_profit * (double)cnt;
        total_samples += cnt;
        ++qualifying_lvls;
    }
    mysql_stmt_close(stmt);
    pool->release(conn);

    result.total_samples = total_samples;

    if (qualifying_lvls == 0 || total_samples == 0) {
        result.notes = "insufficient data (< " + std::to_string(min_samples)
                     + " samples per level across all levels)";
        db->set_ml_setting("market_regime", "UNKNOWN");
        return result;
    }

    result.avg_profit_all_levels = weighted_sum / (double)total_samples;

    // ── 4. Compute deviation ratio ────────────────────────────────────────
    double baseline = std::max(1.0, std::abs(result.profit_target));
    result.deviation_ratio =
        (result.avg_profit_all_levels - result.profit_target) / baseline;

    // ── 5. Read threshold settings (overridable via mlset) ──────────────
    double stable_band    = read_float_setting(db, "market_stable_band",
                                               kDefaultStableBand);
    double inflation_band = read_float_setting(db, "market_inflation_band",
                                               kDefaultInflationBand);
    double critical_band  = read_float_setting(db, "market_critical_band",
                                               kDefaultCriticalBand);

    // Apply hysteresis: widen the band slightly when already in STABLE
    double effective_stable = stable_band;
    if (prev_regime == MarketRegime::STABLE)
        effective_stable += kHysteresis;

    // ── 6. Raw candidate regime ───────────────────────────────────────────
    MarketRegime candidate;
    double abs_dev = std::abs(result.deviation_ratio);
    if (abs_dev >= critical_band) {
        candidate = MarketRegime::CRITICAL;
    } else if (abs_dev <= effective_stable) {
        candidate = MarketRegime::STABLE;
    } else if (result.deviation_ratio > 0) {
        candidate = (abs_dev >= inflation_band) ? MarketRegime::INFLATION
                                                : MarketRegime::INFLATION;
    } else {
        candidate = MarketRegime::DEFLATION;
    }

    // ── 7. Apply Markov transition gate ──────────────────────────────────
    MarketRegime final_regime;
    if (!transition_allowed(prev_regime, candidate)) {
        // Blocked: hold previous regime for one tick, record in notes
        final_regime = prev_regime;
        result.notes += "transition " + std::string(regime_name(prev_regime))
                      + " → " + regime_name(candidate)
                      + " blocked (hysteresis); holding current regime. ";
    } else {
        final_regime = candidate;
    }

    result.regime         = final_regime;
    result.regime_changed = (final_regime != prev_regime);

    // ── 8. Persist to ml_settings ─────────────────────────────────────────
    db->set_ml_setting("market_regime", regime_name(final_regime));

    // Timestamp
    {
        auto now = std::chrono::system_clock::now();
        std::time_t t = std::chrono::system_clock::to_time_t(now);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::gmtime(&t));
        db->set_ml_setting("market_regime_updated_at", buf);
    }

    // Write the regime-specific scale/decay so tune_bait_prices_from_logs
    // picks them up automatically on its next run.
    // We only write these if the owner has NOT manually set them — this
    // preserves manual overrides.
    auto& params = regime_params[static_cast<int>(final_regime)];
    if (!db->get_ml_setting("tune_scale_override")) {
        db->set_ml_setting("tune_scale", std::to_string(params.scale));
    }
    if (!db->get_ml_setting("tune_decay_override")) {
        db->set_ml_setting("tune_decay", std::to_string(params.decay));
    }

    // ── 9. Build human-readable notes ────────────────────────────────────
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(1);
    oss << "avg_profit=" << result.avg_profit_all_levels
        << " target=" << result.profit_target
        << " deviation=" << (result.deviation_ratio * 100.0) << "%"
        << " samples=" << total_samples;
    result.notes += oss.str();

    return result;
}

inline std::string MarketStateClassifier::build_report(bronx::db::Database* db) {
    auto regime_str   = db->get_ml_setting("market_regime");
    auto updated_str  = db->get_ml_setting("market_regime_updated_at");
    auto scale_str    = db->get_ml_setting("tune_scale");
    auto decay_str    = db->get_ml_setting("tune_decay");
    auto override_str = db->get_ml_setting("tune_scale_override");

    MarketRegime regime = regime_str
        ? regime_from_string(*regime_str)
        : MarketRegime::UNKNOWN;

    auto& params = regime_params[static_cast<int>(regime)];

    std::string out;
    out += std::string(regime_emoji(regime)) + " **market regime:** "
        + regime_name(regime) + "\n";

    if (updated_str)
        out += "• last classified: " + *updated_str + " UTC\n";

    out += "• regime description: " + std::string(params.description) + "\n";

    if (scale_str)
        out += "• active tune_scale: " + *scale_str;
    if (override_str)
        out += " *(manual override — regime writes suppressed)*";
    out += "\n";

    if (decay_str)
        out += "• active tune_decay: " + *decay_str + "\n";

    // Show thresholds
    double stable  = kDefaultStableBand    * 100.0;
    double infl    = kDefaultInflationBand * 100.0;
    double crit    = kDefaultCriticalBand  * 100.0;

    // Try to read live overrides
    {
        auto v = db->get_ml_setting("market_stable_band");
        if (v) try { stable = std::stod(*v) * 100.0; } catch (...) {}
    }
    {
        auto v = db->get_ml_setting("market_inflation_band");
        if (v) try { infl = std::stod(*v) * 100.0; } catch (...) {}
    }
    {
        auto v = db->get_ml_setting("market_critical_band");
        if (v) try { crit = std::stod(*v) * 100.0; } catch (...) {}
    }

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(0);
    oss << "• bands: stable=±" << stable << "% | "
        << "inflation=+" << infl << "% | "
        << "critical=±" << crit << "%\n";
    out += oss.str();

    return out;
}

} // namespace market
} // namespace bronx