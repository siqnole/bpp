#!/usr/bin/env python3
"""
Minesweeper Odds & Payout Simulator
====================================
Mirrors the exact multiplier math from commands/gambling/minesweeper.h:

    mult = product_{i=0}^{revealed-1} (total_cells - i) / (safe_cells - i)
    mult *= 0.97   # 3 % house edge

Runs millions of simulated games across every grid size, difficulty, and
cash-out strategy to produce:
  - Expected value (EV) per $1 bet
  - Effective house edge
  - Win rate, median payout, variance, std-dev
  - Multiplier ladders for every configuration
  - Strategy comparison (fixed-reveal vs. random-reveal)
  - Risk-of-ruin tables

Usage:
    python3 tools/minesweeper_sim.py [--sims N] [--seed S] [--quiet]
"""

import argparse
import math
import random
import statistics
import sys
from collections import defaultdict
from dataclasses import dataclass, field
from typing import List, Dict, Tuple

# ──────────────────────────────────────────────────────────────────────
#  Core math (mirrors C++ exactly)
# ──────────────────────────────────────────────────────────────────────

HOUSE_EDGE = 0.03  # 3 %

def calculate_cumulative_multiplier(total_cells: int, num_mines: int, revealed: int) -> float:
    """Exact port of calculate_cumulative_multiplier from minesweeper.h"""
    safe_cells = total_cells - num_mines
    if safe_cells <= 0 or revealed <= 0:
        return 1.0
    if revealed > safe_cells:
        revealed = safe_cells

    mult = 1.0
    for i in range(revealed):
        mult *= (total_cells - i) / (safe_cells - i)
    mult *= (1.0 - HOUSE_EDGE)
    return mult


def difficulty_to_mines(difficulty: str, total_cells: int) -> int:
    """Exact port of difficulty_to_mines from minesweeper.h"""
    d = difficulty.lower()
    if d in ("easy", "e"):
        return max(1, int(total_cells * 0.15))
    elif d in ("medium", "med", "m"):
        return max(1, int(total_cells * 0.25))
    elif d in ("hard", "h"):
        return max(1, int(total_cells * 0.35))
    elif d in ("impossible", "imp", "i"):
        return max(1, int(total_cells * 0.50))
    try:
        return int(d)
    except ValueError:
        return -1


def survival_probability(total_cells: int, num_mines: int, reveals: int) -> float:
    """Probability of surviving `reveals` consecutive picks without hitting a mine."""
    safe = total_cells - num_mines
    if reveals <= 0:
        return 1.0
    if reveals > safe:
        return 0.0
    prob = 1.0
    for i in range(reveals):
        prob *= (safe - i) / (total_cells - i)
    return prob


# ──────────────────────────────────────────────────────────────────────
#  Data structures
# ──────────────────────────────────────────────────────────────────────

@dataclass
class GameResult:
    reveals_before_end: int
    hit_mine: bool
    multiplier: float     # 0 if hit mine, else cumulative mult at cashout
    payout_per_dollar: float  # multiplier if won, 0 if lost


@dataclass
class Config:
    cols: int   # x
    rows: int   # y
    difficulty: str
    label: str = ""

    @property
    def total_cells(self) -> int:
        return self.cols * self.rows

    @property
    def num_mines(self) -> int:
        return difficulty_to_mines(self.difficulty, self.total_cells)

    @property
    def safe_cells(self) -> int:
        return self.total_cells - self.num_mines

    def __post_init__(self):
        if not self.label:
            self.label = f"{self.cols}x{self.rows} {self.difficulty}"


# ──────────────────────────────────────────────────────────────────────
#  Simulation engine
# ──────────────────────────────────────────────────────────────────────

def simulate_game(total_cells: int, num_mines: int, cashout_after: int, rng: random.Random) -> GameResult:
    """
    Simulate one game.
    - Mines are placed uniformly at random.
    - Player reveals cells one at a time (random safe-or-mine each pick, no
      board topology needed — just hypergeometric draws).
    - If `cashout_after` >= safe_cells, player tries to reveal all safe cells.
    """
    safe = total_cells - num_mines
    target_reveals = min(cashout_after, safe)

    # Instead of building a full grid, we simulate sequential draws from an urn
    # containing `safe` safe tokens and `num_mines` mine tokens.
    remaining_safe = safe
    remaining_total = total_cells
    reveals = 0

    for _ in range(target_reveals):
        # Is the next random cell safe?
        if rng.random() < remaining_safe / remaining_total:
            remaining_safe -= 1
            remaining_total -= 1
            reveals += 1
        else:
            # Hit a mine
            return GameResult(
                reveals_before_end=reveals,
                hit_mine=True,
                multiplier=0.0,
                payout_per_dollar=0.0,
            )

    mult = calculate_cumulative_multiplier(total_cells, num_mines, reveals)
    return GameResult(
        reveals_before_end=reveals,
        hit_mine=False,
        multiplier=mult,
        payout_per_dollar=mult,
    )


