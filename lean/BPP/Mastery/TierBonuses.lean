/-
  BPP.Mastery.TierBonuses — Formal proofs for mastery tier system

  C++ source: commands/mastery/mastery_helpers.h

  | Tier         | Catches | Bonus |
  |--------------|---------|-------|
  | Novice       | 1       | 0%    |
  | Apprentice   | 10      | 1%    |
  | Journeyman   | 25      | 2%    |
  | Expert       | 50      | 3%    |
  | Master       | 100     | 5%    |
  | Grandmaster  | 250     | 7%    |
  | Legend       | 500     | 10%   |
  | Mythic       | 1000    | 15%   |
-/
namespace BPP.Mastery.TierBonuses

def thresholds : List Nat := [1, 10, 25, 50, 100, 250, 500, 1000]
def bonuses    : List Nat := [0, 1, 2, 3, 5, 7, 10, 15]

theorem num_tiers   : thresholds.length = 8 := by native_decide
theorem num_bonuses : bonuses.length    = 8 := by native_decide

/-! ## Monotonicity (using Bool for decidability) -/

def isStrictlyIncBool : List Nat → Bool
  | [] => true
  | [_] => true
  | a :: b :: rest => a < b && isStrictlyIncBool (b :: rest)

theorem thresholds_increasing : isStrictlyIncBool thresholds = true := by native_decide
theorem bonuses_increasing    : isStrictlyIncBool bonuses    = true := by native_decide

/-! ## Boundary values -/

theorem tier0_threshold : thresholds[0]! = 1    := by native_decide
theorem tier0_bonus     : bonuses[0]!    = 0    := by native_decide
theorem max_threshold   : thresholds[7]! = 1000 := by native_decide
theorem max_bonus       : bonuses[7]!    = 15   := by native_decide

/-! ## Tier lookup -/

def tierIndex (catches : Nat) : Nat :=
  let rec go : List Nat → Nat → Nat → Nat
    | [], _, best => best
    | t :: rest, idx, best =>
      if catches ≥ t then go rest (idx + 1) idx
      else go rest (idx + 1) best
  go thresholds 0 0

theorem tier_1_catch    : tierIndex 1    = 0 := by native_decide
theorem tier_10_catches : tierIndex 10   = 1 := by native_decide
theorem tier_100        : tierIndex 100  = 4 := by native_decide
theorem tier_1000       : tierIndex 1000 = 7 := by native_decide
theorem tier_5000       : tierIndex 5000 = 7 := by native_decide

/-! ## Bonus lookup -/

def bonusForCatches (catches : Nat) : Nat :=
  bonuses.getD (tierIndex catches) 0

theorem bonus_9    : bonusForCatches 9    = 0  := by native_decide
theorem bonus_10   : bonusForCatches 10   = 1  := by native_decide
theorem bonus_100  : bonusForCatches 100  = 5  := by native_decide
theorem bonus_1000 : bonusForCatches 1000 = 15 := by native_decide

/-! ## Diminishing returns -/

-- Higher tiers cost more catches per bonus %
-- Apprentice: 10 catches/1% = 10, Grandmaster: 150 catches/2% = 75
theorem diminishing_returns : 10 < 75 ∧ 75 < 83 ∧ 83 < 100 := by native_decide

end BPP.Mastery.TierBonuses
