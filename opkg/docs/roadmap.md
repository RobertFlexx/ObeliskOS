# opkg Roadmap

## v1 (foundation, now)

- CLI skeleton with stable command routing
- `.opk` v1 format (`meta.json` + `files.tar`)
- local package DB using JSON files
- static HTTP repo index model
- install/remove/list/info/files/owner behavior scaffolding

## v1.1

- real local install/remove with file manifest writes
- local `.opk` install by file path
- `update` fetches and caches `index.json`
- `search` over repo index

## v1.2

- basic dependency handling (direct dependencies, no SAT)
- conflict checks
- checksum enforcement for downloaded packages

## v2 (later)

- package signatures
- stronger transaction semantics
- rollback strategy
- richer dependency solver

## v3 (later)

- hybrid binary/source packaging flow
- ports tree integration
- binary/source mixed repository policies

## Repository launch and ecosystem bootstrap plan

### Stage 1: web-hosted repos

- publish static `index.json` + `packages/*.opk` over HTTPS
- maintain separated channels/groups (`core`, `devel`, `extra`, `desktop`, `xfce`)
- automate index generation from package metadata

### Stage 2: initial software catalog

- core UNIX utilities first
- compiler/build/devel tools second
- scripting runtimes third
- desktop prereqs and XFCE stack after platform prerequisites

### Stage 3: ports pipeline

- native Obelisk software first
- common portable UNIX software next
- larger ecosystem packages later (after ABI/runtime stability)

## Package model direction

Recommendation: **hybrid in phases, binary-first now**.

- immediate practicality: binary packages
- medium term: add source recipes/build metadata
- long term: dual binary + source ecosystem with policy-driven defaults
