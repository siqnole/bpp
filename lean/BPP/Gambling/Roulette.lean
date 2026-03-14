/-
  BPP.Gambling.Roulette — American roulette house-edge proofs

  C++ source: commands/gambling/roulette.h

  American wheel: 38 slots (0, 00, 1-36).
  All computations over denominator of 38.
-/
namespace BPP.Gambling.Roulette

def totalSlots : Nat := 38
def redSlots   : Nat := 18
def blackSlots : Nat := 18
def greenSlots : Nat := 2

theorem slots_sum : redSlots + blackSlots + greenSlots = totalSlots := by native_decide

/-! ## EV for each bet type (×38 to avoid fractions)

  Single number (35:1): win 35, lose 1.
    EV×38 = 1×35 + 37×(−1) = 35 − 37 = −2

  Red/Black/Even/Odd (1:1): covers 18.
    EV×38 = 18×1 + 20×(−1) = 18 − 20 = −2

  All bets: EV = −2/38 = −1/19 ≈ −5.26%
-/

def evSingleNumber38 : Int := 1 * 35 + 37 * (-1)
def evColor38        : Int := 18 * 1 + 20 * (-1)

theorem single_number_ev : evSingleNumber38 = -2 := by native_decide
theorem color_ev : evColor38 = -2 := by native_decide

/-- All bet types share the same house edge. -/
theorem all_bets_equal : evSingleNumber38 = evColor38 := by native_decide

/-- House edge = 2/38 = 1/19. Verify: 2 × 19 = 38. -/
theorem house_edge_fraction : 2 * 19 = totalSlots := by native_decide

/-- 1/19 > 5/100: house edge exceeds 5%. -/
theorem house_edge_gt_5pct : 100 * 1 > 19 * 5 := by native_decide

/-- 1/19 < 6/100: house edge is less than 6%. -/
theorem house_edge_lt_6pct : 100 * 1 < 19 * 6 := by native_decide

/-- So house edge is between 5% and 6% (5.26%). -/
theorem house_edge_bounds : 19 * 5 < 100 ∧ 100 < 19 * 6 := by constructor <;> native_decide

/-- Red and black have equal probability. -/
theorem red_eq_black : redSlots = blackSlots := by native_decide

/-- Green (0, 00) is the source of the house edge. Without green:
    EV = 18×1 + 18×(−1) = 0 (fair game). -/
theorem without_green_fair : (18 : Int) * 1 + 18 * (-1) = 0 := by native_decide

end BPP.Gambling.Roulette
