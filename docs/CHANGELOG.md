# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]

### Added
- Version, test, and optimization tracking documents.
- Automated regression test script for core license flows.
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
