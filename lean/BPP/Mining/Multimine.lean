/-
  BPP.Mining.Multimine — Formal proofs for multimine probability distribution

  C++ source: commands/mining/mining_helpers.h

  Roll ∈ [0, 100):
  - roll < 40: 0 extra ores
  - roll ≥ 40: extras = 1 + ⌊((roll-40)/60)² × (multimine-1)⌋

  Quadratic bias toward lower extra counts.
-/
namespace BPP.Mining.Multimine

def probNoExtras : Nat := 40
def probSomeExtras : Nat := 60

theorem prob_sum : probNoExtras + probSomeExtras = 100 := by native_decide

/-! ## Discrete distribution -/

def extras (roll : Nat) (mm : Nat) : Nat :=
  if roll < 40 then 0
  else
    let frac_pct := roll - 40
    1 + frac_pct * frac_pct * (mm - 1) / 3600

-- At mm=1: all "extras" rolls give exactly 1 (since mm-1 = 0)
theorem mm1_gives_1 (roll : Nat) (h : 40 ≤ roll) (h2 : roll < 100) :
    extras roll 1 = 1 := by
  unfold extras; simp; omega

-- At roll 40: always 1 extra (frac_pct = 0)
theorem roll40_gives_1 (mm : Nat) (_h : 1 ≤ mm) : extras 40 mm = 1 := by
  unfold extras; simp

-- Max roll at mm=5
theorem roll99_mm5 : extras 99 5 = 4 := by native_decide

-- Max roll at mm=10
theorem roll99_mm10 : extras 99 10 = 9 := by native_decide

/-! ## Expected extras -/

def extrasSum (mm : Nat) : Nat :=
  List.range 60 |>.map (fun i => extras (40 + i) mm) |>.foldl (· + ·) 0

def avgExtras1000 (mm : Nat) : Nat :=
  extrasSum mm * 1000 / 100

theorem avg_mm1  : avgExtras1000 1  = 600  := by native_decide
theorem avg_mm5  : avgExtras1000 5  = 1150 := by native_decide
theorem avg_mm10 : avgExtras1000 10 = 2120 := by native_decide

/-! ## Quadratic bias: most rolls yield LOW extras -/

def countExtrasEq (mm target : Nat) : Nat :=
  List.range 60 |>.filter (fun i => extras (40 + i) mm == target) |>.length

-- At mm=5: 30 of 60 rolls give exactly 1 extra (the minimum)
theorem mm5_mostly_1 : countExtrasEq 5 1 = 30 := by native_decide

-- Only 8 rolls give the maximum (4 extras)
theorem mm5_max_count : countExtrasEq 5 4 = 8 := by native_decide

def countExtrasLE (mm cap : Nat) : Nat :=
  List.range 60 |>.filter (fun i => extras (40 + i) mm ≤ cap) |>.length

-- 43/60 ≈ 71.7% of extras rolls give ≤ 2
theorem mm5_le2 : countExtrasLE 5 2 = 43 := by native_decide

-- 52/60 ≈ 86.7% give ≤ 3
theorem mm5_le3 : countExtrasLE 5 3 = 52 := by native_decide

/-! ## E[frac²] ≈ 1/3 (continuous approximation) -/

def fracSqSum : Nat :=
  List.range 60 |>.map (fun i => i * i) |>.foldl (· + ·) 0

theorem frac_sq_sum_val : fracSqSum = 70210 := by native_decide

-- E[frac²] ×10000 ≈ 3250 (close to 1/3 = 3333)
theorem mean_frac_sq_approx : fracSqSum * 10000 / (60 * 3600) = 3250 := by native_decide

end BPP.Mining.Multimine