def run_simulations(cfg: Config, cashout_after: int, num_sims: int, rng: random.Random) -> List[GameResult]:
    results = []
    tc = cfg.total_cells
    nm = cfg.num_mines
    for _ in range(num_sims):
        results.append(simulate_game(tc, nm, cashout_after, rng))
    return results


# ──────────────────────────────────────────────────────────────────────
#  Analytical (exact) EV calculation
# ──────────────────────────────────────────────────────────────────────

def exact_ev_for_cashout(total_cells: int, num_mines: int, cashout_after: int) -> float:
    """
    Exact expected value per $1 bet when cashing out after `cashout_after` safe reveals.
    EV = P(survive k reveals) * multiplier(k)
    """
    safe = total_cells - num_mines
    k = min(cashout_after, safe)
    p_surv = survival_probability(total_cells, num_mines, k)
    mult = calculate_cumulative_multiplier(total_cells, num_mines, k)
    return p_surv * mult  # lose $1 with prob (1 - p_surv), win mult with prob p_surv


# ──────────────────────────────────────────────────────────────────────
#  Pretty-printing helpers
# ──────────────────────────────────────────────────────────────────────

def fmt_pct(v: float) -> str:
    return f"{v * 100:.2f}%"

def fmt_mult(v: float) -> str:
    return f"{v:.4f}x"

def fmt_money(v: float) -> str:
    return f"${v:,.2f}"

def separator(char="─", width=90):
    print(char * width)


# ──────────────────────────────────────────────────────────────────────
#  Reports
# ──────────────────────────────────────────────────────────────────────

def print_multiplier_ladder(cfg: Config):
    """Show the multiplier at each reveal step, with survival probability and exact EV."""
    tc = cfg.total_cells
    nm = cfg.num_mines
    safe = cfg.safe_cells

    print(f"\n{'='*90}")
    print(f"  MULTIPLIER LADDER — {cfg.label}  (grid {cfg.cols}x{cfg.rows}, "
          f"{nm} mines in {tc} cells, {safe} safe)")
    print(f"{'='*90}")
    print(f"  {'Reveals':>8}  {'Multiplier':>12}  {'Survival%':>11}  "
          f"{'EV per $1':>11}  {'House Edge':>11}  {'Marginal Risk':>14}")
    separator()

    prev_surv = 1.0
    for k in range(1, safe + 1):
        mult = calculate_cumulative_multiplier(tc, nm, k)
        surv = survival_probability(tc, nm, k)
        ev = surv * mult
        effective_edge = 1.0 - ev
        marginal_death = 1.0 - (surv / prev_surv) if prev_surv > 0 else 1.0

        print(f"  {k:>8}  {fmt_mult(mult):>12}  {fmt_pct(surv):>11}  "
              f"{fmt_money(ev):>11}  {fmt_pct(effective_edge):>11}  {fmt_pct(marginal_death):>14}")
        prev_surv = surv

    # Fair multiplier comparison (without house edge)
    print()
    print("  Note: 'House Edge' column shows the effective take per $1 (should be ~3% at each step).")
    print(f"  Theoretical constant house edge = {HOUSE_EDGE*100:.1f}%")


