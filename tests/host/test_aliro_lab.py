#!/usr/bin/env python3
"""Unit tests for tools/aliro_lab.py against the checked-in sample capture
(tests/host/data/aliro_lab_sample.log) plus synthetic logs that drive every
check's warn/fail branch. Stdlib only; run directly or via tests/host/run.sh."""

import contextlib
import io
import os
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "tools"))

import aliro_lab  # noqa: E402

SAMPLE = os.path.join(ROOT, "tests", "host", "data", "aliro_lab_sample.log")
# A real reader serial capture, present once one is committed. Unlike the
# synthetic sample the suite does not assert exact timings against it (real
# jitter would make that brittle) — only that it is a clean walk-up.
CAPTURE = os.path.join(ROOT, "tests", "host", "data", "aliro_lab_capture.log")


def analyze(text):
    txns = aliro_lab.split_transactions(aliro_lab.parse_events(text))
    return txns, [aliro_lab.run_checks(t) for t in txns]


def status_of(checks, cid):
    for c, _cls, status, _detail in checks:
        if c == cid:
            return status
    raise AssertionError("no check %r" % cid)


def lines(*events):
    """Synthetic capture: each event is (t_us, 'ev[ k=v]')."""
    return "\n".join("[ALAB] t=%d ev=%s" % (t, ev) for t, ev in events) + "\n"


# A minimal complete fast walk-up all checks pass on; tests perturb it.
# Line order mirrors the firmware: session.start opens the walk-up; the
# ph.connect dump line arrives later but carries the earlier timestamp.
GOOD_FAST = [
    (1_002_000, "session.start"),
    (1_000_000, "ph.connect"),
    (1_150_000, "ph.op05"),
    (1_160_000, "ph.auth0"),
    (1_170_000, "flow.fast cred=0"),
    (1_180_000, "ph.auth1"),
    (1_250_000, "ph.exch"),
    (1_255_000, "ph.apc"),
    (1_300_000, "rrx id=1"),
    (1_320_000, "rtx id=2"),
    (1_360_000, "rrx id=3"),
    (1_380_000, "rtx id=4"),
    (1_420_000, "rrx id=5"),
    (1_425_000, "ph.m4"),
    (1_700_000, "ph.range"),
    (1_900_000, "ph.trusted"),
    (1_910_000, "grant.sent"),
    (3_000_000, "ph.bolt"),
    (6_000_000, "session.end"),
]


def without(events, name):
    return [(t, ev) for t, ev in events if not ev.startswith(name)]


def with_extra(events, extra):
    """Insert before the trailing session.end (later lines leave the txn)."""
    return events[:-1] + [extra] + events[-1:]


class SampleLogTest(unittest.TestCase):
    def setUp(self):
        with open(SAMPLE) as f:
            self.txns, self.checks = analyze(f.read())

    def test_two_transactions(self):
        self.assertEqual(len(self.txns), 2)
        self.assertFalse(self.txns[0].open)
        self.assertFalse(self.txns[1].open)

    def test_flows(self):
        self.assertEqual(self.txns[0].flow, "standard")
        self.assertEqual(self.txns[1].flow, "fast")

    def test_phase_offsets(self):
        std, fast = self.txns
        self.assertEqual(std.offset_ms("auth1"), 757.0)
        self.assertEqual(std.offset_ms("bolt"), 3389.0)
        self.assertEqual(fast.offset_ms("auth1"), 203.0)
        self.assertEqual(fast.offset_ms("bolt"), 2601.0)

    def test_all_phases_present(self):
        for txn in self.txns:
            self.assertEqual(sorted(txn.phases), sorted(k for k, _ in aliro_lab.PHASES))

    def test_no_warn_no_fail(self):
        for checks in self.checks:
            for cid, _cls, status, detail in checks:
                self.assertIn(status, ("pass", "n/a"), "%s: %s" % (cid, detail))
        self.assertEqual(aliro_lab.worst_status(self.checks), "pass")

    def test_kp_applies_per_flow(self):
        self.assertEqual(status_of(self.checks[0], "kp"), "pass")
        self.assertEqual(status_of(self.checks[1], "kp"), "n/a")


