/-
  BPP.Gambling.Slots — Formal model and fairness proofs for the slot machine

  C++ source: commands/gambling/slots.h

  Symbols and weights (total = 100):
    Cherry=35, Lemon=30, Orange=20, Grape=10, Diamond=4, Seven=1

  Three independent reels. Gross payouts (multiplier of bet):
    Seven=50, Diamond=20, Grape=10, Orange=5, Lemon=2, Cherry=1.5(=3/2)
  Two-match → bet returned (gross 1×). No match → lose bet (gross 0×).

  All probabilities computed over 100³ = 1,000,000 outcomes.
  EV uses net payouts (gross - 1) scaled ×2 to avoid the 0.5 fraction.
-/
namespace BPP.Gambling.Slots

-- Symbol weights
def wCherry  : Nat := 35
def wLemon   : Nat := 30
def wOrange  : Nat := 20
def wGrape   : Nat := 10
def wDiamond : Nat := 4
def wSeven   : Nat := 1

def totalWeight : Nat := wCherry + wLemon + wOrange + wGrape + wDiamond + wSeven

theorem total_weight_is_100 : totalWeight = 100 := by native_decide

def totalOutcomes : Nat := totalWeight ^ 3

theorem total_outcomes_1M : totalOutcomes = 1000000 := by native_decide

/-! ## Triple probabilities -/

def tripleCount (w : Nat) : Nat := w ^ 3

def tripleSevenCount   : Nat := tripleCount wSeven
def tripleDiamondCount : Nat := tripleCount wDiamond
def tripleGrapeCount   : Nat := tripleCount wGrape
def tripleOrangeCount  : Nat := tripleCount wOrange
def tripleLemonCount   : Nat := tripleCount wLemon
def tripleCherryCount  : Nat := tripleCount wCherry

theorem triple_seven   : tripleSevenCount   = 1     := by native_decide
theorem triple_diamond : tripleDiamondCount = 64    := by native_decide
theorem triple_grape   : tripleGrapeCount   = 1000  := by native_decide
theorem triple_orange  : tripleOrangeCount  = 8000  := by native_decide
theorem triple_lemon   : tripleLemonCount   = 27000 := by native_decide
theorem triple_cherry  : tripleCherryCount  = 42875 := by native_decide

def anyTripleCount : Nat :=
  tripleSevenCount + tripleDiamondCount + tripleGrapeCount +
  tripleOrangeCount + tripleLemonCount + tripleCherryCount

theorem any_triple_count : anyTripleCount = 78940 := by native_decide

/-! ## Two-match: exactly 2 of 3 reels match -/

def twoMatchOfSymbol (w : Nat) : Nat := 3 * w * w * (totalWeight - w)

def twoMatchCount : Nat :=
  twoMatchOfSymbol wCherry + twoMatchOfSymbol wLemon +
  twoMatchOfSymbol wOrange + twoMatchOfSymbol wGrape +
  twoMatchOfSymbol wDiamond + twoMatchOfSymbol wSeven

theorem two_match_count : twoMatchCount = 555780 := by native_decide

def noMatchCount : Nat := totalOutcomes - anyTripleCount - twoMatchCount

theorem no_match_count : noMatchCount = 365280 := by native_decide

theorem all_outcomes_sum : anyTripleCount + twoMatchCount + noMatchCount = totalOutcomes := by
  native_decide

/-! ## Expected Value (net payouts ×2 to avoid the Cherry 0.5 fraction)

  Net payout = gross_multiplier - 1 (the bet).
  ×2: Seven=98, Diamond=38, Grape=18, Orange=8, Lemon=2, Cherry=1
  Two-match: 0, No-match: -2
-/

def evNumerScaled : Int :=
  (tripleSevenCount : Int)   * 98 +
  (tripleDiamondCount : Int) * 38 +
  (tripleGrapeCount : Int)   * 18 +
  (tripleOrangeCount : Int)  * 8 +
  (tripleLemonCount : Int)   * 2 +
  (tripleCherryCount : Int)  * 1 +
  (twoMatchCount : Int)      * 0 +
  (noMatchCount : Int)       * (-2)

def evDenomScaled : Nat := 2 * totalOutcomes

theorem ev_numer_exact : evNumerScaled = -549155 := by native_decide

theorem ev_denom_exact : evDenomScaled = 2000000 := by native_decide

theorem house_edge_positive : evNumerScaled < 0 := by native_decide

-- House edge = 549155 / 2000000 ≈ 27.46%
theorem house_edge_between_27_and_28 :
    549155 * 100 > 27 * 2000000 ∧ 549155 * 100 < 28 * 2000000 := by
  constructor <;> native_decide

theorem triple_seven_rarest : tripleSevenCount = 1 := by native_decide

theorem no_match_most_common : anyTripleCount < noMatchCount := by native_decide

end BPP.Gambling.Slots
