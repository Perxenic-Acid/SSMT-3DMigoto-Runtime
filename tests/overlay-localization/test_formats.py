"""翻译不能改变 printf 参数顺序、宽度、长度或类型。"""
import ast
import pathlib
import re
import unittest


class FormatContract(unittest.TestCase):
    def test_all_catalogue_entries_preserve_arguments(self):
        source = (pathlib.Path(__file__).parents[2] / "DirectX11/SSMTOverlayText.cpp").read_text(encoding="utf-8-sig")
        string = r'"(?:[^"\\]|\\.)*"'
        pairs = re.findall(r'TEXT_PAIR\(\s*(' + string + r')\s*,\s*(' + string + r')\s*\)', source)
        self.assertGreater(len(pairs), 50)
        specifier = r'%(?:%|[-+ #0]*(?:\d+|\*)?(?:\.(?:\d+|\*))?(?:hh|ll|I64|I32|I|h|l|j|z|t|L)?[diuoxXfFeEgGaAcCsSpn])'
        for english, chinese in pairs:
            original, translated = ast.literal_eval(english), ast.literal_eval(chinese)
            with self.subTest(message=original):
                self.assertEqual(re.findall(specifier, original), re.findall(specifier, translated))


if __name__ == "__main__":
    unittest.main()
