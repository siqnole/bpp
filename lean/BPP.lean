/-
  BPP Discord Bot — Lean 4 Formal Verification Suite

  Mathematical proofs of fairness, expected value, and correctness
  for every probability and payout formula in the bot.

  All proofs use integer arithmetic (no Mathlib dependency).
  If this compiles, every theorem is proven.
-/
import BPP.Common.Probability
import BPP.Common.Bounded
import BPP.Gambling.Slots
import BPP.Gambling.Coinflip
import BPP.Gambling.Dice
import BPP.Gambling.Crash
import BPP.Gambling.Minesweeper
import BPP.Gambling.Roulette
import BPP.Gambling.Lottery
import BPP.Economy.Rewards
import BPP.Economy.BankInterest
import BPP.Economy.Prestige
import BPP.Fishing.DropTable
import BPP.Fishing.Effects
import BPP.Mining.Multimine
import BPP.Leveling.XPFormula
import BPP.SkillTree.PointBounds
import BPP.Mastery.TierBonuses
import BPP.DailyChallenges.Rewards
