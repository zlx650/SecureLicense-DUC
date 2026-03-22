# Test Record

## 1. Objective

Track repeatable test evidence for each major update, including command, expected behavior, and actual result.

## 2. Environment Baseline

- OS: Ubuntu (local dev machine)
- Compiler: g++ 13.x
- Language: C++17
- Project path: `/data/zhenglingxin/projects/SecureLicense-DUC-demo`

## 3. Regression Case Set (Current)

| ID | Case | Expected |
|---|---|---|
| TC00 | Unit tests: token/parse module | Pass |
| TC01 | Activate license online | Activation succeeds |
| TC02 | Run with online heartbeat | Allowed |
| TC03 | Run offline within grace window | Allowed |
| TC04 | Run offline after grace timeout | Denied |
| TC05 | Run after license expiry | Denied |
| TC06 | Run with tampered token | Denied |
| TC07 | Run with machine mismatch | Denied |
| TC08 | Run with wrong secret | Denied |
| TC09 | Persistence after server restart | Allowed (online heartbeat ok) |
| TC10 | Config file only execution | Activation and run succeed |
| TC11 | CLI overrides config values | Override takes effect |
| TC12 | Benchmark smoke | Script returns PASS |
| TC13 | HTTPS with server cert + client verify | Valid CA succeeds, wrong CA fails |

## 4. How To Execute

```bash
cd /data/zhenglingxin/projects/SecureLicense-DUC-demo
./scripts/test_regression.sh
```

Execution artifacts:

- Summary: `test_artifacts/latest_test_report.txt`
- Server log: `test_artifacts/test_server.log`

## 5. Result Template (Per Iteration)

Copy this block for each major update:

```text
Date: YYYY-MM-DD
Version: vX.Y.Z
Executor: <name>
Command: ./scripts/test_regression.sh
Result: PASS/FAIL
Failed Cases: <none or list>
Notes: <root cause / follow-up>
```

## 6. Current Baseline Result

- Date: 2026-03-22
- Version: v1.4.0
- Executor: local (`conda env: cproject`)
- Command: `./scripts/test_regression.sh`
- Status: PASS
- Summary: PASS=22, FAIL=0
- Report: `test_artifacts/latest_test_report.txt`

## 7. Iteration History

```text
Date: 2026-03-22
Version: v1.4.0
Executor: local
Command: ./scripts/test_regression.sh
Result: PASS
Failed Cases: none
Notes: added HTTPS coverage (valid CA and wrong CA failure), regression kept green at PASS=22.
```