class CheckBranchTest(unittest.TestCase):
    def one(self, events):
        txns, checks = analyze(lines(*events))
        self.assertEqual(len(txns), 1)
        return txns[0], checks[0]

    def test_good_fast_baseline(self):
        _, checks = self.one(GOOD_FAST)
        for cid, _cls, status, detail in checks:
            self.assertIn(status, ("pass", "n/a"), "%s: %s" % (cid, detail))

    def test_order_violation(self):
        events = [(t, ev) if ev != "ph.exch" else (1_155_000, ev)
                  for t, ev in GOOD_FAST]
        _, checks = self.one(events)
        self.assertEqual(status_of(checks, "order"), "fail")

    def test_duplicate_phase(self):
        _, checks = self.one(with_extra(GOOD_FAST, (3_100_000, "ph.bolt")))
        self.assertEqual(status_of(checks, "once"), "fail")

    def test_both_flows(self):
        _, checks = self.one(with_extra(GOOD_FAST, (1_175_000, "flow.standard")))
        self.assertEqual(status_of(checks, "flow"), "fail")

    def test_auth_done_without_flow(self):
        _, checks = self.one(without(GOOD_FAST, "flow."))
        self.assertEqual(status_of(checks, "flow"), "fail")

    def test_setup_incomplete(self):
        _, checks = self.one(without(GOOD_FAST, "rtx"))
        self.assertEqual(status_of(checks, "setup"), "fail")

    def test_setup_missing_irs(self):
        events = [(t, ev) if ev != "rrx id=1" else (t, "rrx id=9")
                  for t, ev in GOOD_FAST]
        _, checks = self.one(events)
        self.assertEqual(status_of(checks, "setup"), "fail")

    def test_bolt_without_trusted(self):
        _, checks = self.one(without(GOOD_FAST, "ph.trusted"))
        self.assertEqual(status_of(checks, "trusted-bolt"), "fail")

    def test_missing_grant(self):
        _, checks = self.one(without(GOOD_FAST, "grant.sent"))
        self.assertEqual(status_of(checks, "grant"), "warn")

    def test_standard_without_kp(self):
        events = [(t, ev) if not ev.startswith("flow.") else (t, "flow.standard")
                  for t, ev in GOOD_FAST]
        _, checks = self.one(events)
        self.assertEqual(status_of(checks, "kp"), "warn")

    def test_budget_exceeded(self):
        events = [(t, ev) if ev != "ph.auth1" else (1_600_000, ev)
                  for t, ev in GOOD_FAST]
        events = [(t, ev) if ev != "ph.exch" else (1_650_000, ev)
                  for t, ev in events]
        _, checks = self.one(events)
        self.assertEqual(status_of(checks, "budget"), "warn")

    def test_incomplete_walkup(self):
        events = [(t, ev) for t, ev in GOOD_FAST
                  if aliro_lab.PHASE_INDEX.get(ev[3:], 0) <= aliro_lab.PHASE_INDEX["m4"]
                  or not ev.startswith("ph.")]
        events = without(without(events, "grant.sent"), "ph.trusted")
        txn, checks = self.one(events)
        self.assertEqual(status_of(checks, "complete"), "warn")
        self.assertEqual(txn.last_phase(), "m4")

    def test_open_transaction(self):
        txn, checks = self.one(without(GOOD_FAST, "session.end"))
        self.assertTrue(txn.open)
        self.assertEqual(status_of(checks, "complete"), "pass")


