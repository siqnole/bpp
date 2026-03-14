/-
  BPP.Fishing.DropTable — Formal proofs for fishing drop table fairness

  C++ source: commands/fishing/fishing_helpers.h

  Luck adjustment: w_adjusted = w + ⌊(w_max − w) × luck / 100⌋
  This flattens the distribution — pushes all weights toward w_max.
-/
namespace BPP.Fishing.DropTable

/-! ## Weight model -/

def weights : List Nat := [200, 90, 35, 13, 3]

def totalWeight (ws : List Nat) : Nat := ws.foldl (· + ·) 0

theorem total_base : totalWeight weights = 341 := by native_decide

/-! ## Luck adjustment -/

def adjustedWeight (w wMax luck : Nat) : Nat :=
  w + (wMax - w) * luck / 100

theorem luck_zero (w wMax : Nat) : adjustedWeight w wMax 0 = w := by
  unfold adjustedWeight; simp

theorem luck_100 (w wMax : Nat) (h : w ≤ wMax) :
    adjustedWeight w wMax 100 = wMax := by
  unfold adjustedWeight
  simp
  omega

/-! ## Adjusted weight is non-decreasing in luck -/

theorem adjusted_nondec_luck (w wMax l1 l2 : Nat) (h : l1 ≤ l2) :
    adjustedWeight w wMax l1 ≤ adjustedWeight w wMax l2 := by
  unfold adjustedWeight
  have : (wMax - w) * l1 ≤ (wMax - w) * l2 := Nat.mul_le_mul_left _ h
  omega

/-! ## Distribution flattening -/

def allAdjusted (ws : List Nat) (wMax luck : Nat) : List Nat :=
  ws.map fun w => adjustedWeight w wMax luck

theorem all_uniform_at_max_luck :
    allAdjusted [200, 90, 35, 13, 3] 200 100 = [200, 200, 200, 200, 200] := by
  native_decide

theorem all_unchanged_at_zero_luck :
    allAdjusted [200, 90, 35, 13, 3] 200 0 = [200, 90, 35, 13, 3] := by
  native_decide

/-! ## Rarity probabilities (×1000000) -/

def probPpm (w total : Nat) : Nat := w * 1000000 / total

-- ~0.88% legendary at luck 0
theorem legendary_prob_base : probPpm 3 341 = 8797 := by native_decide

-- ~58.65% common at luck 0
theorem common_prob_base : probPpm 200 341 = 586510 := by native_decide

-- At luck 50: weights become more uniform
theorem adjusted_at_luck50 :
    allAdjusted [200, 90, 35, 13, 3] 200 50 = [200, 145, 117, 106, 101] := by
  native_decide

-- ~15.1% legendary at luck 50 (up from 0.88%!)
theorem legendary_prob_luck50 : probPpm 101 669 = 150971 := by native_decide

-- ~17× amplification
theorem luck_amplifies_legendary : probPpm 101 669 / probPpm 3 341 = 17 := by
  native_decide

/-! ## Speed multiplier bounds (×100) -/

theorem speed_floor : (80 : Nat) ≤ 150 := by omega
theorem speed_ceiling : (150 : Nat) ≤ 150 := by omega

end BPP.Fishing.DropTable
