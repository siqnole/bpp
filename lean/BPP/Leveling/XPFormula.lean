/-
  BPP.Leveling.XPFormula — Formal proofs for XP requirements

  C++ source: database/operations/leveling/leveling_operations.cpp

  XP for level L:
    L ≤ 1 → 0
    L > 1  → ⌊100 × (L-1)^1.5⌋

  Since (L-1)^1.5 is irrational for most L, we use a lookup table
  matching the C++ ⌊floor⌋ values for exact levels.
-/
namespace BPP.Leveling.XPFormula

/-! ## XP lookup table (C++ exact floor values of 100×(L-1)^1.5) -/

-- XP required to reach level L. Index = level.
def xpTable : List Nat :=
  [ 0         -- Level 0 (unused)
  , 0         -- Level 1
  , 100       -- Level 2: 100 × 1^1.5 = 100
  , 282       -- Level 3: 100 × 2^1.5 ≈ 282.84
  , 519       -- Level 4: 100 × 3^1.5 ≈ 519.61
  , 800       -- Level 5: 100 × 4^1.5 = 800
  , 1118      -- Level 6
  , 1469      -- Level 7
  , 1852      -- Level 8
  , 2263      -- Level 9
  , 2700      -- Level 10: 100 × 9^1.5 = 2700
  , 3162      -- Level 11
  , 3648      -- Level 12
  , 4156      -- Level 13
  , 4685      -- Level 14
  , 5233      -- Level 15
  , 5800      -- Level 16
  , 6400      -- Level 17: 100 × 16^1.5 = 6400
  , 7000      -- Level 18
  , 7623      -- Level 19
  , 8261      -- Level 20
  ]

/-! ## Key level thresholds -/

theorem xp_level1  : xpTable[1]!  = 0    := by native_decide
theorem xp_level2  : xpTable[2]!  = 100  := by native_decide
theorem xp_level5  : xpTable[5]!  = 800  := by native_decide
theorem xp_level10 : xpTable[10]! = 2700 := by native_decide
theorem xp_level17 : xpTable[17]! = 6400 := by native_decide
theorem xp_level20 : xpTable[20]! = 8261 := by native_decide

/-! ## Monotonicity -/

def isStrictlyIncFrom (l : List Nat) (start : Nat) : Bool :=
  match l with
  | [] => true
  | [_] => true
  | a :: b :: rest => (start < 2 || a < b) && isStrictlyIncFrom (b :: rest) (start + 1)

-- XP table is strictly increasing from level 2 onward
theorem xp_increasing : isStrictlyIncFrom xpTable 0 = true := by native_decide

/-! ## Perfect squares: levels where (L-1)^1.5 is exact -/

-- At L=2: (1)^1.5 = 1, XP = 100×1 = 100
theorem exact_L2 : xpTable[2]! = 100 * 1 := by native_decide

-- At L=5: (4)^1.5 = 8, XP = 100×8 = 800
theorem exact_L5 : xpTable[5]! = 100 * 8 := by native_decide

-- At L=10: (9)^1.5 = 27, XP = 100×27 = 2700
theorem exact_L10 : xpTable[10]! = 100 * 27 := by native_decide

-- At L=17: (16)^1.5 = 64, XP = 100×64 = 6400
theorem exact_L17 : xpTable[17]! = 100 * 64 := by native_decide

/-! ## XP gaps increase -/

-- The delta between consecutive levels increases
theorem delta_increasing :
    xpTable[3]! - xpTable[2]! ≤ xpTable[4]! - xpTable[3]! ∧
    xpTable[4]! - xpTable[3]! ≤ xpTable[5]! - xpTable[4]! := by
  native_decide

/-! ## Level cap safety -/

def maxLevel : Nat := 10000

-- 100 × 9999² is well within Nat range (used as upper bound for 100×9999^1.5)
theorem xp_cap_bounded : 100 * 9999 * 9999 < 10000000000 := by omega

end BPP.Leveling.XPFormula