class ParsingTest(unittest.TestCase):
    def test_noise_only(self):
        txns, checks = analyze("I (100) boot: hello\nnothing here\n")
        self.assertEqual(txns, [])
        self.assertEqual(aliro_lab.worst_status(checks), "pass")

    def test_events_before_first_start_dropped(self):
        txns, _ = analyze(lines((500, "ph.connect")) + lines(*GOOD_FAST))
        self.assertEqual(len(txns), 1)
        self.assertEqual(txns[0].phases["connect"], 1_000_000)

    def test_attrs_parsed(self):
        txns, _ = analyze(lines(*GOOD_FAST))
        self.assertEqual(txns[0].first("flow.fast").attrs, {"cred": 0})

    def test_alab_embedded_in_prefixed_line(self):
        txns, _ = analyze("I (99) app: [ALAB] t=1000 ev=session.start\n"
                          + lines((2000, "session.end")))
        self.assertEqual(len(txns), 1)


@unittest.skipUnless(os.path.exists(CAPTURE),
                     "no real capture committed yet (see tests/host/data/README.md)")
class CaptureArtifactTest(unittest.TestCase):
    """Activates once a real reader capture is checked in: the committed
    artifact must parse and score clean (a full walk-up, no FAIL check)."""

    def setUp(self):
        with open(CAPTURE) as f:
            self.txns, self.checks = analyze(f.read())

    def test_has_transactions(self):
        self.assertGreaterEqual(len(self.txns), 1,
                                "capture has no [ALAB] transactions")

    def test_no_failing_check(self):
        self.assertNotEqual(aliro_lab.worst_status(self.checks), "fail",
                            "committed capture must be a clean run (no FAIL)")

    def test_reached_bolt(self):
        self.assertTrue(any("bolt" in t.phases for t in self.txns),
                        "capture should be a full walk-up (a bolt was driven)")


class MainTest(unittest.TestCase):
    def run_main(self, log_text):
        with tempfile.TemporaryDirectory() as tmp:
            log = os.path.join(tmp, "capture.log")
            out = os.path.join(tmp, "report.html")
            with open(log, "w") as f:
                f.write(log_text)
            stdout = io.StringIO()
            with contextlib.redirect_stdout(stdout):
                rc = aliro_lab.main(["aliro_lab.py", log, out])
            with open(out) as f:
                html_text = f.read()
            return rc, stdout.getvalue(), html_text

    def test_sample_exit_zero_and_reports(self):
        with open(SAMPLE) as f:
            rc, term, html_text = self.run_main(f.read())
        self.assertEqual(rc, 0)
        self.assertIn("transaction 1: STANDARD flow", term)
        self.assertIn("transaction 2: FAST flow", term)
        self.assertIn("overall: PASS", term)
        self.assertIn("Transaction 2", html_text)
        self.assertIn("PASS", html_text)
        self.assertIn("connect → bolt", html_text)
        self.assertNotIn("FAIL", html_text.replace("✗ FAIL", ""))

    def test_fail_exit_one(self):
        rc, term, html_text = self.run_main(
            lines(*without(GOOD_FAST, "ph.trusted")))
        self.assertEqual(rc, 1)
        self.assertIn("FAIL", term)
        self.assertIn("✗ FAIL", html_text)

    def test_empty_log_exit_zero(self):
        rc, term, _ = self.run_main("just noise\n")
        self.assertEqual(rc, 0)
        self.assertIn("no [ALAB] transactions", term)

    def test_usage_error(self):
        stderr = io.StringIO()
        with contextlib.redirect_stderr(stderr):
            self.assertEqual(aliro_lab.main(["aliro_lab.py"]), 2)
        self.assertIn("Usage", stderr.getvalue())

    def test_missing_file(self):
        stderr = io.StringIO()
        with contextlib.redirect_stderr(stderr):
            rc = aliro_lab.main(["aliro_lab.py", "/nonexistent/x.log", "/tmp/x.html"])
        self.assertEqual(rc, 2)


if __name__ == "__main__":
    unittest.main(verbosity=1)
