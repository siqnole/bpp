/-
  BPP.Common.Bounded — Clamp and bound lemmas using integers

  Models the clamp pattern: clamp(value, floor, ceiling)
  used in reward calculations.
-/
namespace BPP.Common

/-- Clamp value to [lo, hi]. Uses explicit if-then-else for easy proofs. -/
def clampInt (x lo hi : Int) : Int :=
  if x < lo then lo
  else if hi < x then hi
  else x

theorem clampInt_le_hi (x lo hi : Int) (h : lo ≤ hi) : clampInt x lo hi ≤ hi := by
  unfold clampInt
  split
  · exact h
  · split
    · exact Int.le_refl hi
    · omega

theorem lo_le_clampInt (x lo hi : Int) (h : lo ≤ hi) : lo ≤ clampInt x lo hi := by
  unfold clampInt
  split
  · exact Int.le_refl lo
  · split
    · omega
    · omega

theorem clampInt_bounds (x lo hi : Int) (h : lo ≤ hi) :
    lo ≤ clampInt x lo hi ∧ clampInt x lo hi ≤ hi :=
  ⟨lo_le_clampInt x lo hi h, clampInt_le_hi x lo hi h⟩

end BPP.Common
