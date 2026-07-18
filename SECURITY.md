# Security policy

This project is access-control firmware, so security reports get priority over every
other kind of issue. It is also a personal research project with an explicit caveat: it
is provided as is and should not be relied on to secure anything of value.

## Reporting a vulnerability

Report vulnerabilities privately through GitHub:
**Security tab → Report a vulnerability** on this repository. Please do not open a
public issue or PR for a security problem before it is fixed.

A useful report includes the affected file or subsystem, the firmware commit, and a
reproduction (a frame capture, a failing input, or a test case is ideal).

Expectations, honestly stated for a single-maintainer project: acknowledgment within
7 days, best-effort fixes with no guaranteed timeline, and credit in the release notes
if you want it. There is no bug bounty.

## Scope

In scope:

- Parsing and session code that consumes attacker-controlled radio input (BLE, UWB, NFC
  paths in `modules/`).
- The credential-to-ranging binding: anything that lets ranging be spoofed, replayed, or
  unbound from the authenticated key (STS derivation, key ladder, M1-M4 handling).
- Key material handling in this repository's code.

Out of scope:

- Vulnerabilities in upstream components (nRF Connect SDK, the Nordic add-on, Zephyr,
  the DW3000 driver): report those upstream, though a heads-up here is welcome if this
  project is affected.
- Physical and hardware-level attacks on the DIY reader assembly.
- Findings that amount to the documented threat model limits (for example, the add-on
  handing the engine a plaintext ranging key is a known design seam, noted in the
  README).

## Supported versions

The latest tagged release and `main` are supported. Older tags are not patched;
fixes land on `main` and ship in the next release.
