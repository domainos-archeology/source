---
name: test-loop-gate
description: How to run the host test gate by hand when `make test` hangs or a foreign test misbehaves - find-based enumeration, the .dSYM/Contents glob trap, per-test timeout
metadata:
  type: project
---

Run the host test loop by hand as
`for t in $(find build/host/test[/<sub>] -type f -perm -u+x -name 'test_*' ! -name '*.log' ! -path '*.dSYM*'); do (cd $(dirname $t) && timeout 20 ./$(basename $t)) > $t.log 2>&1 || echo FAILED: $t; done`.

**Why:** `make test` has no per-test timeout, so one hung foreign test (ast/test_reserve on 2026-09-22) stalls the whole gate for the session; and a plain `ls build/host/test/<sub>/test/test_*` glob also matches the `test_x.dSYM/Contents` directories clang leaves next to each binary, which then show up as 34 phantom "FAILED: Contents" rows.

**How to apply:** when the gate must be reported per-subsystem or the full `make test` is stuck, use the find form above (it is what the 423-test full loop was run with); report the foreign hang by name rather than waiting on it. Related: [[feedback_fidelity_gates]].
