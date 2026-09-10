# QChat project instructions

- Follow TDD for subsequent behavioral changes: add a failing test, observe the
  relevant failure, implement the smallest fix, then refactor with tests green.
  The initial scaffold predates this preference; do not claim it was test-first.
- Keep QChat local-only. Do not add transport, telemetry, online key discovery,
  automatic plaintext persistence, destructive panic actions or security claims
  without an explicit scope change.
- Preserve German and Persian TS catalogs and RTL behavior. Persian is a draft.
- Never use real user identities in tests. Use isolated temporary GNUPGHOMEs.
- Document actual limitations in SECURITY.md; passing tests is not a security audit.
- Run relevant tests via CTest. GnuPG agent sockets may need sandbox escalation.
- Do not launch, install or publish the app as a side effect of tests.
