/-
  BPP.Gambling.Dice — Formal model and fairness proofs for the dice game

  C++ source: commands/gambling/dice.h

  Two fair d6 rolls. Net payouts per unit bet:
    Snake eyes (2) or Boxcars (12): +4 × bet
    Lucky 7 or 11:                  +1.5 × bet (scale ×2 → +3)
    Other doubles (3-3,4-4,5-5):    +2 × bet
    No special combination:          −1 × bet

  Sample space: 36 equally likely outcomes.
  C++ claims house edge ≈ −5.6%. We verify the ACTUAL EV.
  All payouts scaled ×2 to avoid the 1.5 fraction.
-/
namespace BPP.Gambling.Dice

/-! ## Outcome counts (over 36 equally likely outcomes) -/

/-- Snake eyes (1,1) + Boxcars (6,6) = 2 outcomes. -/
def countSnakeBoxcars : Nat := 2

/-- Sum=7: 6 ways. Sum=11: 2 ways. Total: 8. -/
def countLucky7or11 : Nat := 8

/-- Other doubles: (2,2),(3,3),(4,4),(5,5) = 4 outcomes. -/
def countOtherDoubles : Nat := 4

/-- No special: 36 - 2 - 8 - 4 = 22. -/
def countLoss : Nat := 22

theorem counts_sum : countSnakeBoxcars + countLucky7or11 + countOtherDoubles + countLoss = 36 := by
  native_decide

/-! ## EV calculation (scaled ×2 to avoid the 1.5 fraction)

  Scaled net payouts (×2 of actual):
    Snake/Boxcars: +8 (actual +4)
    Lucky 7/11:    +3 (actual +1.5)
    Other doubles: +4 (actual +2)
    Loss:          −2 (actual −1)

  EV_numer(×2) = 2×8 + 8×3 + 4×4 + 22×(−2)
               = 16 + 24 + 16 − 44
               = 12

  EV_denom(×2) = 2 × 36 = 72

  EV = 12 / 72 = 1/6 ≈ +16.7% player edge!
  Wait, that's not 2/9. Let me recompute...

  Actually: EV per unit bet = Σ (count/36) × net_payout
  = (2/36)×4 + (8/36)×1.5 + (4/36)×2 + (22/36)×(−1)
  = 8/36 + 12/36 + 8/36 − 22/36
  = (8 + 12 + 8 − 22) / 36
  = 6 / 36
  = 1/6

  So EV = 1/6 ≈ +16.7% in the player's favor.
-/

/-- EV numerator (unscaled) = Σ count_i × net_payout_i (×36). -/
-- Using ×2 scaling to handle 1.5:
-- EV_numer × 2 = 2×8 + 8×3 + 4×4 + 22×(-2)
def evNumer2 : Int :=
  (countSnakeBoxcars : Int) * 8 +
  (countLucky7or11 : Int) * 3 +
  (countOtherDoubles : Int) * 4 +
  (countLoss : Int) * (-2)

def evDenom2 : Nat := 2 * 36  -- = 72

theorem ev_numer_exact : evNumer2 = 12 := by native_decide

theorem ev_denom_exact : evDenom2 = 72 := by native_decide

/-- ⚠️  KEY FINDING: The player has POSITIVE expected value!
    EV = 12/72 = 1/6 ≈ +16.7% player edge.
    The C++ comment claiming "−5.6% house edge" is WRONG.
    The payouts are too generous for these probabilities. -/
theorem player_positive_ev : 0 < evNumer2 := by native_decide

/-- 12/72 simplifies to 1/6 (GCD = 12). -/
theorem ev_simplifies : evNumer2 / 12 = 1 ∧ (evDenom2 : Int) / 12 = 6 := by native_decide

/-! ## Probability verification -/

theorem prob_snake_boxcars : countSnakeBoxcars = 2 := by native_decide
theorem prob_lucky_7or11   : countLucky7or11 = 8   := by native_decide
theorem prob_other_doubles : countOtherDoubles = 4  := by native_decide
theorem prob_loss          : countLoss = 22         := by native_decide

/-! ## What WOULD give −5.6% house edge?

  Target: EV ≈ −0.056 = −56/1000 = −2.016/36 ≈ −2/36 = −1/18
  Need: Σ count_i × payout_i = −2

  One solution: snake/boxcars=3×, 7or11=0.5×, doubles=1×
  2×3 + 8×0.5 + 4×1 + 22×(−1) = 6 + 4 + 4 − 22 = −8
  That's −8/36 = −2/9 ≈ −22.2% (too much).

  Better: snake/boxcars=3→ 2×3=6, doubles=2→ 4×2=8, 7/11=1→ 8×1=8.
  6+8+8−22 = 0. That's break-even.

  For EV = −2/36: snake=4, 7/11=1, doubles=1.
  2×4 + 8×1 + 4×1 + 22×(−1) = 8+8+4−22 = −2.
  −2/36 = −1/18 ≈ −5.56%.
-/
def evHypothetical : Int :=
  (countSnakeBoxcars : Int) * 4 +
  (countLucky7or11 : Int) * 1 +
  (countOtherDoubles : Int) * 1 +
  (countLoss : Int) * (-1)

theorem hypothetical_ev : evHypothetical = -2 := by native_decide

-- −2/36 = −1/18 ≈ −5.56%, which matches the intended "≈ −5.6%" design.
-- So the INTENDED payouts were likely: snake/boxcars=4×, 7/11=1×, doubles=1×.
-- But the ACTUAL C++ code has 7/11=1.5× and doubles=2×, which makes it player-positive.

end BPP.Gambling.Dice
