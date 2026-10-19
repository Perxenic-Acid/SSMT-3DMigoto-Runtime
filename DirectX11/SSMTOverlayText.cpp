#include "SSMTOverlayText.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <locale.h>

namespace SSMTOverlayText {
namespace {
struct Translation {
    const char* english;
    const wchar_t* englishWide;
    const char* chinese;
    const wchar_t* chineseWide;
};
#define TEXT_PAIR(en, zh) { en, L##en, zh, L##zh }
// 与上游格式串精确匹配；技术参数、格式占位符和日志原文保持不变。
const Translation translations[] = {
    TEXT_PAIR("> Reloaded config in %.3fs\n", "> 配置已重载，用时 %.3f 秒\n"),
    TEXT_PAIR("> Wiped user config file d3dx_user.ini\n", "> 已清空用户配置 d3dx_user.ini\n"),
    TEXT_PAIR("> Cleared %d unknown user settings from d3dx_user.ini\n", "> 已从 d3dx_user.ini 清除 %d 项未知设置\n"),
    TEXT_PAIR("> %ls %ls key bindings\n", "> 按键绑定状态：%ls，范围：%ls\n"),
    TEXT_PAIR("> successfully reloaded shaders from ShaderFixes\n", "> 已从 ShaderFixes 重载着色器\n"),
    TEXT_PAIR("> FAILED to reload shaders from ShaderFixes\n", "> 从 ShaderFixes 重载着色器失败\n"),
    TEXT_PAIR("> successfully copied Marked shader to ShaderFixes\n", "> 已将标记的着色器复制到 ShaderFixes\n"),
    TEXT_PAIR("> FAILED to copy Marked shader to ShaderFixes\n", "> 将标记的着色器复制到 ShaderFixes 失败\n"),
    TEXT_PAIR("> HLSL decompilation failed. Falling back to assembly\n", "> HLSL 反编译失败，改用汇编代码\n"),
    TEXT_PAIR("NOTICE: ShaderRegex command lists were dropped from the ShaderOverride\n", "提示：已从 ShaderOverride 中移除 ShaderRegex 命令列表\n"),
    TEXT_PAIR("cached shader found, but lacks a matching .txt file: %ls\n", "找到了着色器缓存，但缺少对应的 .txt 文件：%ls\n"),
    TEXT_PAIR("marked shader file already exists: %ls\n", "标记的着色器文件已存在：%ls\n"),
    TEXT_PAIR("Frame analysis saved to %ls\n", "帧分析已保存到 %ls\n"),
    TEXT_PAIR("Frame Analysis Context is missing: Restart the game with hunting enabled\n", "缺少帧分析上下文：请启用 Hunting 后重新启动游戏\n"),
    TEXT_PAIR("Frame analysis aborted\n", "帧分析已中止\n"),
    TEXT_PAIR("Frame analysis hold mode ended after %i complete frames\n", "帧分析持续模式已结束，共采集 %i 个完整帧\n"),
    TEXT_PAIR("> %s hash %0*llx copied to clipboard\n", "> %s 哈希 %0*llx 已复制到剪贴板\n"),
    TEXT_PAIR("> error copying %s hash %0*llx to clipboard\n", "> 将 %s 哈希 %0*llx 复制到剪贴板失败\n"),
    TEXT_PAIR("  Error opening %S\n", "  打开文件失败：%S\n"),
    TEXT_PAIR("Shader not found: %ls\n", "找不到着色器：%ls\n"),
    TEXT_PAIR("Error compiling custom shader %ls\n", "编译自定义着色器失败：%ls\n"),
    TEXT_PAIR("Failed to load custom buffer resource %ls: %d\n", "加载自定义缓冲区失败：%ls，错误：%d\n"),
    TEXT_PAIR("Out of memory loading %ls\n", "加载 %ls 时内存不足\n"),
    TEXT_PAIR("Error reading custom buffer from file %ls\n", "读取自定义缓冲区文件失败：%ls\n"),
    TEXT_PAIR("Failed to load custom texture resource %ls: 0x%x\n", "加载自定义贴图失败：%ls，错误：0x%x\n"),
    TEXT_PAIR("Ignoring cyclic command list reference `%ls` <=> `%ls`\n", "忽略命令列表的循环引用：`%ls` <=> `%ls`\n"),
    TEXT_PAIR("WARNING: Unknown option: %ls\n", "警告：未知选项：%ls\n"),
    TEXT_PAIR("WARNING: UNABLE TO PARSE KEY BINDING %ls=%ls\n", "警告：无法解析按键绑定 %ls=%ls\n"),
    TEXT_PAIR("3DMigoto does not support DirectX 12\nPlease set the game to use DirectX 11\n", "3DMigoto 不支持 DirectX 12\n请将游戏设置为使用 DirectX 11\n"),
    TEXT_PAIR("NOTICE: %d requested IniParams exceeds the recommended %d\n", "提示：请求的 IniParams 数量 %d 超过建议值 %d\n"),
    TEXT_PAIR("Duplicate non-command list key found: %ls\n - [%ls]\n", "发现重复的非命令列表键：%ls\n - [%ls]\n"),
    TEXT_PAIR("Duplicate section found\n - [%ls]\n", "发现重复配置节\n - [%ls]\n"),
    TEXT_PAIR("Duplicate key found: %ls\n - [%ls] @ [%ls]\n", "发现重复配置键：%ls\n - [%ls] @ [%ls]\n"),
    TEXT_PAIR("Unknown section type\n - [%ls] @ [%ls]\n", "未知配置节类型\n - [%ls] @ [%ls]\n"),
    TEXT_PAIR("Entry outside of section: %ls\n - [%ls]\n", "配置项不在任何配置节内：%ls\n - [%ls]\n"),
    TEXT_PAIR("No value found for: \"%ls\"\n - [%ls] @ [%ls]\n", "配置项缺少值：\"%ls\"\n - [%ls] @ [%ls]\n"),
    TEXT_PAIR("Malformed line: \"%ls\"\n - [%ls] @ [%ls]\n", "配置行格式错误：\"%ls\"\n - [%ls] @ [%ls]\n"),
    TEXT_PAIR("\"%ls=%ls\" too long\n - [%ls]\n", "配置项 \"%ls=%ls\" 过长\n - [%ls]\n"),
    TEXT_PAIR("File included multiple times: %ls\n - [%ls]\n", "文件被重复包含：%ls\n - [%ls]\n"),
    TEXT_PAIR("Unable to parse include condition: %ls\n - [%ls]\n", "无法解析文件包含条件：%ls\n - [%ls]\n"),
    TEXT_PAIR("Include condition could not be statically evaluated: %ls\n - [%ls]\n", "文件包含条件无法静态求值：%ls\n - [%ls]\n"),
    TEXT_PAIR("Unrecognised entry: %ls=%ls\n - [%ls] @ [%ls]\n", "无法识别的配置项：%ls=%ls\n - [%ls] @ [%ls]\n"),
    TEXT_PAIR("Unrecognised entry: %ls\n - [%ls] @ [%ls]\n", "无法识别的配置项：%ls\n - [%ls] @ [%ls]\n"),
    TEXT_PAIR("Unable to parse %ls expression for \"%ls\": \"%ls\"\n - [%ls] @ [%ls]\n", "无法解析 %ls 表达式，配置项 \"%ls\"：\"%ls\"\n - [%ls] @ [%ls]\n"),
    TEXT_PAIR("%ls expression for \"%ls\" cannot be statically evaluated: \"%ls\"\n - [%ls] @ [%ls]\n", "%ls 表达式无法静态求值，配置项 \"%ls\"：\"%ls\"\n - [%ls] @ [%ls]\n"),
    TEXT_PAIR("Expression result \"%f\" for \"%ls\" %ls conversion is invalid: \"%ls\"\n - [%ls] @ [%ls]\n", "表达式结果 \"%f\" 无法转换为配置项 \"%ls\" 所需的 %ls 类型：\"%ls\"\n - [%ls] @ [%ls]\n"),
    TEXT_PAIR("Unable to parse %ls value for \"%ls\" from empty string\n - [%ls] @ [%ls]\n", "无法从空字符串解析 %ls 值，配置项 \"%ls\"\n - [%ls] @ [%ls]\n"),
    TEXT_PAIR("Hash parse error: %ls=%S\n - [%ls] @ [%ls]\n", "哈希解析错误：%ls=%S\n - [%ls] @ [%ls]\n"),
    TEXT_PAIR("Hex string parse error: %ls=%S\n - [%ls] @ [%ls]\n", "十六进制字符串解析错误：%ls=%S\n - [%ls] @ [%ls]\n"),
    TEXT_PAIR("Unrecognised Enum: %ls=%ls\n - [%ls]\n", "无法识别的枚举值：%ls=%ls\n - [%ls]\n"),
    TEXT_PAIR("Unknown Enum: %ls=%ls\n - [%ls]\n", "未知枚举值：%ls=%ls\n - [%ls]\n"),
    TEXT_PAIR("Unknown Enum: %ls=%S\n - [%ls]\n", "未知枚举值：%ls=%S\n - [%ls]\n"),
    TEXT_PAIR("Missing Key=\n - [%ls]\n", "缺少 Key= 按键设置\n - [%ls]\n"),
    TEXT_PAIR("String-to-typed-array parse error: %S\n", "字符串转类型数组时解析失败：%S\n"),
    TEXT_PAIR("ERROR allocating initial data\n", "分配初始数据内存失败\n"),
    TEXT_PAIR("Special value unsupported as normalized integer: %f\n", "特殊值不能用作归一化整数：%f\n"),
    TEXT_PAIR("Value out of [-1, +1] range: %f\n", "数值超出 [-1, +1] 范围：%f\n"),
    TEXT_PAIR("Value out of [0, +1] range: %f\n", "数值超出 [0, +1] 范围：%f\n"),
    TEXT_PAIR("Unsupported format for specifying initial data as text\n", "此格式不支持以文本指定初始数据\n"),
    TEXT_PAIR("Initial data currently only supported on buffers\n - [%ls]\n", "目前仅缓冲区支持初始数据\n - [%ls]\n"),
    TEXT_PAIR("Initial data and filename cannot be used together\n - [%ls]\n", "不能同时指定初始数据和 filename\n - [%ls]\n"),
    TEXT_PAIR("Unsupported format %S for specifying initial data\n - [%ls]\n", "格式 %S 不支持指定初始数据\n - [%ls]\n"),
    TEXT_PAIR("Specified spatial radius \"%d\" is below minimum \"1\".\n - [%ls]\n", "指定的空间半径 \"%d\" 小于最小值 \"1\"\n - [%ls]\n"),
    TEXT_PAIR("Pool variables persistence is not supported for \"%ls\" index type (\"ring\" index only feature).\n - [%ls]\n", "索引类型 \"%ls\" 不支持持久化池变量（仅 \"ring\" 索引支持）\n - [%ls]\n"),
    TEXT_PAIR("Scope unbalanced\n - [%ls]\n", "作用域不匹配\n - [%ls]\n"),
    TEXT_PAIR("Illegal Global Variable Name: \"%ls\"\n - [Constants] @ [%ls]\n", "全局变量名无效：\"%ls\"\n - [Constants] @ [%ls]\n"),
    TEXT_PAIR("Redeclaration of %ls\n - [Constants] @ [%ls]\n", "重复声明变量 %ls\n - [Constants] @ [%ls]\n"),
    TEXT_PAIR("Section missing Hash=\n - [%ls]\n", "配置节缺少 Hash=\n - [%ls]\n"),
    TEXT_PAIR("Section missing Hash= or valid match options\n - [%ls]\n", "配置节缺少 Hash= 或有效的匹配选项\n - [%ls]\n"),
    TEXT_PAIR("Cannot use hash= and match options together!\n - [%ls]\n", "不能同时使用 hash= 和匹配选项\n - [%ls]\n"),
    TEXT_PAIR("Section can never match any resources\n - [%ls]\n", "此配置节无法匹配任何资源\n - [%ls]\n"),
    TEXT_PAIR("Unknown format \"%ls\"\n - [%ls]\n", "未知格式 \"%ls\"\n - [%ls]\n"),
    TEXT_PAIR("Unknown format \"%ls\"\n", "未知格式 \"%ls\"\n"),
    TEXT_PAIR("Unrecognised %ls=%ls\n", "无法识别 %ls=%ls\n"),
    TEXT_PAIR("Invalid flag %ls\n", "无效标志 %ls\n"),
    TEXT_PAIR("Duplicate flag %ls\n", "重复标志 %ls\n"),
    TEXT_PAIR("Missing section\n - [%ls]\n", "缺少配置节\n - [%ls]\n"),
    TEXT_PAIR("Missing corresponding pattern section\n - [%ls]\n", "缺少对应的模式配置节\n - [%ls]\n"),
    TEXT_PAIR("RegEx section missing shader_model\n - [%ls]\n", "RegEx 配置节缺少 shader_model\n - [%ls]\n"),
    TEXT_PAIR("Named capture group overlaps with temp regs!\n - [%ls]\n", "命名捕获组与临时寄存器重叠\n - [%ls]\n"),
    TEXT_PAIR("Disabling entire shader regex group\n - [%ls]\n", "已禁用整个着色器正则组\n - [%ls]\n"),
    TEXT_PAIR("Cannot resolve resource path\n - [%ls]\n", "无法解析资源路径\n - [%ls]\n"),
    TEXT_PAIR("Statement \"endif\" missing \"if\"\n - [%ls] @ [%ls]\n", "endif 缺少对应的 if\n - [%ls] @ [%ls]\n"),
    TEXT_PAIR("Statement \"if\" missing \"endif\":\n - \"%ls\"\n", "if 缺少对应的 endif：\n - \"%ls\"\n"),
    TEXT_PAIR("Command not terminated\n - [%ls]\n", "命令未结束\n - [%ls]\n"),
    TEXT_PAIR("Unable to parse expression - must be in the simple form:\n    [ operator ] value | field_name [ * field_name ] [ * multiplier ] [ / divider ]\n    Parse error on text: \"%ls\"\n", "无法解析表达式，必须使用以下简单格式：\n    [ operator ] value | field_name [ * field_name ] [ * multiplier ] [ / divider ]\n    解析失败的文本：\"%ls\"\n"),
    TEXT_PAIR("Denominator is zero: %ls\n", "分母为零：%ls\n"),
    TEXT_PAIR("Failed to detect stride for override_vertex_count=%d, please set override_byte_stride!\n - [%ls]\n", "无法确定 override_vertex_count=%d 的步长，请设置 override_byte_stride\n - [%ls]\n"),
    TEXT_PAIR("Unrecognised blend operation %ls\n", "无法识别的混合运算：%ls\n"),
    TEXT_PAIR("Unrecognised blend source factor %ls\n", "无法识别的混合源因子：%ls\n"),
    TEXT_PAIR("Unrecognised blend destination factor %ls\n", "无法识别的混合目标因子：%ls\n"),
    TEXT_PAIR("Unrecognised stencil function %ls\n", "无法识别的模板比较函数：%ls\n"),
    TEXT_PAIR("Unrecognised stencil + depth pass operation %ls\n", "无法识别的模板与深度均通过时的操作：%ls\n"),
    TEXT_PAIR("Unrecognised stencil pass / depth fail operation %ls\n", "无法识别的模板通过、深度失败时的操作：%ls\n"),
    TEXT_PAIR("Unrecognised stencil fail operation %ls\n", "无法识别的模板测试失败时的操作：%ls\n"),
    TEXT_PAIR("Unrecognised primitive topology=%ls\n - [%ls]\n", "无法识别的图元拓扑=%ls\n - [%ls]\n"),
    TEXT_PAIR("Unknown sampler \"%ls\"\n - [%ls]\n", "未知采样器 \"%ls\"\n - [%ls]\n"),
    TEXT_PAIR("Option ini_params=%i out of range\n - [%ls]\n", "选项 ini_params=%i 超出范围\n - [%ls]\n"),
    TEXT_PAIR("Invalid condition = \"%ls\"\n - [%ls] @ [%ls]\n", "无效条件 = \"%ls\"\n - [%ls] @ [%ls]\n"),
    TEXT_PAIR("Invalid condition=\"%S\"\n - [%ls] @ [%ls]\n", "无效条件=\"%S\"\n - [%ls] @ [%ls]\n"),
    TEXT_PAIR("Invalid run=\"%ls\"\n - [%ls] @ [%ls]\n", "无效的 run=\"%ls\"\n - [%ls] @ [%ls]\n"),
    TEXT_PAIR("Invalid run=\"%S\"\n - [%ls] @ [%ls]\n", "无效的 run=\"%S\"\n - [%ls] @ [%ls]\n"),
    TEXT_PAIR("Unmatched value \"%S\"\n", "无法匹配的值 \"%S\"\n"),
    TEXT_PAIR("Syntax Error: %S\n              %*s: %S\n", "语法错误：%S\n          %*s: %S\n"),
    TEXT_PAIR("force_full_screen_on_key: Unable to find swap chain\n", "force_full_screen_on_key：找不到交换链\n"),
    TEXT_PAIR("BUG: No HackerContext at ReloadConfig - please report this\n", "内部错误：ReloadConfig 时缺少 HackerContext，请报告此问题\n"),
    TEXT_PAIR("BUG: Unexpected error inserting fuzzy texture override\n", "内部错误：插入模糊匹配贴图替换规则时出现异常\n"),
    TEXT_PAIR("Deprecated hook options: Please remove \"except\" and \"skip\" options\n", "Hook 选项已弃用，请移除 \"except\" 和 \"skip\" 选项\n"),
    TEXT_PAIR("NOTICE: [%S] used deprecated depth_filter option. Consider texture filtering for more flexibility:\n\n[%S]\nx = oD\n\nIn the shader:\nif (asint(IniParams[0].x) == asint(-0.0)) {\n    // No depth buffer bound\n} else {\n    // Depth buffer bound\n}\n\nOr in assembly:\ndcl_resource_texture1d (float,float,float,float) t120\nld_indexable(texture1d)(float,float,float,float) r0.x, l(0, 0, 0, 0), t120.xyzw\nieq r0.x, r0.x, l(0x80000000)\nif_nz r0.x\n    // No depth buffer bound\nelse\n    // Depth buffer bound\nendif\n", "提示：[%S] 使用了已弃用的 depth_filter，可改用更灵活的贴图过滤：\n\n[%S]\nx = oD\n\n在着色器中：\nif (asint(IniParams[0].x) == asint(-0.0)) {\n    // No depth buffer bound\n} else {\n    // Depth buffer bound\n}\n\n或使用汇编：\ndcl_resource_texture1d (float,float,float,float) t120\nld_indexable(texture1d)(float,float,float,float) r0.x, l(0, 0, 0, 0), t120.xyzw\nieq r0.x, r0.x, l(0x80000000)\nif_nz r0.x\n    // No depth buffer bound\nelse\n    // Depth buffer bound\nendif\n"),
    TEXT_PAIR("Possible Mod Conflict: Duplicate ShaderOverride hash=%16llx\n[%ls]\n[%ls]\nIf this is intentional, add allow_duplicate_hash=true or allow_duplicate_hash=overrule to suppress warning\n",
        "可能存在 Mod 冲突：重复的 ShaderOverride hash=%16llx\n[%ls]\n[%ls]\n若为有意重复，可添加 allow_duplicate_hash=true 或 allow_duplicate_hash=overrule 关闭警告\n"),
    TEXT_PAIR("Possible Mod Conflict: Duplicate TextureOverride hash=%08lx\n[%ls]\n[%ls]\nIf this is intentional, add a match_priority=n to suppress warning and disambiguate order\n",
        "可能存在 Mod 冲突：重复的 TextureOverride hash=%08lx\n[%ls]\n[%ls]\n若为有意重复，可添加 match_priority=n 关闭警告并明确匹配顺序\n"),
    TEXT_PAIR("Detected a conflicting d3dx.ini in the game directory that is not being used.\nUsing this configuration: %S\n",
        "游戏目录中存在冲突的 d3dx.ini，该文件未被使用\n当前使用的配置：%S\n"),
    TEXT_PAIR("NOTICE: Detected %d unknown user settings in d3dx_user.ini\n This is normal if you removed/changed any mods\n%ls Press %ls to reload the config now, or %ls to reset all settings to default\n The first unrecognised entry was: \"%ls = %f\"\n",
        "提示：d3dx_user.ini 中发现 %d 项未知用户设置\n 移除或更换 Mod 后出现此提示属于正常情况\n%ls 按 %ls 立即重载配置，或按 %ls 将所有设置恢复默认\n 第一项未知设置：\"%ls = %f\"\n"),
};
#undef TEXT_PAIR

struct SyntaxReason {
    const wchar_t* english;
    const wchar_t* chinese;
    bool prefix;
};
// 仅翻译解析器已知的原因，前缀后的变量名和表达式原样保留。
const SyntaxReason syntaxReasons[] = {
    { L"Unterminated bracket expression", L"括号表达式未结束", false },
    { L"Float not recognized: ", L"无法识别浮点数：", true },
    { L"Variable not recognized: ", L"无法识别变量：", true },
    { L"Unrecognised identifier: ", L"无法识别标识符：", true },
    { L"Unexpected identifier", L"意外的标识符", false },
    { L"Unmatched )", L"右括号 ) 缺少匹配项", false },
    { L"Unmatched (", L"左括号 ( 缺少匹配项", false },
    { L"Expression inside parentheses must not be empty", L"括号内的表达式不能为空", false },
    { L"FIXME: Implement left-associative unary operators", L"尚未实现左结合的一元运算符", false },
    { L"BUG", L"内部解析错误", false },
    { L"BUG: LHS operand invalid", L"内部错误：左操作数无效", false },
    { L"BUG: RHS operand invalid", L"内部错误：右操作数无效", false },
    { L"Empty expression", L"表达式为空", false },
    { L"Unexpected", L"意外的表达式内容", false },
    { L"Non-evaluatable", L"表达式无法求值", false },
};

std::atomic<bool> chinese{ PRIMARYLANGID(GetUserDefaultUILanguage()) == LANG_CHINESE &&
    (SUBLANGID(GetUserDefaultUILanguage()) == SUBLANG_CHINESE_SIMPLIFIED ||
     SUBLANGID(GetUserDefaultUILanguage()) == SUBLANG_CHINESE_SINGAPORE) };

struct Utf8Locale {
    _locale_t value = _create_locale(LC_ALL, ".UTF-8");
    ~Utf8Locale() { if (value) _free_locale(value); }
};

_locale_t Locale() { static Utf8Locale locale; return locale.value; }

std::wstring Decode(const char* text)
{
    int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, nullptr, 0);
    UINT codePage = CP_UTF8;
    if (!count) { codePage = CP_ACP; count = MultiByteToWideChar(codePage, 0, text, -1, nullptr, 0); }
    if (!count) return L"";
    std::wstring result(count, L'\0');
    MultiByteToWideChar(codePage, 0, text, -1, &result[0], count);
    result.resize(count - 1);
    return result;
}

std::wstring Render(const char* format, va_list args)
{
    char buffer[4096] = {};
    va_list copy;
    va_copy(copy, args);
    _vsnprintf_s_l(buffer, sizeof(buffer), _TRUNCATE, format, Locale(), copy);
    va_end(copy);
    return Decode(buffer);
}

std::wstring Render(const wchar_t* format, va_list args)
{
    wchar_t buffer[1024] = {};
    va_list copy;
    va_copy(copy, args);
    _vsnwprintf_s_l(buffer, _countof(buffer), _TRUNCATE, format, Locale(), copy);
    va_end(copy);
    return buffer;
}

void TranslateCleanup(std::wstring& text)
{
    const wchar_t* english[] = {
        L" Unknown settings clean-up will remove them from d3dx_user.ini on next config reload\n",
        L" To disable unknown settings clean-up, set \"clear_unknown_settings = 0\" inside d3dx.ini and restart the game\n",
        L" Unknown settings clean-up is disabled, so they will stay in d3dx_user.ini\n",
        L" To enable unknown settings clean-up, set \"clear_unknown_settings = 1\" inside d3dx.ini and restart the game\n"
    };
    const wchar_t* chinese[] = {
        L" 下次重载配置时将从 d3dx_user.ini 移除未知设置\n",
        L" 如需关闭自动清理，请在 d3dx.ini 中设置 clear_unknown_settings = 0 并重启游戏\n",
        L" 自动清理已关闭，未知设置将保留在 d3dx_user.ini 中\n",
        L" 如需开启自动清理，请在 d3dx.ini 中设置 clear_unknown_settings = 1 并重启游戏\n"
    };
    for (size_t i = 0; i < _countof(english); ++i) {
        auto position = text.find(english[i]);
        if (position != std::wstring::npos) text.replace(position, wcslen(english[i]), chinese[i]);
    }
}

void TranslateArguments(const Translation& entry, std::wstring& text)
{
    if (wcsstr(entry.englishWide, L"NOTICE: Detected %d unknown user settings")) {
        TranslateCleanup(text);
    } else if (!strcmp(entry.english, "Syntax Error: %S\n              %*s: %S\n")) {
        // 只处理引擎语法错误第二行的原因，不替换第一行的 Mod 表达式。
        auto line = text.find(L'\n');
        if (line == std::wstring::npos) return;
        auto caret = text.find(L"^: ", line);
        if (caret == std::wstring::npos) return;
        auto start = caret + 3;
        for (const auto& reason : syntaxReasons) {
            auto length = wcslen(reason.english);
            if (!text.compare(start, length, reason.english) &&
                (reason.prefix || !text.compare(start + length, std::wstring::npos, L"\n"))) {
                text.replace(start, length, reason.chinese);
                break;
            }
        }
    }
}
}

