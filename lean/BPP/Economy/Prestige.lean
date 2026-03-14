/-
  BPP.Economy.Prestige — Formal proofs for prestige requirements

  C++ source: commands/economy/prestige.h

  Requirements scale exponentially: requirement(P) = base × multiplier^P
-/
namespace BPP.Economy.Prestige

/-! ## Networth requirements (exact: 500M × 2^P) -/

def networthReq (p : Nat) : Nat := 500000000 * 2 ^ p

theorem networth_p0  : networthReq 0  = 500000000    := by native_decide
theorem networth_p1  : networthReq 1  = 1000000000   := by native_decide
theorem networth_p5  : networthReq 5  = 16000000000  := by native_decide
theorem networth_p10 : networthReq 10 = 512000000000 := by native_decide

theorem networth_strict_mono (p : Nat) : networthReq p < networthReq (p + 1) := by
  unfold networthReq
  simp only [Nat.pow_succ]
  have : 0 < 2 ^ p := by
    induction p with
    | zero => simp
    | succ n ih => simp [Nat.pow_succ]; omega
  omega

/-! ## Fish requirements (×10000 fixed-point lookup tables) -/

def commonReq10k : List Nat :=
  [10000000, 11000000, 12100000, 13310000, 14641000,
   16105100, 17715610, 19487171, 21435888, 23579477, 25937424]

def rareReq10k : List Nat :=
  [2000000, 2300000, 2645000, 3041750, 3498012,
   4022714, 4626121, 5320039, 6118045, 7035752, 8091115]

def epicReq10k : List Nat :=
  [500000, 625000, 781250, 976562, 1220703,
   1525878, 1907348, 2384185, 2980231, 3725290, 4656612]

def legendaryReq10k : List Nat :=
  [100000, 140000, 196000, 274400, 384160,
   537824, 752953, 1054135, 1475789, 2066105, 2892547]

/-! ## Monotonicity via Bool-returning function -/

def isStrictlyIncBool : List Nat → Bool
  | [] => true
  | [_] => true
  | a :: b :: rest => a < b && isStrictlyIncBool (b :: rest)

theorem common_increasing    : isStrictlyIncBool commonReq10k    = true := by native_decide
theorem rare_increasing      : isStrictlyIncBool rareReq10k      = true := by native_decide
theorem epic_increasing      : isStrictlyIncBool epicReq10k      = true := by native_decide
theorem legendary_increasing : isStrictlyIncBool legendaryReq10k = true := by native_decide

/-! ## Cross-rarity ordering at each prestige level -/

theorem rarity_ordering_p0 :
    legendaryReq10k[0]! < epicReq10k[0]! ∧
    epicReq10k[0]! < rareReq10k[0]! ∧
    rareReq10k[0]! < commonReq10k[0]! := by native_decide

theorem rarity_ordering_p5 :
    legendaryReq10k[5]! < epicReq10k[5]! ∧
    epicReq10k[5]! < rareReq10k[5]! ∧
    rareReq10k[5]! < commonReq10k[5]! := by native_decide

theorem rarity_ordering_p10 :
    legendaryReq10k[10]! < epicReq10k[10]! ∧
    epicReq10k[10]! < rareReq10k[10]! ∧
    rareReq10k[10]! < commonReq10k[10]! := by native_decide

/-! ## Prestige fish (starts at P5, mult 1.3×) -/

def prestigeFishReq10k : List Nat :=
  [10000, 13000, 16900, 21970, 28561, 37129]

theorem prestige_fish_increasing : isStrictlyIncBool prestigeFishReq10k = true := by
  native_decide

/-! ## Legendary grows fastest: ratio comparison -/

-- At P0, legendary req is 10× smaller than common
-- At P10, legendary has overtaken in growth terms
theorem growth_rate_ordering :
    legendaryReq10k[0]! * 10 < commonReq10k[0]! ∧
    legendaryReq10k[10]! * 10 > commonReq10k[10]! := by
  native_decide

end BPP.Economy.Prestige
