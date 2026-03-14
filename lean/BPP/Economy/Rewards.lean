/-
  BPP.Economy.Rewards — Formal proofs for daily/weekly/work rewards

  C++ source: commands/economy/money.h

  Reward formulas (per unit networth, then clamped):
    daily  = clamp(8% × networth, $500, $250M)
    weekly = clamp(50% × networth, $1000, $1B)
    work   = clamp(3% × networth, $100, $25M)
-/
import BPP.Common.Bounded
namespace BPP.Economy.Rewards

open BPP.Common

/-! ## Floor/ceiling values -/

def dailyFloor  : Int := 500
def dailyCeil   : Int := 250000000
def weeklyFloor : Int := 1000
def weeklyCeil  : Int := 1000000000
def workFloor   : Int := 100
def workCeil    : Int := 25000000

/-- All floors ≤ ceilings. -/
theorem daily_floor_le_ceil  : dailyFloor ≤ dailyCeil := by native_decide
theorem weekly_floor_le_ceil : weeklyFloor ≤ weeklyCeil := by native_decide
theorem work_floor_le_ceil   : workFloor ≤ workCeil := by native_decide

/-- Clamped rewards are always in bounds. -/
theorem daily_bounded (nw : Int) :
    dailyFloor ≤ clampInt nw dailyFloor dailyCeil ∧
    clampInt nw dailyFloor dailyCeil ≤ dailyCeil :=
  clampInt_bounds nw dailyFloor dailyCeil daily_floor_le_ceil

theorem weekly_bounded (nw : Int) :
    weeklyFloor ≤ clampInt nw weeklyFloor weeklyCeil ∧
    clampInt nw weeklyFloor weeklyCeil ≤ weeklyCeil :=
  clampInt_bounds nw weeklyFloor weeklyCeil weekly_floor_le_ceil

theorem work_bounded (nw : Int) :
    workFloor ≤ clampInt nw workFloor workCeil ∧
    clampInt nw workFloor workCeil ≤ workCeil :=
  clampInt_bounds nw workFloor workCeil work_floor_le_ceil

/-! ## Break-even points

  daily reaches floor when 8% × nw ≤ 500 → nw ≤ 6250
  daily reaches ceil  when 8% × nw ≥ 250M → nw ≥ 3,125,000,000
-/

/-- Daily floor break-even: 8 × 6250 = 50000 = 500 × 100. -/
theorem daily_floor_breakeven : 8 * 6250 = 500 * 100 := by native_decide

/-- Daily ceil break-even: 8 × 3125000000 = 25000000000 = 250000000 × 100. -/
theorem daily_ceil_breakeven : 8 * 3125000000 = 250000000 * 100 := by native_decide

/-! ## Ordering: work < daily < weekly (by percentage) -/

/-- Percentage ordering: 3% < 8% < 50%. -/
theorem pct_ordering : (3 : Nat) < 8 ∧ (8 : Nat) < 50 := by omega

/-- Floor ordering: $100 < $500 < $1000. -/
theorem floor_ordering : workFloor < dailyFloor ∧ dailyFloor < weeklyFloor := by
  constructor <;> native_decide

/-- Ceiling ordering: $25M < $250M < $1B. -/
theorem ceil_ordering : workCeil < dailyCeil ∧ dailyCeil < weeklyCeil := by
  constructor <;> native_decide

end BPP.Economy.Rewards
