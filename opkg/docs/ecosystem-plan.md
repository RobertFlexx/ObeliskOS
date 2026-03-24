# Software Ecosystem Plan

This document outlines how `opkg` grows into Obelisk's real software distribution
foundation.

## Repository hosting bootstrap

1. start with static HTTPS hosting for `index.json` and `packages/*.opk`
2. generate indexes in CI from package metadata
3. publish per-channel repos:
   - `core`
   - `devel`
   - `extra`
   - `desktop`
   - `xfce`

## Catalog growth strategy

### wave 1: native foundation

- Obelisk-native admin/runtime tools
- shell/base utilities not yet covered

### wave 2: portable UNIX software

- common tools with small dependency surface
- basic developer toolchain packages
- scripting runtime essentials

### wave 3: larger ecosystem software

- desktop-oriented stacks
- broader user applications
- XFCE ecosystem completeness

## Category priorities

- core UNIX tools
- devel tools
- scripting runtimes
- desktop stack prerequisites
- XFCE stack

## Porting strategy

- prioritize native Obelisk software first
- then portable, low-friction UNIX software
- defer heavyweight ports until ABI/runtime settle
