# CS6600-18-pipesim

A single-issue, five-stage, in-order processor simulator for the CS6600 Computer Architecture assignment. The simulated pipeline has the following stages:

```text
IF -> ID -> EX -> MEM -> WB
```

The simulator implements the toy PISA instruction set and supports data-hazard stalls, control-hazard stalls, branch/jump redirection, memory accesses, and correct HALT pipeline draining.

## Repository layout

```text
assignment1/
  design.cc                  Processor datapath and control implementation
  framework.cc               Simulation framework and command loop
  pipesim.h                  Framework API and ISA definitions
  SPEC.md                    Assignment specification
  IMPLEMENTATION_GUIDE.md    Implementation notes
  tests/                     PISA test programs
```

## Build

Open a terminal in the assignment directory:

```powershell
cd assignment1
mingw32-make
```

This produces `pipesim.exe`. On Linux or macOS, use `make` instead if GNU Make is installed.

## Run a program

```powershell
.\pipesim.exe .\tests\basic_alu.pisa
```

The simulator reads commands from standard input:

```text
n          advance one cycle
n N        advance up to N cycles, stopping when HALT retires
p          print registers, non-zero memory, and status
pipe       print the current five-stage pipeline snapshot
graph      print the datapath/control structure
s          print cycle, retirement, and stall statistics
reset      restore the initial program state
q          quit
```

For example:

```text
n 100
p
s
q
```

## Run the supplied tests

Run one test non-interactively:

```powershell
"n 100`np`ns`nq" | .\pipesim.exe .\tests\raw_hazard.pisa
```

Run every test program:

```powershell
Get-ChildItem .\tests\*.pisa | ForEach-Object {
  Write-Host "`n=== $($_.Name) ==="
  "n 100`np`ns`nq" | .\pipesim.exe $_.FullName
}
```

Useful test cases include:

- `basic_alu.pisa`: arithmetic and logical instructions
- `raw_hazard.pisa`: consecutive read-after-write hazards
- `load_use.pisa`: load-use hazard
- `store.pisa`: store instruction behavior
- `branch_taken.pisa`, `branch_not_taken.pisa`, and `bne_taken.pisa`: conditional branches
- `jump.pisa`: unconditional jump and PC redirection
- `halt_drain.pisa`: HALT retirement after pipeline drain

## Pipeline behavior

The design does not use forwarding. When an instruction in ID depends on a result still in ID/EX or EX/MEM, the processor holds the PC and IF/ID register, inserts a bubble into ID/EX, and records a data stall. Branches and jumps are held while their target is resolved in EX, then the PC is redirected and a control stall is recorded.
