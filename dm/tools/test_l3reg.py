#!/usr/bin/env python3
import unittest

from l3reg import fit


class L3RegressionTest(unittest.TestCase):
    def test_linear_samples_use_ticks_and_project_total_growth(self):
        rows = [(tick, int(100 + 0.5 * tick), int(200 + 0.25 * tick), 7, 300, 8, 9) for tick in (100, 200, 400)]
        expected_slope = 500
        expected_growth = 500 * (216000 - 400) / 1000 + 300 - 150

        slope, growth = fit(rows, 1, 216000)

        self.assertAlmostEqual(slope, expected_slope, places=6)
        self.assertAlmostEqual(growth, expected_growth, places=6)

    def test_constant_samples_have_zero_slope(self):
        rows = [(tick, 123, 456, 7, 300, 8, 9) for tick in (100, 200, 400)]

        slope, growth = fit(rows, 1, 216000)

        self.assertEqual(slope, 0)
        self.assertEqual(growth, 0)


if __name__ == '__main__':
    unittest.main()
