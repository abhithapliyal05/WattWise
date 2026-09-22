# Energy cost model

Program energy is estimated as

    E = Σ_blocks  freq(b) × Σ_{q ∈ b} cost(category(q))

Units are **model picojoules** — relative, not absolute joules (Review 1 scope).
The ordering memory ≫ divide ≫ multiply > ALU > move > branch follows the
instruction-level models of Tiwari et al. (1994) and Steinke et al. (2001).

| Key | Default pJ | IR ops |
|---|---:|---|
| mem.load | 10.0 | `Load` (array read incl. address generation) |
| mem.store | 12.0 | `Store` |
| int.alu | 1.0 | int `Add Sub Neg Not Lt Le Gt Ge Eq Ne` |
| int.shift | 1.0 | `Shl` |
| int.mul | 3.0 | int `Mul` |
| int.div | 18.0 | int `Div Mod` (multi-cycle, unpipelined) |
| fp.alu | 2.5 | double add/sub/neg/compare |
| fp.mul | 3.5 | double `Mul` |
| fp.div | 14.0 | double `Div` |
| convert | 2.0 | `I2D D2I` |
| move | 0.8 | `Copy` |
| branch.uncond | 0.6 | `Jmp` |
| branch.cond | 0.9 | `JmpF` |
| call / ret / param | 6.0 / 4.0 / 0.8 | `Call` / `Ret` / `Param` |
| io | 0.0 | `Print PrintS PrintNL` (out of scope) |

Override any subset of entries with `--costs FILE` (one `key value` per line,
`#` comments). Unlisted keys keep their defaults.

## Frequency

* **Static** (`--static`): trip counts are derived from induction variables —
  exact when start, step and bound are constants; `bound/2` for a triangular
  inner loop whose bound is the outer induction variable; otherwise 10.
  Blocks that do not dominate the loop latch get probability 0.5. Invocation
  counts propagate over the call graph; recursive calls are weighted ×10.
* **Dynamic** (`--dynamic`): the IR interpreter counts every quad execution.
  `--both` reports top-3 hotspot agreement between the two.