def print_simulation_report(cfg: Config, cashout_after: int, results: List[GameResult]):
    """Print summary statistics from a batch of simulations."""
    n = len(results)
    wins = [r for r in results if not r.hit_mine]
    losses = [r for r in results if r.hit_mine]

    payouts = [r.payout_per_dollar for r in results]
    net_results = [r.payout_per_dollar - 1.0 for r in results]  # profit per $1

    win_rate = len(wins) / n
    avg_payout = statistics.mean(payouts)
    median_payout = statistics.median(payouts)
    ev_per_dollar = avg_payout  # since bet = $1
    effective_edge = 1.0 - ev_per_dollar

    std_dev = statistics.stdev(net_results) if n > 1 else 0
    variance = std_dev ** 2

    # Exact theoretical comparison
    exact_ev = exact_ev_for_cashout(cfg.total_cells, cfg.num_mines, cashout_after)

    tc = cfg.total_cells
    nm = cfg.num_mines
    safe = cfg.safe_cells
    target = min(cashout_after, safe)
    mult_at_cashout = calculate_cumulative_multiplier(tc, nm, target)

    print(f"\n  Config: {cfg.label} | Cash out after {cashout_after} reveal(s)")
    print(f"  Grid: {cfg.cols}x{cfg.rows} = {tc} cells | Mines: {nm} | Safe: {safe}")
    print(f"  Target multiplier at cashout: {fmt_mult(mult_at_cashout)}")
    separator("·")
    print(f"  {'Simulated games':.<40} {n:>12,}")
    print(f"  {'Win rate':.<40} {fmt_pct(win_rate):>12}")
    print(f"  {'Avg payout / $1 bet':.<40} {fmt_money(avg_payout):>12}")
    print(f"  {'Median payout / $1 bet':.<40} {fmt_money(median_payout):>12}")
    print(f"  {'Simulated EV / $1':.<40} {fmt_money(ev_per_dollar):>12}")
    print(f"  {'Exact EV / $1':.<40} {fmt_money(exact_ev):>12}")
    print(f"  {'Simulated house edge':.<40} {fmt_pct(effective_edge):>12}")
    print(f"  {'Exact house edge':.<40} {fmt_pct(1.0 - exact_ev):>12}")
    print(f"  {'Std deviation (per $1)':.<40} {std_dev:>12.4f}")
    print(f"  {'Variance (per $1)':.<40} {variance:>12.4f}")

    if wins:
        win_payouts = [r.payout_per_dollar for r in wins]
        print(f"  {'Avg winning payout':.<40} {fmt_money(statistics.mean(win_payouts)):>12}")
        print(f"  {'Max winning payout':.<40} {fmt_money(max(win_payouts)):>12}")

    # Distribution of reveals reached before death (losses only)
    if losses:
        death_reveals = [r.reveals_before_end for r in losses]
        avg_death = statistics.mean(death_reveals)
        print(f"  {'Avg reveals before death (losses)':.<40} {avg_death:>12.2f}")


def print_strategy_comparison(cfg: Config, num_sims: int, rng: random.Random):
    """Compare fixed cash-out strategies for a given config."""
    tc = cfg.total_cells
    nm = cfg.num_mines
    safe = cfg.safe_cells

    print(f"\n{'='*90}")
    print(f"  STRATEGY COMPARISON — {cfg.label}")
    print(f"  {num_sims:,} simulations per strategy")
    print(f"{'='*90}")
    print(f"  {'Strategy':>20}  {'Win Rate':>10}  {'Avg Payout':>12}  "
          f"{'EV/$1':>10}  {'Edge':>8}  {'StdDev':>8}  {'Exact EV':>10}")
    separator()

    for cashout in range(1, safe + 1):
        results = run_simulations(cfg, cashout, num_sims, rng)
        payouts = [r.payout_per_dollar for r in results]
        nets = [p - 1.0 for p in payouts]

        wr = sum(1 for r in results if not r.hit_mine) / len(results)
        avg_p = statistics.mean(payouts)
        edge = 1.0 - avg_p
        sd = statistics.stdev(nets) if len(nets) > 1 else 0
        exact = exact_ev_for_cashout(tc, nm, cashout)

        label = f"Cash out @ {cashout}"
        if cashout == safe:
            label += " (all)"

        print(f"  {label:>20}  {fmt_pct(wr):>10}  {fmt_money(avg_p):>12}  "
              f"{fmt_money(avg_p):>10}  {fmt_pct(edge):>8}  {sd:>8.3f}  {fmt_money(exact):>10}")


