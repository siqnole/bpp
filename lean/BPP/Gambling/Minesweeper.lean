/-
  BPP.Gambling.Minesweeper — Formal model and house-edge proof

  C++ source: commands/gambling/minesweeper.h

  Grid of total_cells with num_mines. Player reveals cells one by one.
  Cumulative multiplier = 0.97 × Π (total-i)/(safe-i)
  This equals 0.97 / P(survive), giving exactly 3% house edge.
-/
namespace BPP.Gambling.Minesweeper

/-! ## Survival probability as a fraction -/

def fallingFactorial (n : Nat) (k : Nat) : Nat :=
  match k with
  | 0 => 1
  | k + 1 => (n - k) * fallingFactorial n k

def survNumer (safe r : Nat) : Nat := fallingFactorial safe r
def survDenom (total r : Nat) : Nat := fallingFactorial total r

-- Multiplier = 0.97 / P(survive) = 97 × survDenom / (100 × survNumer)
def multNumer (total : Nat) (r : Nat) : Nat := 97 * survDenom total r
def multDenom (safe : Nat) (r : Nat) : Nat := 100 * survNumer safe r

/-! ## The key fairness theorem

  EV = P(survive) × multiplier - 1
     = (survNumer/survDenom) × (97×survDenom)/(100×survNumer) - 1
     = 97/100 - 1 = -3/100

  In integers: survNumer × multNumer × 100 = survDenom × multDenom × 97
  i.e., survNumer × (97 × survDenom) × 100 = survDenom × (100 × survNumer) × 97
  Both sides = 97 × 100 × survNumer × survDenom.
-/

theorem ev_always_097 (total safe r : Nat) :
    survNumer safe r * multNumer total r * 100 =
    survDenom total r * multDenom safe r * 97 := by
  unfold multNumer multDenom survNumer survDenom
  -- Goal: fallingFactorial safe r * (97 * fallingFactorial total r) * 100
  --     = fallingFactorial total r * (100 * fallingFactorial safe r) * 97
  simp [Nat.mul_comm, Nat.mul_assoc, Nat.mul_left_comm]

theorem house_edge_3pct : 100 - 97 = 3 := by omega

/-! ## Concrete examples -/

-- Easy: 25 cells, 4 mines, 21 safe
theorem easy_surv_1 : survNumer 21 1 = 21 := by native_decide
theorem easy_denom_1 : survDenom 25 1 = 25 := by native_decide

-- Hard: 25 cells, 9 mines, 16 safe
theorem hard_surv_1 : survNumer 16 1 = 16 := by native_decide
theorem hard_denom_1 : survDenom 25 1 = 25 := by native_decide

-- Multiplier is higher for harder difficulty (same total):
-- Easy mult ×100: 97×25/(100×21) = 2425/2100
-- Hard mult ×100: 97×25/(100×16) = 2425/1600
-- Hard > Easy because denominator is smaller
theorem harder_means_higher_mult :
    multNumer 25 1 * multDenom 16 1 < multNumer 25 1 * multDenom 21 1 := by
  native_decide

-- Cross-check: multDenom easy > multDenom hard (more safe cells = larger denom)
theorem more_safe_larger_denom :
    multDenom 16 1 < multDenom 21 1 := by native_decide

-- 5-reveal fairness still holds
theorem easy_fairness_5 :
    survNumer 21 5 * multNumer 25 5 * 100 =
    survDenom 25 5 * multDenom 21 5 * 97 := by
  unfold multNumer multDenom survNumer survDenom
  simp [Nat.mul_comm, Nat.mul_assoc, Nat.mul_left_comm]

end BPP.Gambling.Minesweeper
