/-
  BPP.Gambling.Lottery — Formal model and house-edge proof

  C++ source: commands/gambling/lottery.h

  Ticket cost: random $300–$1000. Expected = $650.
  Pool contribution: 30% of ticket cost.
  Base pool: $30,000,000.
  Winner gets the pool.
-/
namespace BPP.Gambling.Lottery

/-- Pool rate: 30% (30/100). -/
def poolRatePct : Nat := 30

/-- Expected ticket cost (average of 300 and 1000). -/
def expectedCost : Nat := 650

/-- Base pool always present. -/
def basePool : Nat := 30000000

/-- Per-ticket pool contribution: 30% × 650 = 195. -/
def perTicketContrib : Nat := poolRatePct * expectedCost / 100

theorem contrib_exact : perTicketContrib = 195 := by native_decide

/-- House take rate: 70% of each ticket is not added to pool. -/
theorem house_take : 100 - poolRatePct = 70 := by native_decide

/-- Marginal EV per ticket (ignoring base pool) = contrib - cost = 195 - 650 = -455. -/
theorem marginal_ev : (perTicketContrib : Int) - expectedCost = -455 := by native_decide

/-- Marginal house edge: 455/650 = 70%. Verify: 455 × 100 = 650 × 70. -/
theorem marginal_edge_is_70pct : 455 * 100 = 650 * 70 := by native_decide

/-- With 1000 tickets, total pool = 30M + 195K = 30,195,000.
    EV per ticket = 30195000/1000 - 650 = 30195 - 650 = 29545.
    Early participants have massive positive EV from the base pool! -/
theorem ev_at_1000 : basePool + 1000 * perTicketContrib = 30195000 := by native_decide
theorem ev_profit_1000 : 30195000 / 1000 - expectedCost = 29545 := by native_decide

/-- Break-even point: base_pool / (cost - contrib) = 30M / 455 ≈ 65,934 tickets.
    Above this many tickets, marginal EV turns negative. -/
theorem breakeven_tickets : basePool / (expectedCost - perTicketContrib) = 65934 := by
  native_decide

end BPP.Gambling.Lottery