def print_random_strategy(cfg: Config, num_sims: int, rng: random.Random):
    """Simulate a player who cashes out at a random reveal count each game."""
    tc = cfg.total_cells
    nm = cfg.num_mines
    safe = cfg.safe_cells

    results = []
    for _ in range(num_sims):
        # Randomly decide when to cash out (1 to safe)
        cashout = rng.randint(1, safe)
        results.append(simulate_game(tc, nm, cashout, rng))

    payouts = [r.payout_per_dollar for r in results]
    nets = [p - 1.0 for p in payouts]
    wr = sum(1 for r in results if not r.hit_mine) / len(results)
    avg_p = statistics.mean(payouts)
    edge = 1.0 - avg_p
    sd = statistics.stdev(nets) if len(nets) > 1 else 0

    print(f"\n  RANDOM STRATEGY — {cfg.label}")
    print(f"  Player cashes out at a uniformly random reveal (1..{safe})")
    print(f"  {'Simulations':.<40} {num_sims:>12,}")
    print(f"  {'Win rate':.<40} {fmt_pct(wr):>12}")
    print(f"  {'Avg payout / $1':.<40} {fmt_money(avg_p):>12}")
    print(f"  {'Effective edge':.<40} {fmt_pct(edge):>12}")
    print(f"  {'Std deviation':.<40} {sd:>12.4f}")


def print_risk_of_ruin(cfg: Config, cashout_after: int, bankroll_multiples: List[int]):
    """
    Estimate risk of ruin (going broke) at different bankroll levels using
    exact EV/variance and a normal approximation for large N.
    """
    tc = cfg.total_cells
    nm = cfg.num_mines
    safe = cfg.safe_cells
    k = min(cashout_after, safe)

    surv = survival_probability(tc, nm, k)
    mult = calculate_cumulative_multiplier(tc, nm, k)
    ev = surv * mult - 1.0  # net per $1 bet (negative = house wins)

    # Variance per bet: E[X^2] - (E[X])^2
    # X = mult with prob surv, 0 with prob (1-surv), minus the $1 cost
    ex2 = surv * (mult ** 2) + (1 - surv) * 0
    var = ex2 - (surv * mult) ** 2
    sd = math.sqrt(var) if var > 0 else 0

    print(f"\n  RISK OF RUIN — {cfg.label} | Cash out after {cashout_after}")
    print(f"  Net EV per bet: {fmt_money(ev)} | σ per bet: {sd:.4f}")
    separator("·")
    print(f"  {'Bankroll (bets)':>18}  {'P(ruin) approx':>16}  {'Expected profit after N=1000':>30}")

    for B in bankroll_multiples:
        # Gambler's ruin approximation for biased random walk
        # If ev > 0 (player edge — shouldn't happen with house edge):
        #   P(ruin) ≈ exp(-2 * ev * B / var)  (Wald)
        # If ev < 0 (house edge): ruin is eventual certainty, but for finite play:
        if var > 0 and ev != 0:
            # Using simplified Lundberg exponent approximation
            r = 2 * abs(ev) / var if var > 0 else 0
            if ev > 0:
                p_ruin = math.exp(-r * B) if r * B < 700 else 0.0
            else:
                p_ruin = 1.0 - math.exp(-r * B) if r * B < 700 else 1.0
                p_ruin = min(1.0, max(0.0, p_ruin))
        else:
            p_ruin = 0.5  # unbiased

        expected_after_1000 = B + 1000 * ev
        print(f"  {B:>18}  {fmt_pct(p_ruin):>16}  {fmt_money(expected_after_1000):>30}")


def print_ev_heatmap():
    """Print an EV heatmap across all grid sizes and difficulties."""
    difficulties = ["easy", "medium", "hard", "impossible"]
    grids = [(c, r) for r in range(2, 5) for c in range(2, 6)]

    print(f"\n{'='*90}")
    print("  EXACT EV HEATMAP (per $1 bet, cash out after 1 reveal)")
    print(f"  Shows how much $1 returns on average for the safest strategy (1 reveal)")
    print(f"{'='*90}")

    header = f"  {'Grid':>6}"
    for d in difficulties:
        header += f"  {d:>12}"
    print(header)
    separator()

    for cols, rows in grids:
        tc = cols * rows
        line = f"  {cols}x{rows:>2}"
        for d in difficulties:
            nm = difficulty_to_mines(d, tc)
            safe = tc - nm
            if safe <= 0:
                line += f"  {'N/A':>12}"
                continue
            ev = exact_ev_for_cashout(tc, nm, 1)
            edge = 1.0 - ev
            line += f"  {fmt_money(ev):>12}"
        print(line)

    print()
    print("  NOTE: EV should always be $0.97 for 1 reveal (constant 3% edge).")
    print("  Deviations indicate rounding in mine-count floors from int().")

    # Now show for max-reveal (all safe cells) — highest variance strategy
    print(f"\n{'='*90}")
    print("  EXACT EV HEATMAP (per $1 bet, cash out after ALL safe reveals)")
    print(f"  Shows return for the riskiest strategy (must clear entire board)")
    print(f"{'='*90}")
    print(header)
    separator()

    for cols, rows in grids:
        tc = cols * rows
        line = f"  {cols}x{rows:>2}"
        for d in difficulties:
            nm = difficulty_to_mines(d, tc)
            safe = tc - nm
            if safe <= 0:
                line += f"  {'N/A':>12}"
                continue
            ev = exact_ev_for_cashout(tc, nm, safe)
            line += f"  {fmt_money(ev):>12}"
        print(line)


