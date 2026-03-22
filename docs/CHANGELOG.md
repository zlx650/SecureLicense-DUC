# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]

### Added
- No pending entries.

### Changed
- No pending entries.

## [v1.3.1] - 2026-03-22

### Added
- Benchmark output now includes latency percentiles:
  - `p50`
  - `p95`
  - `p99`
- Benchmark output now includes categorized failure summary:
  - `activate_failures`
  - `run_failures`

### Changed
- Benchmark result file format changed to `STATUS|LATENCY_MS|FAIL_CLASS` for easier post-analysis.
- Regression baseline remains PASS=19, with `TC12` validating upgraded benchmark output path.

## [v1.3.0] - 2026-03-22

### Added
- Benchmark script: `tools/benchmark/bench_license_flow.sh` for concurrent activate/run throughput smoke.
- GitHub Actions CI: `.github/workflows/ci.yml` (build dependencies + regression test gate).
- Regression case `TC12` for benchmark smoke verification.
- Unit tests for logging utilities:
  - `log_level_parse`
  - `request_id_unique`

### Changed
- Client command path now emits structured JSON logs (`component=client`, `request_id`) across activate/run success and failure paths.
- Regression summary baseline expanded to PASS=19 with benchmark smoke included.

## [v1.2.1] - 2026-03-21

### Added
- Config file module (`config.hpp` / `config.cpp`) with key-value parser.
- Default configuration files:
  - `config/server.conf`
  - `config/client.conf`
- Support for `--config` and `--log-level` in server and client.
- Regression cases:
  - `TC10.1/TC10.2`: config-file-only activation/run.
  - `TC11.1/TC11.2`: command-line override over config values.

### Changed
- Runtime config source changed to: `default values < config file < command line`.
- Build scripts (`run_demo.sh`, `scripts/test_regression.sh`) now compile with `src/config.cpp`.
- Regression summary baseline expanded to PASS=18 with config and override coverage.

## [v1.2.0] - 2026-03-21

### Added
- SQLite license persistence module (`LicenseStore`) with schema initialization.
- Server startup option `--db` to configure SQLite file path.
- Regression cases `TC09.1/TC09.2` for restart persistence verification.
- Unit test case `sqlite_store_roundtrip` in `duccore_tests`.
- Conda-aware SQLite include/lib detection in helper scripts.

### Changed
- License state check in heartbeat path migrated from in-memory map to SQLite query.
- Build scripts (`run_demo.sh`, `scripts/test_regression.sh`) now link SQLite and support conda environments.
- Regression summary baseline expanded to PASS=14 with SQLite persistence covered.

## [v1.1.0] - 2026-03-21

### Added
- Structured logging module with log level and request ID support.
- Basic unit test target for token and parser logic (`duccore_tests`).

### Changed
- Replaced demo signature with HMAC-SHA256.
- Integrated server-side request lifecycle logs in JSON format.
- Expanded regression suite to include unit-test gate (TC00).

## [v1.0.0] - 2026-03-21

### Added
- C++17 license server and client demo.
- Cloud activation endpoint: `/activate`.
- Heartbeat validation endpoint: `/heartbeat`.
- Local token verification and expiration check.
- Offline grace-period fallback.
- Local/remote time rollback basic detection.
- One-command end-to-end demo script.

### Notes
- This release is an MVP intended for architecture validation.
- Security primitives and transport are intentionally simplified.
