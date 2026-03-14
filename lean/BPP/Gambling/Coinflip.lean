/-
  BPP.Gambling.Coinflip — Formal model and fairness proofs

  C++ source: commands/gambling/coinflip.h

  Base: win prob = 50%, gross payout = 0.98 × bet on win, lose bet on loss.
  Skill bonuses: luck_bonus ∈ [0,5], payout_bonus ∈ [0,10].

  EV formula (per 1 unit bet):
    win_prob = (50 + luck)/100
    win_gross = (98 + payout_bonus × 98/100)/100 ... simplified below
    loss_prob = (50 - luck)/100

  We compute: evNumer = win × (9800 + 98×bonus) - loss × 10000
  with denominator 1,000,000 (since win and loss are percentages × 100-scale payouts).
-/
namespace BPP.Gambling.Coinflip

/-- EV numerator over denominator of 1,000,000.
    win = (50 + luck), loss = (50 - luck).
    Win gross payout in basis points = 9800 + 98×payout_bonus.
    Loss = full bet = 10000. -/
def evNumer (luck : Nat) (payout_bonus : Nat) : Int :=
  let win := (50 + luck : Int)
  let loss := (50 - luck : Int)
  win * (9800 + 98 * payout_bonus) - loss * 10000

/-! ## Base case (no skill bonuses) -/

-- 50×9800 - 50×10000 = 490000 - 500000 = -10000
theorem base_ev : evNumer 0 0 = -10000 := by native_decide

theorem base_house_edge : evNumer 0 0 < 0 := by native_decide

-- -10000/1000000 = -1.0% house edge
theorem base_edge_is_1pct : -10000 * 100 = -1 * 1000000 := by omega

/-! ## Effect of luck skill -/

-- Luck=1: 51×9800 - 49×10000 = 499800 - 490000 = 9800 (POSITIVE!)
theorem luck_1_ev : evNumer 1 0 = 9800 := by native_decide

-- Even 1 rank of luck (51% win) overcomes the 2% payout penalty
theorem luck_1_positive : 0 < evNumer 1 0 := by native_decide

-- Luck=5, no bonus: 55×9800 - 45×10000 = 539000 - 450000 = 89000
theorem max_luck_ev : evNumer 5 0 = 89000 := by native_decide

/-! ## Max skill bonuses -/

-- Luck=5, payout=10: 55×(9800+980) - 45×10000 = 55×10780 - 450000 = 142900
theorem max_skills_ev : evNumer 5 10 = 142900 := by native_decide

theorem max_skills_positive : 0 < evNumer 5 10 := by native_decide

/-! ## The 0.98 payout factor explained -/

-- The 0.98 factor: even at 50/50, player gets 98% back on wins
-- EV = 50% × 0.98 - 50% × 1.0 = -0.01 = -1%
-- In basis: 50 × 98 - 50 × 100 = 4900 - 5000 = -100 (over denom 10000)
theorem house_edge_from_098 : (50 : Int) * 98 - 50 * 100 = -100 := by omega

/-! ## Win probability bounds -/

theorem win_prob_at_least_50 (luck : Nat) : 50 ≤ 50 + luck := by omega

theorem win_prob_at_most_55 (luck : Nat) (h : luck ≤ 5) : 50 + luck ≤ 55 := by omega

/-! ## Key insight: luck=1 is the break-even point

  The design intentionally makes even 1 skill point in luck give
  players an edge, rewarding prestige investment. -/
theorem breakeven_at_luck_1 : evNumer 0 0 < 0 ∧ 0 < evNumer 1 0 := by
  constructor <;> native_decide

end BPP.Gambling.Coinflip