def print_mathematical_proof():
    """Prove that the house edge is exactly 3% at every cashout level."""
    print(f"\n{'='*90}")
    print("  MATHEMATICAL PROOF: CONSTANT 3% HOUSE EDGE")
    print(f"{'='*90}")
    print("""
  The multiplier after revealing k safe cells is:

      M(k) = 0.97 × ∏_{i=0}^{k-1} (T - i) / (S - i)

  where T = total cells, S = safe cells (T - mines).

  The probability of surviving k reveals is:

      P(k) = ∏_{i=0}^{k-1} (S - i) / (T - i)

  Therefore the expected value per $1 bet when cashing out at k reveals is:

      EV(k) = P(k) × M(k)
            = [∏ (S-i)/(T-i)] × [0.97 × ∏ (T-i)/(S-i)]
            = 0.97 × [∏ (S-i)/(T-i) × ∏ (T-i)/(S-i)]
            = 0.97 × 1
            = 0.97

  This holds for ALL k from 1 to S. The products perfectly cancel, leaving
  exactly 0.97 regardless of grid size, mine count, or cashout timing.

  ∴ The house edge is exactly 3% for every possible configuration and strategy.

  This is an elegant property of the multiplicative inverse payout design:
  the multiplier is precisely the reciprocal of survival probability (scaled
  by the house edge factor), making the game provably fair at every step.
""")

    # Verify numerically
    print("  NUMERICAL VERIFICATION:")
    print(f"  {'Config':>20}  {'Cashout':>8}  {'P(surv)':>10}  {'Mult':>10}  {'EV':>10}  {'Edge':>8}")
    separator("·")

    test_cases = [
        (9, 2, 1), (9, 2, 4), (9, 2, 7),   # 3x3 easy
        (20, 5, 1), (20, 5, 8), (20, 5, 15), # 5x4 medium
        (8, 4, 1), (8, 4, 2), (8, 4, 4),     # 4x2 impossible
        (12, 4, 1), (12, 4, 5), (12, 4, 8),   # 4x3 hard
    ]
    for tc, nm, k in test_cases:
        safe = tc - nm
        if k > safe:
            continue
        p = survival_probability(tc, nm, k)
        m = calculate_cumulative_multiplier(tc, nm, k)
        ev = p * m
        edge = 1.0 - ev
        label = f"T={tc} M={nm}"
        print(f"  {label:>20}  {k:>8}  {p:>10.6f}  {fmt_mult(m):>10}  {ev:>10.6f}  {fmt_pct(edge):>8}")


def print_payout_distribution(cfg: Config, cashout_after: int, num_sims: int, rng: random.Random):
    """Show distribution of outcomes as a text histogram."""
    results = run_simulations(cfg, cashout_after, num_sims, rng)

    wins = sum(1 for r in results if not r.hit_mine)
    losses = len(results) - wins

    print(f"\n  PAYOUT DISTRIBUTION — {cfg.label} | Cash out @ {cashout_after}")
    mult = calculate_cumulative_multiplier(cfg.total_cells, cfg.num_mines, min(cashout_after, cfg.safe_cells))
    print(f"  Target multiplier: {fmt_mult(mult)}")

    bar_width = 50
    total = len(results)

    # Two buckets: loss ($0) and win (mult)
    loss_pct = losses / total
    win_pct = wins / total
    max_pct = max(loss_pct, win_pct)

    loss_bar = "█" * int(loss_pct / max_pct * bar_width) if max_pct > 0 else ""
    win_bar = "█" * int(win_pct / max_pct * bar_width) if max_pct > 0 else ""

    print(f"\n  $0.00 (loss)  {loss_bar} {fmt_pct(loss_pct)} ({losses:,})")
    print(f"  {fmt_money(mult):>12}   {win_bar} {fmt_pct(win_pct)} ({wins:,})")

    # If reveals > 1, also show the distribution of how far players got before dying
    if cashout_after > 1 and losses > 0:
        death_dist = defaultdict(int)
        for r in results:
            if r.hit_mine:
                death_dist[r.reveals_before_end] += 1

        print(f"\n  Death distribution (reveals completed before hitting mine):")
        for k in sorted(death_dist.keys()):
            count = death_dist[k]
            pct = count / losses
            bar = "▓" * int(pct / 0.5 * bar_width) if pct > 0 else "▏"
            print(f"    After {k:>2} safe reveals: {bar} {fmt_pct(pct)} ({count:,})")


