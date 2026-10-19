"""翻译不能改变 printf 参数顺序、宽度、长度或类型。"""
import ast
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).parents[2]
STRING = r'"(?:[^"\\]|\\.)*"'


def catalogue():
    source = (ROOT / "DirectX11/SSMTOverlayText.cpp").read_text(encoding="utf-8-sig")
    return [(ast.literal_eval(english), ast.literal_eval(chinese)) for english, chinese in
            re.findall(r'TEXT_PAIR\(\s*(' + STRING + r')\s*,\s*(' + STRING + r')\s*\)', source)]


def without_comments(source):
    # 保留字符串中的路径、示例代码和换行，仅排除实际 C++ 注释。
    return re.sub(STRING + r'|//[^\n]*|/\*[\s\S]*?\*/',
                  lambda match: match[0] if match[0].startswith('"') else '\n' * match[0].count('\n'), source)


def overlay_formats(filename):
    source = without_comments((ROOT / "DirectX11" / filename).read_text(encoding="utf-8-sig"))
    call = (r'\b(?:IniWarningW?\(\s*|LogOverlayW?\(\s*'
            r'LOG_(?:WARNING(?:_MONOSPACE)?|NOTICE|DIRE|INFO)\s*,\s*)'
            r'((?:L?' + STRING + r'\s*)+)')
    return {''.join(ast.literal_eval(literal) for literal in re.findall(STRING, match[1]))
            for match in re.finditer(call, source)}


class FormatContract(unittest.TestCase):
    def test_all_catalogue_entries_preserve_arguments(self):
        pairs = catalogue()
        self.assertGreater(len(pairs), 50)
        self.assertEqual(len(pairs), len(dict(pairs)), "同一格式串不能有多个翻译")
        specifier = r'%(?:%|[-+ #0]*(?:\d+|\*)?(?:\.(?:\d+|\*))?(?:hh|ll|I64|I32|I|h|l|j|z|t|L)?[diuoxXfFeEgGaAcCsSpn])'
        for original, translated in pairs:
            with self.subTest(message=original):
                self.assertEqual(re.findall(specifier, original), re.findall(specifier, translated))

    def test_config_reload_and_override_errors_are_covered(self):
        translated = dict(catalogue())
        for filename in ["IniHandler.cpp", "Override.cpp"]:
            messages = overlay_formats(filename)
            self.assertTrue(messages, filename)
            with self.subTest(source=filename):
                self.assertEqual(messages - translated.keys(), set(), "重载错误遗漏中文翻译")
        syntax = {message for message in overlay_formats("CommandList.cpp") if message.startswith("Syntax Error: ")}
        self.assertEqual(len(syntax), 1)
        self.assertFalse(syntax - translated.keys(), "表达式错误外层遗漏翻译")

    def test_expression_error_reasons_are_covered(self):
        source = without_comments((ROOT / "DirectX11/CommandList.cpp").read_text(encoding="utf-8-sig"))
        reasons = {ast.literal_eval(literal) for literal in re.findall(
            r'throw\s+CommandListSyntaxError\(L(' + STRING + r')', source)}
        catalogue_source = (ROOT / "DirectX11/SSMTOverlayText.cpp").read_text(encoding="utf-8-sig")
        translated = {ast.literal_eval(literal) for literal in re.findall(
            r'\{\s*L(' + STRING + r'),\s*L' + STRING + r',\s*(?:true|false)\s*\}', catalogue_source)}
        self.assertTrue(reasons)
        self.assertEqual(reasons - translated, set(), "表达式错误原因遗漏翻译")


if __name__ == "__main__":
    unittest.main()
