import unittest
from src.mac_checker import is_special_mac

class TestMacChecker(unittest.TestCase):

    def test_special_mac(self):
        self.assertTrue(is_special_mac("00:1A:2B:00:00:01"))
        self.assertTrue(is_special_mac("00:1A:2C:00:00:02"))
        self.assertTrue(is_special_mac("00:1A:2B:FF:FF:FF"))
        self.assertTrue(is_special_mac("00:1A:2C:AA:BB:CC"))
        self.assertTrue(is_special_mac("00:1A:2B:CC:DD:EE"))
        self.assertTrue(is_special_mac("00:1A:2C:11:22:33"))

    def test_non_special_mac(self):
        self.assertFalse(is_special_mac("00:1A:2D:00:00:03"))
        self.assertFalse(is_special_mac("00:1A:2E:00:00:04"))
        self.assertFalse(is_special_mac("00:1B:2B:00:00:01"))
        self.assertFalse(is_special_mac("00:1C:2C:00:00:02"))
        self.assertFalse(is_special_mac("00:1D:2E:00:00:04"))
        self.assertFalse(is_special_mac("00:1E:2F:00:00:05"))
        self.assertFalse(is_special_mac("00:1F:3A:00:00:06"))

if __name__ == '__main__':
    unittest.main()