def print_long_run_simulation(cfg: Config, cashout_after: int, num_sessions: int, 
                               bets_per_session: int, bet_size: int, rng: random.Random):
    """Simulate multiple sessions of a player with a fixed bankroll."""
    tc = cfg.total_cells
    nm = cfg.num_mines

    print(f"\n  LONG-RUN SESSION SIMULATION — {cfg.label} | Cash out @ {cashout_after}")
    print(f"  {num_sessions} sessions × {bets_per_session} bets × ${bet_size:,} per bet")
    separator("·")

    session_profits = []
    busts = 0

    for _ in range(num_sessions):
        bankroll = bet_size * bets_per_session  # start with enough for all bets
        initial = bankroll
        bets_placed = 0

        for _ in range(bets_per_session):
            if bankroll < bet_size:
                busts += 1
                break
            bankroll -= bet_size
            result = simulate_game(tc, nm, cashout_after, rng)
            bankroll += int(result.payout_per_dollar * bet_size)
            bets_placed += 1

        session_profits.append(bankroll - initial)

    avg_profit = statistics.mean(session_profits)
    med_profit = statistics.median(session_profits)
    std_profit = statistics.stdev(session_profits) if len(session_profits) > 1 else 0
    min_profit = min(session_profits)
    max_profit = max(session_profits)
    profitable = sum(1 for p in session_profits if p > 0)

    print(f"  {'Profitable sessions':.<40} {profitable}/{num_sessions} ({fmt_pct(profitable/num_sessions)})")
    print(f"  {'Bust rate (ran out of money)':.<40} {fmt_pct(busts/num_sessions):>12}")
    print(f"  {'Avg session profit':.<40} {fmt_money(avg_profit):>12}")
    print(f"  {'Median session profit':.<40} {fmt_money(med_profit):>12}")
    print(f"  {'Std dev of session profit':.<40} {fmt_money(std_profit):>12}")
    print(f"  {'Worst session':.<40} {fmt_money(min_profit):>12}")
    print(f"  {'Best session':.<40} {fmt_money(max_profit):>12}")


