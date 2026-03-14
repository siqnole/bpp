/-
  BPP.Economy.BankInterest — Formal proofs for bank interest

  C++ source: commands/passive/bank_interest.h

  Rate = min(0.001 + prestige × 0.0005, 0.005)
  Payout = min(bank × rate, 500000)

  All rates in basis points (×10000): 10 to 50 bps.
-/
namespace BPP.Economy.BankInterest

def rateBps (prestige : Nat) : Nat :=
  if 10 + prestige * 5 ≤ 50 then 10 + prestige * 5 else 50

def payout (bank : Nat) (prestige : Nat) : Nat :=
  Nat.min (bank * rateBps prestige / 10000) 500000

/-! ## Rate bounds -/

theorem rate_at_p0  : rateBps 0  = 10 := by native_decide
theorem rate_at_p5  : rateBps 5  = 35 := by native_decide
theorem rate_at_p8  : rateBps 8  = 50 := by native_decide
theorem rate_at_p10 : rateBps 10 = 50 := by native_decide

theorem rate_ge_10 (p : Nat) : 10 ≤ rateBps p := by
  unfold rateBps; split <;> omega

theorem rate_le_50 (p : Nat) : rateBps p ≤ 50 := by
  unfold rateBps; split <;> omega

theorem rate_nondecreasing (p : Nat) : rateBps p ≤ rateBps (p + 1) := by
  unfold rateBps; split <;> split <;> omega

/-! ## Payout bounds -/

theorem payout_capped (bank : Nat) (p : Nat) : payout bank p ≤ 500000 := by
  unfold payout
  exact Nat.min_le_right _ _

theorem payout_at_100m : payout 100000000 8 = 500000 := by native_decide
theorem payout_at_1m   : payout 1000000 8   = 5000   := by native_decide

-- Cap reached at prestige 8
theorem cap_prestige : rateBps 7 < 50 ∧ rateBps 8 = 50 := by
  constructor <;> native_decide

end BPP.Economy.BankInterest
