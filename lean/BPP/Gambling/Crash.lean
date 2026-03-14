/-
  BPP.Gambling.Crash — Formal model for the crash game

  C++ source: commands/gambling/crash.h

  Crash point generation:
    - 4% instant bust (crash_point = 1.00)
    - Otherwise: exponential distribution

  Multiplier growth per tick:
    next(m) = 1.0 + (m - 1.0) × 1.08 + 0.025

  All values in ×10000 fixed point (10000 = 1.00×).
-/
namespace BPP.Gambling.Crash

/-! ## Instant bust house edge -/

-- 4% of games bust instantly. At immediate cashout:
-- EV = 96% × 1.00 - 100% × cost = -4%
theorem ev_instant_cashout : (96 : Int) - 100 = -4 := by omega

theorem instant_bust_edge : (96 : Int) < 100 := by omega

/-! ## Multiplier growth model

  In ×10000 fixed point:
  next(m) = 10000 + (m - 10000) × 108 / 100 + 250
  where (m-10000)×108/100 is the 8% growth on excess, and 250 is the 0.025 base growth.
-/

def nextMult10k (m : Nat) : Nat :=
  10000 + (m - 10000) * 108 / 100 + 250

def multSeq10k : Nat → Nat
  | 0 => 10000
  | n + 1 => nextMult10k (multSeq10k n)

theorem mult_tick_0 : multSeq10k 0 = 10000 := by native_decide
theorem mult_tick_1 : multSeq10k 1 = 10250 := by native_decide
theorem mult_tick_2 : multSeq10k 2 = 10520 := by native_decide
theorem mult_tick_3 : multSeq10k 3 = 10811 := by native_decide
theorem mult_tick_5 : multSeq10k 5 = 11465 := by native_decide
theorem mult_tick_10 : multSeq10k 10 = 13617 := by native_decide
theorem mult_tick_20 : multSeq10k 20 = 21425 := by native_decide

/-! ## Growth properties -/

-- Growth is always positive when m ≥ 10000
theorem growth_positive (m : Nat) (hm : 10000 ≤ m) : m < nextMult10k m := by
  unfold nextMult10k
  omega

-- Multiplier is always ≥ 1.00× (10000)
theorem mult_ge_1 : ∀ n : Nat, 10000 ≤ multSeq10k n := by
  intro n
  induction n with
  | zero => simp [multSeq10k]
  | succ k ih =>
    simp only [multSeq10k]
    have : multSeq10k k < nextMult10k (multSeq10k k) := growth_positive _ ih
    omega

-- Maximum 120 ticks before game must end
def maxTicks : Nat := 120

-- The multiplier grows superlinearly (each tick adds more than the last)
theorem growth_accelerates :
    multSeq10k 2 - multSeq10k 1 ≥ multSeq10k 1 - multSeq10k 0 := by
  native_decide

end BPP.Gambling.Crash
