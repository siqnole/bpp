import Lake
open Lake DSL

package «bpp-proofs» where
  version := v!"0.1.0"
  leanOptions := #[
    ⟨`autoImplicit, false⟩
  ]

@[default_target]
lean_lib «BPP» where
  srcDir := "."
  roots := #[`BPP]
