# Optimization Roadmap

## Goal

Prioritize changes that improve interview defensibility, runtime reliability, and production readiness.

## Completed In Current Iteration

1. Signature upgrade
- Replaced lightweight demo signature with HMAC-SHA256.

2. Structured logging foundation
- Added JSON logs with `level`, `component`, and `request_id`.

3. Test baseline expansion
- Added unit test binary (`duccore_tests`) and integrated it into regression gate (TC00).

## P0 (Next 1-2 iterations)

1. Expand unit-test coverage depth
- Current: token + parser + cache + request-line basic tests.
- Target: time rollback logic, heartbeat response parsing, malformed HTTP response handling.
- Interview value: boundary modeling and failure-driven development.

2. Add failure-path coverage scripts
- Scope: invalid token, wrong machine, expired license, timeout path.
- Interview value: resilience and exception handling.

## P1 (Mid-term)

1. Use TLS for client-server communication
- Target: HTTPS, optional mTLS.
- Interview value: transport security.

2. Persist server-side license state
- Current: in-memory map.
- Target: SQLite/PostgreSQL with schema and migration script.
- Interview value: consistency, durability, recovery strategy.

3. Add rate limit and abuse control
- Target: per-machine request limit and backoff.
- Interview value: defensive backend design.

4. Add metrics and pressure test tooling
- Target: latency, throughput, failure ratio, CPU/memory profile.
- Interview value: performance and capacity planning.

## P2 (Long-term)

1. Refactor into layered architecture
- Split API layer, service layer, storage layer, security layer.

2. Introduce configurable policy engine
- Grace policy, expiry policy, machine binding policy.

3. Add CI pipeline
- Build + lint + unit test + regression script.

## Interview-focused Gap Summary

- C++ depth gaps: RAII usage patterns, exception-safe resource wrappers.
- OS/network depth gaps: socket timeout policy, retry/backoff strategy.
- Test depth gaps: currently no gtest/catch2 unit tests.
- Operations gaps: no CI, no metrics dashboard, no structured log index.

## Recommended Iteration Rhythm

For each major change:

1. Update code.
2. Run `./scripts/test_regression.sh`.
3. Append evidence to `docs/TEST_RECORD.md`.
4. Add change notes to `docs/CHANGELOG.md` under `[Unreleased]`.
5. Move notes into next tag section at release time.
