#!/usr/bin/env python3
"""Synthetic capacity model: original exclusive CPU6/7 vs shared PRIME affinity.

This is an *indicator* of frame pacing risk, not a claim of Android FPS gain.
Both render threads can execute on either PRIME CPU, but a single thread
cannot be active on two CPUs simultaneously.
"""
import unittest

def fixed_loads(draw, command, background6, background7):
    return background6 + draw, background7 + command

def shared_minimax(draw, command, background6, background7):
    # With preemptive migration, both foreground tasks can be split over
    # time while neither executes on two cores simultaneously.
    total = draw + command + background6 + background7
    return max(total / 2.0, background6, background7)

class PrimeBalanceSynthetic(unittest.TestCase):
    def test_skew_from_log_workers_30_to_50_percent(self):
        # Representative normalized workload, not measured TLOU parameters.
        draw, command = 0.64, 0.42
        base6, base7 = 0.40, 0.32
        cpu6, cpu7 = fixed_loads(draw, command, base6, base7)
        flexible = shared_minimax(draw, command, base6, base7)
        self.assertGreater(cpu6, 1.0)
        self.assertLess(cpu7, 1.0)
        self.assertLess(flexible, 1.0)
        print("SYNTHETIC fractional core demand: exclusive CPU6=%.2f CPU7=%.2f; "
              "shared PRIME balanced=%.2f max; Android FPS not predicted" %
              (cpu6, cpu7, flexible))

    def test_no_oversubscription_regime(self):
        cpu6, cpu7 = fixed_loads(0.3, 0.3, 0.2, 0.2)
        flexible = shared_minimax(0.3, 0.3, 0.2, 0.2)
        self.assertLessEqual(max(cpu6,cpu7),1.0)
        self.assertLessEqual(flexible,max(cpu6,cpu7))

if __name__ == "__main__":
    unittest.main()
