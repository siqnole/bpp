/-
  BPP.SkillTree.PointBounds — Formal proofs for skill tree point allocation

  C++ source: commands/skill_tree/skill_tree.h

  Points per prestige: P1–P5: 2, P6–P10: 3, P11+: 4.
  Three branches × 25 max ranks = 75 total skill slots.
-/
namespace BPP.SkillTree.PointBounds

def pointsAt (p : Nat) : Nat :=
  if p ≤ 5 then 2
  else if p ≤ 10 then 3
  else 4

def totalPoints : Nat → Nat
  | 0 => 0
  | n + 1 => totalPoints n + pointsAt (n + 1)

/-! ## Specific prestige values -/

theorem points_p1  : totalPoints 1  = 2  := by native_decide
theorem points_p5  : totalPoints 5  = 10 := by native_decide
theorem points_p10 : totalPoints 10 = 25 := by native_decide
theorem points_p15 : totalPoints 15 = 45 := by native_decide
theorem points_p20 : totalPoints 20 = 65 := by native_decide

/-! ## Branch structure -/

def branchMaxRanks : List Nat := [5, 5, 5, 5, 3, 2]

def slotsPerBranch : Nat := branchMaxRanks.foldl (· + ·) 0

theorem slots_per_branch : slotsPerBranch = 25 := by native_decide

def totalSlots : Nat := slotsPerBranch * 3

theorem total_slots : totalSlots = 75 := by native_decide

/-! ## Prestige needed to max all skills -/

-- P22: 73 points (not enough)
theorem p22_not_enough : totalPoints 22 = 73 := by native_decide

-- P23: 77 points (enough for 75 slots)
theorem p23_enough : totalPoints 23 = 77 := by native_decide

-- Therefore need prestige 23 to max all skills
theorem need_p23_for_max : totalPoints 22 < totalSlots ∧ totalSlots ≤ totalPoints 23 := by
  constructor <;> native_decide

/-! ## Points per prestige is non-decreasing -/

theorem points_nondec (p : Nat) : pointsAt p ≤ pointsAt (p + 1) := by
  unfold pointsAt
  split <;> (try split) <;> (try split) <;> (try split) <;> omega

/-! ## Cumulative is strictly increasing -/

theorem total_strict_mono (p : Nat) : totalPoints p < totalPoints (p + 1) := by
  simp only [totalPoints]
  have : 2 ≤ pointsAt (p + 1) := by
    unfold pointsAt
    split <;> (try split) <;> omega
  omega

/-! ## Respec cost -/

def respecCost (networth : Nat) : Nat :=
  Nat.min (Nat.max (networth / 10) 500000) 500000000

theorem respec_floor : respecCost 0 = 500000 := by native_decide
theorem respec_mid   : respecCost 50000000 = 5000000 := by native_decide
theorem respec_cap   : respecCost 100000000000 = 500000000 := by native_decide

theorem respec_bounded (n : Nat) :
    500000 ≤ respecCost n ∧ respecCost n ≤ 500000000 := by
  constructor
  · simp only [respecCost, Nat.min_def, Nat.max_def]
    split <;> split <;> omega
  · simp only [respecCost, Nat.min_def, Nat.max_def]
    split <;> split <;> omega

/-! ## Gambler branch totals -/

def maxLuckBonus    : Nat := 5 * 1  -- 5%
def maxPayoutBonus  : Nat := 5 * 2  -- 10%
def maxLossReduction : Nat := 5 * 2  -- 10%

theorem gambler_luck   : maxLuckBonus    = 5  := by native_decide
theorem gambler_payout : maxPayoutBonus  = 10 := by native_decide
theorem gambler_safety : maxLossReduction = 10 := by native_decide

end BPP.SkillTree.PointBounds
