import unittest
from src.mac_checker import is_special_mac

class TestMacChecker(unittest.TestCase):

    def test_special_mac(self):
        self.assertTrue(is_special_mac("00:1A:2B:00:00:01"))
        self.assertTrue(is_special_mac("00:1A:2C:00:00:02"))
        # Add more tests for valid special MACs
        self.assertTrue(is_special_mac("00:1A:2B:FF:FF:FF"))
        self.assertTrue(is_special_mac("00:1A:2C:AA:BB:CC"))

    def test_non_special_mac(self):
        self.assertFalse(is_special_mac("00:1A:2D:00:00:03"))
        self.assertFalse(is_special_mac("00:1A:2E:00:00:04"))
        # Add more tests for non-special MACs
        self.assertFalse(is_special_mac("00:1B:2B:00:00:01"))
        self.assertFalse(is_special_mac("00:1C:2C:00:00:02"))

if __name__ == '__main__':
    unittest.main()