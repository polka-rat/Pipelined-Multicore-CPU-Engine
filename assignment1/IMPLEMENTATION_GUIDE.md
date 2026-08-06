# Implementation Guide

Read this after `SPEC.md` and `tutorial.cc`.

## What's given vs. what you write

`pipesim.h` and `framework.cc` are the harness: program loading, the
command loop, the clock engine, structural validation. `design.cc` is
the processor, and it's the whole assignment.

`design.cc` here already compiles and runs. Every `TODO` is part of the
assignment. Everything else is plumbing.

## Concepts

- **Stage** (`StageRole`: `IF`/`ID`/`EX`/`MEM`/`WB`, `AddStage`) —
  combinational logic: reads the current latch, computes the next latch,
  holds no state of its own between cycles.
- **Clocked state** (`RegisterRole`: PC + the four latches,
  `AddRegister`) — the only thing that actually remembers anything
  across a cycle. Updates only in `Commit()`, called once every stage
  has finished evaluating — this is what makes the cycle behave like a
  synchronous clock edge instead of stages stepping on each other's
  inputs mid-cycle.
- **Functional/unit role** (`UnitRole`: ALU, MainControl,
  HazardControl, `AddUnit`) — stateless logic a stage calls into (EX
  calls the ALU; ID calls MainControl and HazardControl). Not a stage
  and not clocked state — it doesn't run on its own each cycle, it's
  invoked.
- **Coarse connection** (`Connect(...)`) — an edge the structural check
  verifies *exists*, not that it's actually *used*. You could register
  a `Connect(alu_node, execute_node, kData)` edge and never call
  `alu_->Run(...)` in `Execute()`, and the check would still pass —
  hence "coarse." SPEC.md treats this as evidence, not proof, and
  expects a short code review or viva to back it up.

**Stall / bubble / redirect** are the three ways the front end deviates
from "just keep fetching," each from a different source:

| | Source | Effect |
|---|---|---|
| Stall | HazardControl detects a RAW hazard | PC and `IF/ID` held — same instruction re-presented next cycle |
| Bubble | same hazard event | `ID/EX` gets an invalid latch instead of the stalled instruction |
| Redirect | EX resolves a branch/jump | PC overwritten with the target; `IF/ID` cleared, not held |

In the scaffold these collapse into one `front_action_` value
(`kHold`/`kStop`/`kRedirect`) that `Fetch()` reads each cycle. Stall and
bubble are the same hazard event seen from two different latches;
redirect is a genuinely separate event and takes priority if it could
ever coincide with a stall.

## Mapping `tutorial.cc` onto the real API

| `tutorial.cc` | Real API |
|---|---|
| `ValueRegister` (current/next, `Commit()`) | `Latch` + a `ClockedState` |
| `Counter` / `Accumulator` | `Core`'s per-stage methods |
| `ExampleBuilder` | `ProcessorBuilder` (already validates the required shape) |
| `Clock()` | `Processor::Clock()`, order `WB, MEM, EX, ID, IF`, then commit |

`tutorial.cc` doesn't show one thing this scaffold uses: a single `Core`
holding all latches and stage logic, with thin `Stage`/`Register`
adapters forwarding the framework's calls to it. Five independent stage
classes would each need access to the same latches; one shared object
avoids that. Not required — any layout that satisfies `SPEC.md`'s
structural check is fine.

## Wiring `BuildDesign()`

Nothing runs until the full graph is registered — `builder.Build()`
validates the structure before `main()` can even call `Reset()`. The
required roles and every required edge are listed in SPEC.md. One
register/stage pair is wired as a worked example; the rest follow the
same `AddRegister` / `AddStage` / `AddUnit` shape, each returning a
`NodeId` you pass to `Connect()`.

Run it as you go — the error list is your checklist:

```
$ make && ./pipesim example.pisa
error: invalid processor structure:
  - missing ID stage
  - missing EX stage
  ...
```

Register/connect one piece, rebuild, rerun, watch the list shrink.

## Suggested order

1. Finish `BuildDesign()` until the structure error list is empty
2. `IsControl`, `ReadsRs1`, `ReadsRs2`
3. `MainControl::Decode` (rest of it — `ADDI` is done)
4. `Alu::Run` (rest of it — `ADD` is done)
5. `Decode()`'s register reads + control lookup, hazard checking still off
6. `Execute()`'s ALU call
7. `HazardControl`
8. `BNE`, `J`, `HALT` in `Execute()` (`BEQ` is done)
9. `Memory()`

Use `pipe` after each `n 1` to watch stage occupancy directly, and `s`
for stall/cycle counts, as you go.
