/-
  BPP.Fishing.Effects — Formal proofs for fish effect expected values

  C++ source: commands/fishing/fishing_helpers.h

  Each effect modifies fish value v. We compute expected value multipliers.
  All EVs are in ×1000 fixed-point (so 1000 = 1.0×).
-/
namespace BPP.Fishing.Effects

/-! ## Jackpot effect
  50% chance: v × 0.2 (lose 80%)
  50% chance: v × 8.0
  EV = 0.5 × 0.2v + 0.5 × 8v = 0.1v + 4.0v = 4.1v
-/

/-- Jackpot EV × 1000: (200 + 8000) / 2 = 4100. -/
def jackpotEV1000 : Nat := (200 + 8000) / 2

theorem jackpot_ev : jackpotEV1000 = 4100 := by native_decide

/-- Jackpot is positive-EV: 4.1x average. -/
theorem jackpot_positive : jackpotEV1000 > 1000 := by native_decide

/-! ## Critical effect
  50% chance: v × 2
  50% chance: v × 1
  EV = 0.5 × 2v + 0.5 × v = 1.5v
-/

def criticalEV1000 : Nat := (2000 + 1000) / 2

theorem critical_ev : criticalEV1000 = 1500 := by native_decide
theorem critical_positive : criticalEV1000 > 1000 := by native_decide

/-! ## Wacky effect
  v × rand(1, 5), uniform: EV = v × (1+2+3+4+5)/5 = v × 3.0
-/

def wackyEV1000 : Nat := (1000 + 2000 + 3000 + 4000 + 5000) / 5

theorem wacky_ev : wackyEV1000 = 3000 := by native_decide
theorem wacky_positive : wackyEV1000 > 1000 := by native_decide

/-! ## Volatile effect
  v × (0.3 + rand(0,37)/10), uniform over {0.3, 0.4, ..., 4.0}
  38 outcomes from 0.3 to 4.0 in 0.1 steps
  Sum = 0.3 + 0.4 + ... + 4.0 = 38 × (0.3 + 4.0) / 2 = 38 × 2.15 = 81.7
  EV = 81.7/38 = 2.15
-/

/-- Volatile outcomes ×10: {3, 4, ..., 40}, 38 outcomes. -/
def volatileSum10 : Nat := (3 + 40) * 38 / 2

-- Sum×10 = 817
theorem volatile_sum : volatileSum10 = 817 := by native_decide

/-- Volatile EV×1000 = 817/38 × (1000/10) = 817 × 100 / 38 ≈ 2150. -/
def volatileEV1000 : Nat := volatileSum10 * 100 / 38

theorem volatile_ev : volatileEV1000 = 2150 := by native_decide
theorem volatile_positive : volatileEV1000 > 1000 := by native_decide

/-! ## Diminishing effect
  v × max(0.5, 3.0 − luck/50)
  At luck 0: mult = 3.0
  At luck 50: mult = 2.0
  At luck 100: mult = 1.0
  At luck 125+: mult = 0.5 (floor kicks in)
-/

/-- Diminishing mult ×1000. -/
def diminishingMult1000 (luck : Nat) : Nat :=
  Nat.max 500 (3000 - luck * 20)

theorem diminishing_luck0 : diminishingMult1000 0 = 3000 := by native_decide
theorem diminishing_luck50 : diminishingMult1000 50 = 2000 := by native_decide
theorem diminishing_luck100 : diminishingMult1000 100 = 1000 := by native_decide
theorem diminishing_luck125 : diminishingMult1000 125 = 500 := by native_decide

/-- Diminishing is always at least 0.5× no matter the luck. -/
theorem diminishing_floor (luck : Nat) : diminishingMult1000 luck ≥ 500 := by
  unfold diminishingMult1000
  exact Nat.le_max_left _ _

/-! ## Cascading effect
  v × 1.15^rand(1,6), uniform
  Using ×10000 fixed point: 1.15^k ×10000
  1: 11500, 2: 13225, 3: 15208, 4: 17490, 5: 20113, 6: 23130
  Sum = 100666, EV = 100666/6 = 16777
  EV ≈ 1.6777×
-/

def cascadingPowers10k : List Nat := [11500, 13225, 15208, 17490, 20113, 23130]

def cascadingSum10k : Nat := cascadingPowers10k.foldl (· + ·) 0

theorem cascading_sum : cascadingSum10k = 100666 := by native_decide

def cascadingEV10k : Nat := cascadingSum10k / 6

theorem cascading_ev : cascadingEV10k = 16777 := by native_decide

/-- Every cascading outcome is ≥ 1.0× (all powers > 10000). -/
theorem cascading_all_positive :
    cascadingPowers10k.all (· ≥ 10000) = true := by native_decide

/-! ## Gambler effect
  wins > losses → v × 2
  wins ≤ losses → v × 0.5
  EV depends on win rate; assuming 50%: EV = 0.5×2 + 0.5×0.5 = 1.25×
-/

def gamblerEV_50pct_1000 : Nat := (2000 + 500) / 2

theorem gambler_ev_balanced : gamblerEV_50pct_1000 = 1250 := by native_decide
theorem gambler_positive_balanced : gamblerEV_50pct_1000 > 1000 := by native_decide

/-! ## Ascended effect
  v × min(10, 1.5^prestige)
  Prestige 0: 1.0, P1: 1.5, P2: 2.25, P3: 3.375, P4: 5.0625, P5: 7.59
  P6: 11.39 → capped at 10.0
-/

/-- 1.5^P × 10000 for small P values. -/
def ascendedMult10k : List Nat :=
  [10000, 15000, 22500, 33750, 50625, 75937, 100000]
  -- P0     P1     P2     P3     P4     P5     P6 (capped at 10.0)

theorem ascended_p0 : ascendedMult10k.getD 0 0 = 10000 := by native_decide
theorem ascended_p4 : ascendedMult10k.getD 4 0 = 50625 := by native_decide
theorem ascended_capped_p6 : ascendedMult10k.getD 6 0 = 100000 := by native_decide

/-- Ascended is always at least 1.0× (the identity). -/
theorem ascended_ge_1 :
    ascendedMult10k.all (· ≥ 10000) = true := by native_decide

/-! ## Summary: all effects are positive-EV (≥ 1.0×) -/

/-- All basic effect EVs ×1000 are ≥ 1000 (i.e., ≥ 1.0× multiplier). -/
theorem all_effects_positive_ev :
    [jackpotEV1000, criticalEV1000, wackyEV1000, volatileEV1000, gamblerEV_50pct_1000].all (· ≥ 1000) = true := by
  native_decide

/-- Ranking of effects by EV (descending):
    Jackpot > Wacky > Volatile > Critical > Gambler -/
theorem effect_ev_ranking :
    jackpotEV1000 > wackyEV1000 ∧
    wackyEV1000 > volatileEV1000 ∧
    volatileEV1000 > criticalEV1000 ∧
    criticalEV1000 > gamblerEV_50pct_1000 := by
  unfold jackpotEV1000 wackyEV1000 volatileEV1000 criticalEV1000 gamblerEV_50pct_1000
  native_decide

end BPP.Fishing.Effects