bool Configure(const wchar_t* language, LANGID systemLanguage)
{
    bool enabled = false;
    if (!_wcsicmp(language, L"auto")) {
        enabled = PRIMARYLANGID(systemLanguage) == LANG_CHINESE &&
            (SUBLANGID(systemLanguage) == SUBLANG_CHINESE_SIMPLIFIED ||
             SUBLANGID(systemLanguage) == SUBLANG_CHINESE_SINGAPORE);
    } else if (!_wcsicmp(language, L"zh_s") || !_wcsicmp(language, L"zh-CN") || !_wcsicmp(language, L"zh-Hans")) {
        enabled = true;
    } else if (_wcsicmp(language, L"en") && _wcsicmp(language, L"en-US")) {
        return false;
    }
    chinese.store(enabled, std::memory_order_relaxed);
    return true;
}

bool UseChinese() { return chinese.load(std::memory_order_relaxed); }

Message Format(const char* format, va_list args)
{
    Message result{ Render(format, args), L"" };
    for (const auto& entry : translations) {
        if (!strcmp(format, entry.english)) {
            result.chinese = Render(entry.chinese, args);
            TranslateArguments(entry, result.chinese);
            break;
        }
    }
    return result;
}

Message Format(const wchar_t* format, va_list args)
{
    Message result{ Render(format, args), L"" };
    for (const auto& entry : translations) {
        if (!wcscmp(format, entry.englishWide)) {
            result.chinese = Render(entry.chineseWide, args);
            TranslateArguments(entry, result.chinese);
            break;
        }
    }
    return result;
}

const wchar_t* HuntingFormat(bool chinese)
{
    return chinese ? L"着色器 Hunting 模式（标记方式：%ls）" : L"Shader Hunting Mode (marking: %ls)";
}

std::wstring FontCharacters()
{
    std::wstring text = HuntingFormat(true);
    for (wchar_t c = 32; c < 127; ++c) text += c;
    for (const auto& entry : translations) text += entry.chineseWide;
    for (const auto& reason : syntaxReasons) text += reason.chinese;
    return text;
}
}
