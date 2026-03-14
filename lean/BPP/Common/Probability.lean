/-
  BPP.Common.Probability — Weighted discrete distribution model

  Models `std::discrete_distribution` from C++ as a list of natural-number
  weights. P(i) = weights[i] / Σ weights.
-/
namespace BPP.Common

/-- Total weight of a weight list. -/
def totalWeight (ws : List Nat) : Nat :=
  ws.foldl (· + ·) 0

/-- Probability fraction: (numerator, denominator) = (weight_i, total). -/
def probOf (ws : List Nat) (i : Nat) : Nat × Nat :=
  match ws.get? i with
  | some w => (w, totalWeight ws)
  | none => (0, totalWeight ws)

end BPP.Common
