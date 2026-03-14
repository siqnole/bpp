/-
  BPP.DailyChallenges.Rewards — Formal proofs for daily challenge rewards

  C++ source: commands/daily_challenges/challenges.h, streaks.h

  Reward = clamp(networth × base_pct, 500, 50,000,000)
  | Difficulty | base_pct | XP  |
  |------------|----------|-----|
  | Easy       | 2%       | 50  |
  | Medium     | 4%       | 100 |
  | Hard       | 8%       | 200 |
-/
namespace BPP.DailyChallenges.Rewards

def basePct100 (difficulty : Nat) : Nat :=
  match difficulty with
  | 0 => 2   -- Easy
  | 1 => 4   -- Medium
  | 2 => 8   -- Hard
  | _ => 0

def xpReward (difficulty : Nat) : Nat :=
  match difficulty with
  | 0 => 50
  | 1 => 100
  | 2 => 200
  | _ => 0

def coinReward (networth : Nat) (difficulty : Nat) : Nat :=
  let raw := networth * basePct100 difficulty / 100
  if raw < 500 then 500
  else if raw > 50000000 then 50000000
  else raw

/-! ## Difficulty ordering -/

theorem pct_ordering : basePct100 0 < basePct100 1 ∧ basePct100 1 < basePct100 2 := by
  constructor <;> native_decide

theorem xp_ordering  : xpReward 0 < xpReward 1 ∧ xpReward 1 < xpReward 2 := by
  constructor <;> native_decide

theorem hard_4x_easy     : basePct100 2 = 4 * basePct100 0 := by native_decide
theorem medium_2x_easy_xp : xpReward 1  = 2 * xpReward 0   := by native_decide

/-! ## Reward bounds -/

theorem reward_floor (nw diff : Nat) : 500 ≤ coinReward nw diff := by
  simp only [coinReward]
  split
  · omega
  · split <;> omega

theorem reward_cap (nw diff : Nat) : coinReward nw diff ≤ 50000000 := by
  simp only [coinReward]
  split
  · omega
  · split <;> omega

/-! ## Specific examples -/

theorem poor_easy : coinReward 1000 0 = 500 := by native_decide
theorem poor_hard : coinReward 1000 2 = 500 := by native_decide
theorem mid_easy  : coinReward 10000000 0 = 200000 := by native_decide
theorem mid_hard  : coinReward 10000000 2 = 800000 := by native_decide
theorem rich_easy : coinReward 1000000000 0 = 20000000 := by native_decide
theorem rich_hard : coinReward 1000000000 2 = 50000000 := by native_decide

/-! ## Streak milestones -/

def streakMilestones : List (Nat × Nat) :=
  [ (3, 5000), (7, 50000), (14, 200000), (21, 500000), (30, 1000000),
    (60, 5000000), (90, 10000000), (180, 50000000), (365, 250000000) ]

theorem max_streak : (streakMilestones.map Prod.snd).max? = some 250000000 := by
  native_decide

def totalStreakBonus : Nat := (streakMilestones.map Prod.snd).foldl (· + ·) 0

theorem total_streak : totalStreakBonus = 316755000 := by native_decide

end BPP.DailyChallenges.Rewards