# ──────────────────────────────────────────────────────────────────────
#  Main
# ──────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description="Minesweeper Odds Simulator")
    parser.add_argument("--sims", type=int, default=100_000, help="Simulations per test (default: 100,000)")
    parser.add_argument("--seed", type=int, default=42, help="RNG seed for reproducibility")
    parser.add_argument("--quiet", action="store_true", help="Skip slow simulation sections")
    args = parser.parse_args()

    rng = random.Random(args.seed)
    N = args.sims

    print("╔══════════════════════════════════════════════════════════════════════════════════════════╗")
    print("║                    MINESWEEPER ODDS & PAYOUT ANALYSIS                                  ║")
    print("║                    Mirrors minesweeper.h math exactly                                   ║")
    print(f"║                    Simulations per test: {N:>10,}                                      ║")
    print(f"║                    RNG seed: {args.seed:<10}                                              ║")
    print("╚══════════════════════════════════════════════════════════════════════════════════════════╝")

    # ── Section 1: Mathematical proof ──────────────────────────────────
    print_mathematical_proof()

    # ── Section 2: EV heatmaps ─────────────────────────────────────────
    print_ev_heatmap()

    # ── Section 3: Multiplier ladders for key configurations ───────────
    key_configs = [
        Config(3, 3, "easy"),
        Config(3, 3, "medium"),
        Config(3, 3, "hard"),
        Config(3, 3, "impossible"),
        Config(5, 4, "easy"),
        Config(5, 4, "medium"),
        Config(5, 4, "hard"),
        Config(5, 4, "impossible"),
        Config(2, 2, "easy"),     # smallest grid
        Config(2, 2, "impossible"),
    ]

    for cfg in key_configs:
        print_multiplier_ladder(cfg)

    if args.quiet:
        print("\n[--quiet mode: skipping simulation sections]")
        return

    # ── Section 4: Strategy comparison for popular configs ─────────────
    popular_configs = [
        Config(3, 3, "easy"),
        Config(3, 3, "medium"),
        Config(3, 3, "hard"),
        Config(5, 4, "easy"),
        Config(5, 4, "hard"),
        Config(5, 4, "impossible"),
    ]

    for cfg in popular_configs:
        print_strategy_comparison(cfg, N, rng)
        print_random_strategy(cfg, N, rng)

    # ── Section 5: Detailed simulation reports ─────────────────────────
    separator("═")
    print("  DETAILED SIMULATION REPORTS")
    separator("═")

    # Test various cashout strategies on 3x3 grids
    for diff in ["easy", "medium", "hard", "impossible"]:
        cfg = Config(3, 3, diff)
        for cashout in [1, 2, cfg.safe_cells // 2, cfg.safe_cells]:
            if cashout < 1:
                cashout = 1
            results = run_simulations(cfg, cashout, N, rng)
            print_simulation_report(cfg, cashout, results)

    # ── Section 6: Payout distributions ────────────────────────────────
    separator("═")
    print("  PAYOUT DISTRIBUTIONS")
    separator("═")

    dist_configs = [
        (Config(3, 3, "easy"), 1),
        (Config(3, 3, "easy"), 4),
        (Config(3, 3, "hard"), 1),
        (Config(3, 3, "hard"), 3),
        (Config(5, 4, "medium"), 5),
        (Config(5, 4, "impossible"), 3),
    ]

    for cfg, cashout in dist_configs:
        print_payout_distribution(cfg, cashout, N, rng)

    # ── Section 7: Risk of ruin ────────────────────────────────────────
    separator("═")
    print("  RISK OF RUIN ANALYSIS")
    separator("═")

    ruin_configs = [
        (Config(3, 3, "easy"), 1),
        (Config(3, 3, "easy"), 4),
        (Config(3, 3, "hard"), 2),
        (Config(5, 4, "medium"), 5),
    ]
    bankrolls = [10, 25, 50, 100, 250, 500, 1000]

    for cfg, cashout in ruin_configs:
        print_risk_of_ruin(cfg, cashout, bankrolls)

    # ── Section 8: Long-run session simulations ────────────────────────
    separator("═")
    print("  LONG-RUN SESSION SIMULATIONS")
    separator("═")

    session_configs = [
        (Config(3, 3, "easy"), 2, 100, 1000),
        (Config(3, 3, "medium"), 2, 100, 1000),
        (Config(3, 3, "hard"), 1, 100, 1000),
        (Config(5, 4, "easy"), 5, 100, 1000),
        (Config(5, 4, "impossible"), 2, 100, 1000),
    ]

    for cfg, cashout, sessions, bets in session_configs:
        print_long_run_simulation(cfg, cashout, sessions, bets, 1000, rng)

    # ── Summary ────────────────────────────────────────────────────────
    print(f"\n{'='*90}")
    print("  SUMMARY")
    print(f"{'='*90}")
    print("""
  KEY FINDINGS:

  1. CONSTANT HOUSE EDGE: The multiplier design guarantees exactly 3% house edge
     at every cashout point, regardless of grid size, mine count, or strategy.
     This is mathematically proven (the survival probability and multiplier are
     mutual inverses scaled by 0.97).

  2. STRATEGY DOESN'T MATTER (for EV): Since EV = $0.97 per $1 bet for any
     cashout timing, no strategy beats the house. The choice of when to cash out
     only affects variance, not expected return.

  3. VARIANCE INCREASES WITH RISK: Later cashouts have higher multipliers but
     lower win rates. The standard deviation grows with the number of reveals
     attempted, making high-reveal strategies more volatile.

  4. GRID SIZE / DIFFICULTY AFFECTS GRANULARITY: Larger grids offer more cashout
     steps (finer control over risk/reward), while smaller grids are coarser.
     But the EV remains 0.97 in all cases.

  5. FOR PLAYERS: The "optimal fun" strategy depends on risk tolerance:
     - Conservative: Cash out after 1-2 reveals (high win rate, small mult)
     - Moderate: Cash out at ~50% of safe cells
     - Degen: Try to clear the entire board (huge mult, very low win rate)
""")


if __name__ == "__main__":
    main()
