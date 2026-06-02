#include "ReplView.h"
#include "Welcome.h"
#include "ui/PermissionDialog.h"
#include "util/LruCache.h"
#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/dom/elements.hpp"
#include "core/Json.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <sstream>

namespace aicoder
{

  namespace
  {

    // 代码块内容缓存（存 string，cache hit 时重新构造 Element）
    // key = language + "|" + content，value = codeBlockContent
    LruCache<std::string, std::string> gCodeBlockCache{64};

    // 模型输出行首标记 + 续行对齐缩进（都占 kModelIndentCols 列；"空格不能少"）。
    // 想换成 "* " 只改 kModelMarker 即可，保持 2 列宽就与缩进对齐。
    constexpr const char *kModelMarker = "• ";
    constexpr const char *kModelIndent = "  ";
    constexpr int kModelIndentCols = 2;

    // 输入区高度（行）：夹在上下两条分隔线之间，文字垂直居中，上下留白不紧凑。
    // 用元素自身高度撑开（size + vcenter），不是插空行占位。3 行 = 上下各留 1 行白。
    constexpr int kInputBoxHeight = 3;

    // 思考动画帧（含用户指定的 ⠋⠙⠹⠸…），按挂钟时间循环。
    const char *const kSpinnerFrames[] = {
        "⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
    constexpr int kSpinnerN = 10;
    constexpr long long kSpinnerPeriodMs = 80; // 每帧 80ms ≈ 12.5fps

    // 状态标签颜色：棕色。
    ftxui::Color brownColor() { return ftxui::Color::RGB(170, 110, 40); }

    // 用户消息高亮底色：淡白色（配黑色前景保证可读）。想调深浅改这一处即可。
    ftxui::Color userBgColor() { return ftxui::Color::RGB(230, 230, 230); }

    // 按挂钟时间选当前 spinner 帧（steady_clock 单调，无需记录起点）。
    const char *currentSpinner()
    {
      using namespace std::chrono;
      auto ms =
          duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
      return kSpinnerFrames[(ms / kSpinnerPeriodMs) % kSpinnerN];
    }

    // Simple markdown parser - returns FTXUI Elements
    // Supports: **bold**, *italic*, `code`, ```code blocks```, # headers, > quotes, - lists

    bool isEmptyLine(const std::string &line)
    {
      for (char c : line)
      {
        if (!std::isspace((unsigned char)c))
          return false;
      }
      return true;
    }

    std::string trim(const std::string &s)
    {
      size_t start = 0;
      while (start < s.size() && std::isspace((unsigned char)s[start]))
        start++;
      size_t end = s.size();
      while (end > start && std::isspace((unsigned char)s[end - 1]))
        end--;
      return s.substr(start, end - start);
    }

    bool isCodeBlockStart(const std::string &line)
    {
      return line.find("```") == 0;
    }

    int getHeaderLevel(const std::string &line)
    {
      int level = 0;
      for (size_t i = 0; i < line.size() && line[i] == '#'; i++)
      {
        level++;
      }
      return (level > 0 && level < line.size() && line[level] == ' ') ? level : 0;
    }

    // 检测分割线：--- 或 ***
    bool isHorizontalRule(const std::string &line)
    {
      std::string t = trim(line);
      return t == "---" || t == "***" || t == "___";
    }

    // 检测软换行标记：行末两个空格
    bool hasTrailingSpace(const std::string &line)
    {
      if (line.empty()) return false;
      return line.back() == ' ' && line[line.size() - 2] == ' ';
    }

    // 转义字符处理：\* → *，\- → -，\\ → \ 等
    std::string unescape(const std::string &s)
    {
      std::string r;
      for (size_t i = 0; i < s.size(); ++i)
      {
        if (s[i] == '\\' && i + 1 < s.size())
        {
          char c = s[i + 1];
          if (c == '*' || c == '_' || c == '`' || c == '~' || c == '-' || c == '#' || c == '>' || c == '[' || c == ']' || c == '(' || c == ')' || c == '\\')
          {
            r += s[++i];
            continue;
          }
        }
        r += s[i];
      }
      return r;
    }

    // Wrap text to specified width, respecting word boundaries
    std::vector<std::string> wrapText(const std::string &text, int width)
    {
      std::vector<std::string> lines;
      if (width <= 0 || text.empty())
      {
        if (!text.empty())
          lines.push_back(text);
        return lines;
      }

      std::istringstream stream(text);
      std::string word;
      std::string currentLine;
      int currentWidth = 0;

      while (stream >> word)
      {
        int wordLen = static_cast<int>(word.size());
        if (currentLine.empty())
        {
          if (wordLen <= width)
          {
            currentLine = word;
            currentWidth = wordLen;
          }
          else
          {
            // Word longer than width - split it
            while (wordLen > width)
            {
              lines.push_back(word.substr(0, width));
              word = word.substr(width);
              wordLen = static_cast<int>(word.size());
            }
            currentLine = word;
            currentWidth = wordLen;
          }
        }
        else if (currentWidth + 1 + wordLen <= width)
        {
          currentLine += " " + word;
          currentWidth += 1 + wordLen;
        }
        else
        {
          lines.push_back(currentLine);
          currentLine = word;
          currentWidth = wordLen;
        }
      }
      if (!currentLine.empty())
      {
        lines.push_back(currentLine);
      }
      return lines;
    }

    // 递归解析内联格式化（支持嵌套：**_粗斜体_**）
    ftxui::Element parseInlineElement(const std::string &text)
    {
      ftxui::Elements elements;
      std::string remaining = text;

      while (!remaining.empty())
      {
        // 优先处理最长的匹配：***text*** > **text** > *text*
        // 三重斜体/粗体：***text***
        if (remaining.size() >= 5 && remaining.substr(0, 3) == "***")
        {
          size_t endPos = remaining.find("***", 3);
          if (endPos != std::string::npos)
          {
            if (3 > 0)
              elements.push_back(ftxui::text(remaining.substr(0, 3)));
            std::string innerText = remaining.substr(3, endPos - 3);
            // 内部递归解析，处理 ***text*** 内部的 **text** 或 *text*
            ftxui::Element inner = parseInlineElement(innerText);
            elements.push_back(inner | ftxui::bold | ftxui::italic);
            remaining = remaining.substr(endPos + 3);
            continue;
          }
        }

        // 粗体 **text**
        size_t boldPos = remaining.find("**");
        if (boldPos != std::string::npos)
        {
          size_t boldEnd = remaining.find("**", boldPos + 2);
          if (boldEnd != std::string::npos)
          {
            if (boldPos > 0)
            {
              elements.push_back(ftxui::text(remaining.substr(0, boldPos)));
            }
            std::string boldText = remaining.substr(boldPos + 2, boldEnd - boldPos - 2);
            // 内部递归解析，支持 *_粗斜体_* 嵌套
            ftxui::Element inner = parseInlineElement(boldText);
            elements.push_back(inner | ftxui::bold);
            remaining = remaining.substr(boldEnd + 2);
            continue;
          }
        }

        // 转义字符：\* 等
        if (remaining[0] == '\\')
        {
          char next = remaining[1];
          if (next == '*' || next == '_' || next == '`' || next == '~' || next == '-' ||
              next == '#' || next == '>' || next == '[' || next == ']' || next == '\\')
          {
            elements.push_back(ftxui::text(std::string(1, next)));
            remaining = remaining.substr(2);
            continue;
          }
        }

        // 斜体 *text*（不匹配 ** 或 ***
        if (remaining[0] == '*')
        {
          size_t italicEnd = remaining.find('*', 1);
          if (italicEnd != std::string::npos && italicEnd > 1)
          {
            if (1 > 0)
              elements.push_back(ftxui::text(remaining.substr(0, 1)));
            std::string italicText = remaining.substr(1, italicEnd - 1);
            ftxui::Element inner = parseInlineElement(italicText);
            elements.push_back(inner | ftxui::italic);
            remaining = remaining.substr(italicEnd + 1);
            continue;
          }
        }

        // Inline code: `text` - 使用 permission 颜色（棕色） + dim
        size_t codePos = remaining.find('`');
        if (codePos != std::string::npos)
        {
          size_t codeEnd = remaining.find('`', codePos + 1);
          if (codeEnd != std::string::npos)
          {
            if (codePos > 0)
            {
              elements.push_back(ftxui::text(remaining.substr(0, codePos)));
            }
            std::string codeText = remaining.substr(codePos + 1, codeEnd - codePos - 1);
            elements.push_back(ftxui::text(codeText) | ftxui::color(brownColor()) | ftxui::dim);
            remaining = remaining.substr(codeEnd + 1);
            continue;
          }
        }

        // No more formatting
        if (!remaining.empty())
        {
          elements.push_back(ftxui::text(remaining));
          break;
        }
      }

      if (elements.size() == 1)
      {
        return elements[0];
      }
      return ftxui::hbox(std::move(elements));
    }

    // 从 "```cpp" 行提取语言标签
    std::string extractCodeLang(const std::string &codeBlockStartLine)
    {
      size_t p = codeBlockStartLine.find("```");
      if (p != 0)
        return "";
      size_t pos = 3;
      while (pos < codeBlockStartLine.size() && codeBlockStartLine[pos] == ' ')
        pos++;
      std::string lang = codeBlockStartLine.substr(pos);
      size_t trailing = lang.find("```");
      if (trailing != std::string::npos)
        lang = lang.substr(0, trailing);
      size_t b = lang.find_first_not_of(" \t\r\n");
      size_t e = lang.find_last_not_of(" \t\r\n");
      if (b == std::string::npos)
        return "";
      return lang.substr(b, e - b + 1);
    }

    // 各语言关键字数组（按字母排序）
    static const std::string cpp_kw[] = {
        "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor",
        "bool", "break", "case", "catch", "char", "class", "const", "constexpr",
        "const_cast", "continue", "decltype", "default", "delete", "do", "double",
        "dynamic_cast", "else", "enum", "explicit", "export", "extern", "false",
        "float", "for", "friend", "goto", "if", "inline", "int", "long", "mutable",
        "namespace", "new", "noexcept", "not", "not_eq", "nullptr", "operator",
        "or", "or_eq", "private", "protected", "public", "register", "reinterpret_cast",
        "return", "short", "signed", "sizeof", "static", "static_assert",
        "static_cast", "struct", "switch", "template", "this", "thread_local",
        "throw", "true", "try", "typedef", "typeid", "typename", "union",
        "unsigned", "using", "virtual", "void", "volatile", "wchar_t", "while",
        "override", "final", "concept", "requires"};
    static const std::string python_kw[] = {
        "False", "None", "True", "and", "as", "assert", "async", "await",
        "break", "class", "continue", "def", "del", "elif", "else", "except",
        "finally", "for", "from", "global", "if", "import", "in", "is",
        "lambda", "nonlocal", "not", "or", "pass", "raise", "return", "try",
        "while", "with", "yield", "match", "case"};
    static const std::string js_kw[] = {
        "async", "await", "break", "case", "catch", "class", "const", "continue",
        "debugger", "default", "delete", "do", "else", "export", "extends",
        "false", "finally", "for", "function", "if", "import", "in", "instanceof",
        "let", "new", "null", "return", "static", "super", "switch", "this",
        "throw", "true", "try", "typeof", "var", "void", "while", "with",
        "yield", "of", "from", "as", "get", "set"};
    static const std::string bash_kw[] = {
        "if", "then", "else", "elif", "fi", "for", "do", "done", "while", "until",
        "case", "esac", "in", "function", "return", "exit", "local", "export",
        "readonly", "declare", "typeset", "unset", "shift", "source", "alias",
        "echo", "printf", "read", "eval", "exec", "trap", "true", "false"};

    // C++ 类型关键字
    static const std::string cpp_types[] = {
        "int", "float", "double", "char", "bool", "void", "long", "short",
        "unsigned", "signed", "size_t", "ptrdiff_t", "int8_t", "int16_t", "int32_t", "int64_t",
        "uint8_t", "uint16_t", "uint32_t", "uint64_t", "intptr_t", "uintptr_t",
        "char16_t", "char32_t", "wchar_t", "char8_t", "nullptr_t", "byte"};
    static const std::string cpp_storage[] = {
        "static", "extern", "register", "mutable", "thread_local", "constexpr",
        "const", "volatile", "inline", "auto"};

    // Python 类型
    static const std::string python_types[] = {
        "int", "float", "str", "bool", "list", "dict", "tuple", "set", "frozenset",
        "bytes", "bytearray", "range", "type", "object"};
    static const std::string python_storage[] = {"global", "nonlocal"};

    // JavaScript 类型
    static const std::string js_types[] = {
        "number", "string", "boolean", "object", "symbol", "bigint",
        "Array", "Object", "Function", "Promise", "Map", "Set"};
    static const std::string js_storage[] = {"let", "const", "var", "static"};

    // Bash 类型
    static const std::string bash_types[] = {"int", "float", "string", "array"};
    static const std::string bash_storage[] = {"local", "export", "readonly", "declare", "typeset"};

    // 代码高亮配色主题（支持 dark/light 双主题）
    // 关键：不要在字段默认初始化器里调 ftxui::Color::RGB(...)，否则全局 gTheme 构造时
    // 触发 Terminal::ColorSupport() → Quirks::SetColorSupport()，在 main() 之前 SIGSEGV。
    // 改为函数局部 static（Meyers singleton）：第一次访问时构造,此时 main 已开始,Terminal 已就绪。
    struct CodeTheme {
        ftxui::Color keyword;
        ftxui::Color type;
        ftxui::Color func;
        ftxui::Color storage;
        ftxui::Color string;
        ftxui::Color number;
        ftxui::Color comment;
        ftxui::Color punctuation;
        ftxui::Color background;
        ftxui::Color codeText;

        void applyLight() {
          keyword = ftxui::Color::RGB(0, 0, 255);            // Blue
          type = ftxui::Color::RGB(0, 255, 0);            // Green
          func = ftxui::Color::RGB(150, 100, 0);
          storage = ftxui::Color::RGB(255, 0, 0);           // Red
          string = ftxui::Color::RGB(128, 0, 128);
          number = ftxui::Color::RGB(255, 0, 255);         // Magenta
          comment = ftxui::Color::RGB(130, 130, 130);
          punctuation = ftxui::Color::RGB(80, 80, 80);
          background = ftxui::Color::RGB(245, 245, 245);
          codeText = ftxui::Color::RGB(0, 0, 0);            // Black
        }

        void applyDark() {
          keyword = ftxui::Color::RGB(0, 255, 255);     // Cyan
          type = ftxui::Color::RGB(0, 255, 0);        // Green
          func = ftxui::Color::RGB(255, 255, 0);       // Yellow
          storage = ftxui::Color::RGB(255, 0, 0);       // Red
          string = ftxui::Color::RGB(255, 255, 0);    // Yellow
          number = ftxui::Color::RGB(255, 0, 255);     // Magenta
          comment = ftxui::Color::RGB(100, 100, 100);
          punctuation = ftxui::Color::RGB(180, 180, 180);
          background = ftxui::Color::RGB(30, 30, 30);
          codeText = ftxui::Color::RGB(255, 255, 255); // White
        }
    };

    // Meyers singleton：函数局部 static 变量在第一次访问时构造（线程安全由 C++11 保证）。
    // 这样可以避开 _GLOBAL__sub_I_ 静态初始化时点 — 第一次访问必然发生在 main() 之后。
    CodeTheme &getTheme() {
      static CodeTheme theme = []{
        CodeTheme t;
        t.applyDark();
        return t;
      }();
      return theme;
    }

    enum class KeywordCategory { Keyword, Type, Storage };

    const std::string *keywordsFor(const std::string &lang, size_t *outSize, KeywordCategory cat = KeywordCategory::Keyword)
    {
      if (lang == "cpp" || lang == "c" || lang == "h" || lang == "hpp")
      {
        if (cat == KeywordCategory::Type) { *outSize = sizeof(cpp_types)/sizeof(cpp_types[0]); return cpp_types; }
        if (cat == KeywordCategory::Storage) { *outSize = sizeof(cpp_storage)/sizeof(cpp_storage[0]); return cpp_storage; }
        *outSize = sizeof(cpp_kw)/sizeof(cpp_kw[0]); return cpp_kw;
      }
      if (lang == "python" || lang == "py")
      {
        if (cat == KeywordCategory::Type) { *outSize = sizeof(python_types)/sizeof(python_types[0]); return python_types; }
        if (cat == KeywordCategory::Storage) { *outSize = sizeof(python_storage)/sizeof(python_storage[0]); return python_storage; }
        *outSize = sizeof(python_kw)/sizeof(python_kw[0]); return python_kw;
      }
      if (lang == "javascript" || lang == "js" || lang == "jsx" || lang == "ts" || lang == "tsx")
      {
        if (cat == KeywordCategory::Type) { *outSize = sizeof(js_types)/sizeof(js_types[0]); return js_types; }
        if (cat == KeywordCategory::Storage) { *outSize = sizeof(js_storage)/sizeof(js_storage[0]); return js_storage; }
        *outSize = sizeof(js_kw)/sizeof(js_kw[0]); return js_kw;
      }
      if (lang == "bash" || lang == "sh" || lang == "zsh")
      {
        if (cat == KeywordCategory::Type) { *outSize = sizeof(bash_types)/sizeof(bash_types[0]); return bash_types; }
        if (cat == KeywordCategory::Storage) { *outSize = sizeof(bash_storage)/sizeof(bash_storage[0]); return bash_storage; }
        *outSize = sizeof(bash_kw)/sizeof(bash_kw[0]); return bash_kw;
      }
      *outSize = sizeof(cpp_kw)/sizeof(cpp_kw[0]); return cpp_kw;
    }

    bool isWordMatch(const std::string &rest, const std::string &word) {
      if (rest.size() < word.size()) return false;
      if (rest.compare(0, word.size(), word) != 0) return false;
      if (rest.size() == word.size()) return true;
      char c = rest[word.size()];
      return !std::isalnum(c) && c != '_';
    }

    // 语法高亮：keyword/type/storage 三类分化着色
    ftxui::Element colorizeCodeLine(const std::string &line, const std::string &lang = "")
    {
      ftxui::Elements parts;
      std::string rest = line;
      while (!rest.empty())
      {
        if (rest.size() >= 2 && rest[0] == '/' && rest[1] == '/')
        {
          parts.push_back(ftxui::text(rest) | ftxui::color(getTheme().comment));
          break;
        }
        if (!rest.empty() && rest[0] == '#')
        {
          parts.push_back(ftxui::text(rest) | ftxui::color(getTheme().comment));
          break;
        }
        if (rest[0] == '"' || rest[0] == '\'')
        {
          char q = rest[0];
          auto p = rest.find(q, 1);
          if (p != std::string::npos)
          {
            parts.push_back(ftxui::text(rest.substr(0, p + 1)) | ftxui::color(getTheme().string));
            rest = rest.substr(p + 1);
            continue;
          }
        }
        size_t n = 0;
        while (n < rest.size() && std::isdigit(rest[n]))
          ++n;
        if (n > 0)
        {
          parts.push_back(ftxui::text(rest.substr(0, n)) | ftxui::color(getTheme().number));
          rest = rest.substr(n);
          continue;
        }
        bool matched = false;
        for (size_t cat = static_cast<size_t>(KeywordCategory::Storage); cat <= static_cast<size_t>(KeywordCategory::Keyword); ++cat) {
          size_t kwSize = 0;
          const std::string *kwArr = keywordsFor(lang, &kwSize, static_cast<KeywordCategory>(cat));
          for (size_t i = 0; i < kwSize; ++i)
          {
            const auto &k = kwArr[i];
            if (isWordMatch(rest, k))
            {
              ftxui::Color col = (cat == static_cast<size_t>(KeywordCategory::Storage)) ? getTheme().storage :
                                 (cat == static_cast<size_t>(KeywordCategory::Type)) ? getTheme().type : getTheme().keyword;
              parts.push_back(ftxui::text(k) | ftxui::color(col));
              rest = rest.substr(k.size());
              matched = true;
              break;
            }
          }
          if (matched) break;
        }
        if (!matched)
        {
          parts.push_back(ftxui::text(std::string(1, rest[0])));
          rest = rest.substr(1);
        }
      }
      if (parts.empty())
        parts.push_back(ftxui::text(""));
      return ftxui::hbox(std::move(parts)) | ftxui::flex;
    }

    // Split markdown into lines and render each, wrapping to terminalWidth
    ftxui::Elements renderMarkdownLines(const std::string &markdown, int terminalWidth)
    {
      ftxui::Elements elements;
      if (markdown.empty())
        return elements;
      if (terminalWidth <= 0)
        terminalWidth = 80; // fallback

      std::istringstream stream(markdown);
      std::string line;
      bool inCodeBlock = false;
      std::string codeBlockContent;
      std::string currentLang; // 当前代码块的语言标签

      while (std::getline(stream, line))
      {
        // Code block handling - preserve formatting, don't wrap
        if (isCodeBlockStart(line))
        {
          if (!inCodeBlock)
          {
            inCodeBlock = true;
            codeBlockContent.clear();
            currentLang = extractCodeLang(line); // 提取语言标签
          }
          else
          {
            inCodeBlock = false;
            std::string rawKey = currentLang + "|" + codeBlockContent;
            std::string cacheKey = fnv1aHash(rawKey);
            std::string cachedContent;
            bool cacheHit = gCodeBlockCache.get(cacheKey, &cachedContent);
            if (cacheHit) {
              // 缓存命中：从存储的 content 重新渲染
              cachedContent = codeBlockContent;
            }
            // 渲染（无论命中与否都用 codeBlockContent）
            ftxui::Elements codeLines;
            std::istringstream cs(codeBlockContent);
            std::string cline;
            int lineNum = 1;
            while (std::getline(cs, cline)) {
              std::string numStr = std::to_string(lineNum);
              int pad = 3 - static_cast<int>(numStr.size());
              std::string paddedNum(pad, ' ');
              paddedNum += numStr;
              ftxui::Element numEl = ftxui::text(paddedNum + " ") | ftxui::color(getTheme().comment) | ftxui::size(ftxui::WIDTH, ftxui::EQUAL, 4);
              codeLines.push_back(ftxui::hbox(ftxui::Elements{numEl, colorizeCodeLine(cline, currentLang)}));
              lineNum++;
            }
            elements.push_back(ftxui::text(""));  // 上空行
            elements.push_back(
                ftxui::vbox(std::move(codeLines)) | ftxui::border | ftxui::flex);
            elements.push_back(ftxui::text(""));  // 下空行
            // 缓存 string content（命中时不覆盖，仍用原内容）
            if (!cacheHit) {
              gCodeBlockCache.put(cacheKey, codeBlockContent);
            }
            currentLang.clear();
          }
          continue;
        }

        if (inCodeBlock)
        {
          if (!codeBlockContent.empty())
            codeBlockContent += "\n";
          codeBlockContent += line;
          // elements.push_back(ftxui::paragraph(codeBlockContent) | ftxui::flex);
          continue;
        }

        // Empty line
        if (isEmptyLine(line))
        {
          elements.push_back(ftxui::text(""));
          continue;
        }

        // Horizontal rule: --- or ***
        if (isHorizontalRule(line))
        {
          elements.push_back(ftxui::text(std::string(terminalWidth - 4, '-')) | ftxui::dim);
          continue;
        }

        // Headers
        int headerLevel = getHeaderLevel(line);
        if (headerLevel > 0)
        {
          std::string text = trim(line.substr(headerLevel + 1));
          ftxui::Element e = ftxui::text(text);
          if (headerLevel == 1)
            e = e | ftxui::bold | ftxui::underlined;
          else
            e = e | ftxui::bold;
          elements.push_back(std::move(e));
          continue;
        }

        // Quote line - 使用斜体 italic 样式
        if (!line.empty() && line[0] == '>')
        {
          std::string text = trim(line.substr(1));
          auto wrapped = wrapText(text, terminalWidth - 3);
          for (const auto &w : wrapped)
          {
            auto e = parseInlineElement(w);
            elements.push_back(ftxui::hbox({ftxui::text("│ "), e | ftxui::italic, ftxui::filler()}));
          }
          continue;
        }

        // List item - 支持嵌套深度，每层缩进
        size_t i = 0;
        while (i < line.size() && std::isspace(line[i]))
          i++;
        if (i < line.size() && (line[i] == '-' || line[i] == '*' || line[i] == '+'))
        {
          int indentLevel = 0;
          size_t j = 0;
          while (j < line.size() && std::isspace(line[j])) {
            if (line[j] == '\t') indentLevel += 2;
            else indentLevel += 1;
            j++;
          }
          indentLevel = std::min(indentLevel / 2, 4);
          std::string text = trim(line.substr(j + 1));
          auto wrapped = wrapText(text, terminalWidth - 4 - indentLevel * 2);
          const char* bullets[] = {"•", "◦", "▪", "▸", "✦"};
          std::string indentStr(indentLevel * 2, ' ');
          for (const auto &w : wrapped)
          {
            auto e = parseInlineElement(w);
            elements.push_back(ftxui::hbox({ftxui::text(indentStr + bullets[indentLevel] + " "), e, ftxui::filler()}));
          }
          continue;
        }

        // Regular paragraph - wrap first, then apply inline formatting
        auto wrapped = wrapText(line, terminalWidth);
        for (const auto &w : wrapped)
        {
          auto e = parseInlineElement(w);
          elements.push_back(ftxui::hbox({e, ftxui::filler()}));
        }
      }

      return elements;
    }

  } // anonymous namespace

  class ReplView::Impl
  {
  public:
    ftxui::Component component;
    ftxui::Component input;
    // 底部互斥容器,放 input 和 permission 的 Maybe 包装。
    // 注意:它的子节点在 waitForPermission 时被动态替换。
    ftxui::Component inputArea_;
    ftxui::ScreenInteractive *screen = nullptr;
    std::vector<UIMessage> messages_;
    std::string input_text_;
    bool thinking_ = false;
    std::string stream_text_;
    std::string stream_reasoning_;
    // 合并重绘标志：worker 线程每条增量都想刷屏，但只在没有待处理重绘时才真正投递 Event，
    // 把一段时间内的多条增量折叠成一次 Draw，避免每条增量都全屏重解析（O(n²)）。
    std::atomic<bool> redrawQueued_{false};
    float scroll_pos_ = 1.0f;                                   // 0~1 滚动位置（底部=1）
    int totalLines_ = 0;                                        // 渲染的总行数（右上角标注用）
    int visibleLines_ = 20;                                     // 每屏可视行数（Renderer 里更新，事件处理里用）
    int terminalWidth_ = 80;                                    // default, updated on resize
    ChatMode mode_ = ChatMode::Normal;                          // Tab 键切换
    std::vector<std::pair<std::string, std::string>> commands_; // data-driven slash commands
    int compIndex_ = 0;                                         // 命令补全菜单当前选中项
    int cursorPos_ = 0;                                         // 绑定到 Input 的光标位置，补全后同步到末尾
    std::weak_ptr<Impl> self_;
    SubmitCallback onSubmit_;

    // Permission asking (worker thread waits)
    std::mutex permissionMutex_;
    std::condition_variable permissionCv_;
    bool permissionPending_ = false;
    // 三态选择结果(替代旧 bool)。UI 通过 PermissionDialog 写入。
    PermissionChoice permissionResult_ = PermissionChoice::Deny;
    // 弹窗的请求 + Dialog 实例(TUI 端),由 waitForPermission 投递后构造。
    // Dialog 自身继承 ComponentBase,以 Component 形式持有便于
    // 接入 ftxui Component 树(Maybe / 焦点路由)。
    PermissionRequest permissionRequest_;
    ftxui::Component permissionComponent_;     // Maybe 包装后的(放进 inputArea_)
    ftxui::Component permissionDialogRaw_;     // 原始 dialog(查 Finished/Result 用)
    // 用户选 "Yes, and never ask again" 时回调 —— App 层在此写 PermissionStore。
    ReplView::AllowForeverCallback allowForeverCallback_;
    // 持久化放行查询(命中则不进弹窗)。可选。
    ReplView::AllowLookupCallback allowLookupCallback_;

    static constexpr float kScrollStep = 0.1f;

    Impl()
    {
      ftxui::InputOption input_opt;
      input_opt.on_enter = [this]
      { onEnter(); };
      // 单行输入：Enter 直接提交，不插入换行。FTXUI 默认 multiline=true 会在光标处插入 '\n'
      // 再触发 on_enter —— 配合 Tab 补全留下的过期光标，会把命令切成 "/\nquit" 之类、破坏匹配。
      input_opt.multiline = false;
      // 绑定光标位置：Tab 补全后需把光标同步到末尾（否则后续输入会插到字符串中间）。
      input_opt.cursor_position = &cursorPos_;
      // 输入框无背景色：覆盖默认 transform（默认聚焦时会反色加底），只保留占位符变暗。
      input_opt.transform = [](ftxui::InputState state)
      {
        if (state.is_placeholder)
          state.element |= ftxui::dim;
        return state.element;
      };
      input = ftxui::Input(&input_text_, "input message, enter to send...", input_opt);

      // 底部互斥容器:input 和 permission 的 Maybe 包装作为它的子节点。
      // 平时只 input 可见(permission Maybe=false → 空 Node),pending 时反过来。
      // 子节点在 waitForPermission 时替换为真正 dialog 的 Maybe 包装。
      inputArea_ = ftxui::Container::Vertical({});
      inputArea_->Add(ftxui::Maybe(input, [this] { return !permissionPending_; }));

      auto container = ftxui::Container::Vertical({inputArea_});

      component = ftxui::Renderer(container, [this]
                                  {
      // 一次重绘已开始：放行下一条增量再投递重绘（与 appendDelta 的 exchange 配对）。
      redrawQueued_.store(false);
      // Get current terminal width from screen
      if (screen) {
        int w = screen->dimx();
        if (w > 10) terminalWidth_ = w;
        int h = screen->dimy();
        // 可视行数 ≈ 屏幕总高减去顶部标题/模式行、底部分隔线、输入框、状态栏
        if (h > 12) visibleLines_ = h - 10;
      }

      ftxui::Elements els;
      totalLines_ = 0;  // 重置总行数计数

      // Welcome 屏不在这里 push(原行为:仅 messages_.empty() 时显示,用户敲字时让位)。
      // 改为:welcome 作为 layout 顶层固定项,消息滚动时不被覆盖。

      for (const auto& m : messages_) {
        if (m.is_user) {
          // 用户输出：右对齐，淡白色背景 + 黑色前景，做成醒目"气泡"以区分模型输出。
          els.push_back(ftxui::hbox({
              ftxui::filler(),
              ftxui::text(" " + m.text + " ❯")
                  | ftxui::bgcolor(userBgColor()) | ftxui::color(ftxui::Color::Black)}));
          totalLines_++;
        } else if (m.is_error) {
          els.push_back(ftxui::hbox({ftxui::text("✗ " + m.text) | ftxui::dim, ftxui::filler()}));
          totalLines_++;
        } else if (m.is_thinking) {
          bool hasContent = !stream_reasoning_.empty() || !stream_text_.empty();
          if (!hasContent) {
            // 状态 B（Thinking）：棕色 "Thinking" + 动态 spinner。
            // 此阶段没有数据驱动重绘 —— 靠 RequestAnimationFrame 让 FTXUI 一直在跑的
            // 动画线程持续投递帧（每帧 arm 一次）。一旦有流式内容（进入 Computing）就不再
            // arm，重绘交回 appendDelta 的增量合并，避免长文本被 60fps 反复重解析。
            if (screen) screen->RequestAnimationFrame();
            els.push_back(ftxui::hbox({
                ftxui::text(std::string(currentSpinner()) + " Thinking")
                    | ftxui::color(brownColor()) | ftxui::bold,
                ftxui::filler()}));
            totalLines_++;
          } else {
            // 状态 C（Computing）：棕色 "Computing" 标志 + 流式文本（思维链变暗 + 正文 markdown）。
            // 正文沿用 kModelMarker 行首标记 + 续行缩进，与最终回复一致（结束时无缝替换）。
            els.push_back(ftxui::hbox({
                ftxui::text("Computing") | ftxui::color(brownColor()) | ftxui::bold,
                ftxui::filler()}));
            totalLines_++;
            ftxui::Elements blockLines;
            if (!stream_reasoning_.empty()) {
              std::istringstream rs(stream_reasoning_);
              std::string rline;
              while (std::getline(rs, rline))
                for (const auto& w : wrapText(rline, terminalWidth_ - kModelIndentCols))
                  blockLines.push_back(ftxui::text(w) | ftxui::dim);
            }
            if (!stream_text_.empty())
              for (auto& e : renderMarkdownLines(stream_text_, terminalWidth_ - kModelIndentCols))
                blockLines.push_back(std::move(e));
            for (size_t i = 0; i < blockLines.size(); ++i) {
              els.push_back(ftxui::hbox({
                  ftxui::text(i == 0 ? kModelMarker : kModelIndent),
                  std::move(blockLines[i]), ftxui::filler()}));
              totalLines_++;
            }
          }
        } else if (m.is_tool) {
          // 工具调用行：首行 ⚙ 工具名 + 参数（青色加粗），次行结果摘要（变暗）。
          // text() 不渲染 '\n'，按行拆成 vbox。
          ftxui::Elements toolLines;
          std::istringstream ts(m.text);
          std::string tline;
          bool firstLine = true;
          while (std::getline(ts, tline)) {
            ftxui::Element e = ftxui::text(tline);
            e = firstLine ? (e | ftxui::color(ftxui::Color::Cyan) | ftxui::bold)
                          : (e | ftxui::dim);
            toolLines.push_back(ftxui::hbox({std::move(e), ftxui::filler()}));
            firstLine = false;
          }
          els.push_back(ftxui::vbox(std::move(toolLines)));
          totalLines_++;  // tool 消息算一行
        } else {
          // 模型最终输出：行首 kModelMarker（如 "• "），续行用等宽空格缩进对齐，
          // 与右对齐带 " ❯" 的用户输入区分开。markdown 按「宽度 − 缩进」换行以免溢出。
          auto lines = renderMarkdownLines(m.text, terminalWidth_ - kModelIndentCols);
          for (size_t i = 0; i < lines.size(); ++i) {
            els.push_back(ftxui::hbox({
                ftxui::paragraph(i == 0 ? kModelMarker : kModelIndent),
                std::move(lines[i]), ftxui::filler()}));
            totalLines_++;
          }
        }
      }

      // 统计总行数，供右上角位置标注用
      int nowLine = static_cast<int>(std::round(scroll_pos_ * totalLines_));
      if (totalLines_ == 0) nowLine = 0;

      // Permission 弹窗:不在这里 push。改在 layout 底部与 input 互斥显示
      // (见下方 Maybe 包装)。这里只统计消息区行数,不影响渲染。

      auto messages_area = ftxui::vbox(std::move(els)) |
                           ftxui::focusPositionRelative(0.0f, scroll_pos_) |
                           ftxui::yframe | ftxui::flex;
      ftxui::Elements layout;
      // Welcome 屏:与消息区并列,固定在 layout 顶部,不随消息滚动消失。
      // 输入和输出消息时,welcome 屏保持显示。
      layout.push_back(welcomeScreen() | ftxui::size(ftxui::HEIGHT, ftxui::LESS_THAN, 14));
      layout.push_back(ftxui::separator());
      layout.push_back(messages_area);
      layout.push_back(ftxui::separator() | ftxui::color(ftxui::Color::GrayDark));  // 灰色上分隔线
      // 命令补全菜单：输入以 / 开头时浮现在输入框上方，↑↓ 选择，Tab 补全。
      {
        auto comps = completions();
        if (!comps.empty()) {
          int n = static_cast<int>(comps.size());
          if (compIndex_ >= n) compIndex_ = n - 1;
          if (compIndex_ < 0) compIndex_ = 0;
          ftxui::Elements items;
          for (int i = 0; i < n; ++i) {
            // 名字正常显示，描述变暗，区分主次。
            ftxui::Element name = ftxui::text(" " + comps[i].first + "  ");
            ftxui::Element desc = ftxui::text(comps[i].second + " ") | ftxui::dim;
            ftxui::Element row = ftxui::hbox({name, desc});
            if (i == compIndex_) row = row | ftxui::inverted;  // 高亮当前项
            items.push_back(row);
          }
          layout.push_back(ftxui::vbox(std::move(items)) | ftxui::border);
        }
      }
      // 底部互斥区(input vs permission dialog):
      //   * 极简紧凑:顶线(灰色) + 一行 "> prompt + input",共 2 行。
      //   * permission 期间:由 PermissionDialog 自己的 window 边框负责 UI。
      // 不要在外面套 size(HEIGHT, EQUAL, 3) —— 那样去掉边框后会留
      // 大块空白行。input 组件本身就是单行,直接给它 hbox 即可。
      ftxui::Element bottomEl;
      if (!permissionPending_) {
        bottomEl = ftxui::hbox({
                        ftxui::text("❯ ") | ftxui::color(ftxui::Color::White),
                        inputArea_->Render(),
                    });
      } else {
        // 权限弹窗:由 PermissionDialog 自带的 window 边框呈现。
        // 不要在这里再包任何 border/window/dbox(那会重复套框)。
        bottomEl = inputArea_->Render() | ftxui::xflex;
      }
      layout.push_back(bottomEl);
      layout.push_back(ftxui::separator() | ftxui::color(ftxui::Color::GrayDark));  // 灰色下分隔线
      // 状态提示行：模式标签单独一行、靠左对齐（前置空格留点边距）。下方不再加分隔线，
      // 状态行即一个独立的单行容器，直接紧贴 App 外边框。
      // 反思=绿色加粗、计划=青色加粗、普通=变暗。
      ftxui::Element modeTag;
      if (mode_ == ChatMode::Reflection)
        modeTag = ftxui::text(" [反思] Tab切换") | ftxui::bold |
                  ftxui::color(ftxui::Color::Green);
      else if (mode_ == ChatMode::PlanExecute)
        modeTag = ftxui::text(" [计划] Tab切换") | ftxui::bold |
                  ftxui::color(ftxui::Color::Cyan);
      else
        modeTag = ftxui::text(" [普通] Tab切换") | ftxui::dim;
      std::string posTag = " " + std::to_string(nowLine) + "/" + std::to_string(totalLines_) + " ";
      layout.push_back(ftxui::hbox({ modeTag, ftxui::filler(), ftxui::text(posTag) | ftxui::dim }));  // filler 在右 → 模式靠左、位置靠右
      return ftxui::vbox(std::move(layout)); });

      component = ftxui::CatchEvent(component, [this](ftxui::Event e)
                                    {
      if (permissionPending_ && permissionComponent_) {
        // Modal:全部事件交给 PermissionDialog Component(方向键/Enter/Esc/Tab)。
        // 它"已 finished"则向上 return false 触发通知流程。
        // 通过 Maybe 包装的事件自动转发:permissionComponent_->OnEvent
        // 实际会调到内层 dialog->OnEvent。
        if (permissionComponent_ && permissionComponent_->OnEvent(e)) {
          // OnEvent 已处理 → 检查是否已 finished,若是则回写结果并释放 Component。
          if (auto dlg = std::dynamic_pointer_cast<PermissionDialog>(permissionDialogRaw_)) {
            if (dlg->Finished()) {
              grantPermission(dlg->Result());
              // 释放两个引用 → pending 翻 false → input 重新 Maybe 可见
              permissionComponent_.reset();
              permissionDialogRaw_.reset();
            }
          }
          return true;
        }
        // dialog 没消费(例如鼠标非预期)——Modal 期间不放行到 input
        return true;
      }
      // 命令补全激活时（输入以 / 开头且有候选）：拦截上下/Tab/Enter。
      {
        auto comps = completions();
        if (!comps.empty()) {
          int n = static_cast<int>(comps.size());
          if (compIndex_ >= n) compIndex_ = n - 1;
          if (compIndex_ < 0) compIndex_ = 0;
          if (e == ftxui::Event::ArrowDown) {
            compIndex_ = (compIndex_ + 1) % n;
            return true;
          }
          if (e == ftxui::Event::ArrowUp) {
            compIndex_ = (compIndex_ + n - 1) % n;
            return true;
          }
          if (e == ftxui::Event::Tab) {  // Tab：补全到输入框（不提交）
            input_text_ = comps[compIndex_].first;
            cursorPos_ = static_cast<int>(input_text_.size());  // 光标移到末尾
            compIndex_ = 0;
            return true;
          }
          if (e == ftxui::Event::Return) {  // Enter：补全并直接提交
            input_text_ = comps[compIndex_].first;
            cursorPos_ = static_cast<int>(input_text_.size());
            compIndex_ = 0;
            onEnter();
            return true;
          }
        }
      }
      if (e == ftxui::Event::Tab) {  // Tab 三态循环：普通 → 反思 → 计划 → 普通
        mode_ = (mode_ == ChatMode::Normal)     ? ChatMode::Reflection
                : (mode_ == ChatMode::Reflection) ? ChatMode::PlanExecute
                                                  : ChatMode::Normal;
        return true;
      }
      if (e == ftxui::Event::ArrowUp || e == ftxui::Event::PageUp) {
        scroll_pos_ = std::max(0.0f, scroll_pos_ - kScrollStep);
        return true;
      }
      if (e == ftxui::Event::ArrowDown || e == ftxui::Event::PageDown) {
        scroll_pos_ = std::min(1.0f, scroll_pos_ + kScrollStep);
        return true;
      }
      if (e.is_mouse()) {
        // 只拦截滚轮事件，让文字选择/复制通过
        // 每次滚动约 5 行：FTXUI focusPositionRelative 用 0~1 比率，
        // 约 0.02 对应典型终端约 5 行（总元素约 200 时）
        constexpr float kWheelScroll = 0.02f;
        if (e.mouse().button == ftxui::Mouse::WheelUp) {
          scroll_pos_ = std::max(0.0f, scroll_pos_ - kWheelScroll);
          return true;
        }
        if (e.mouse().button == ftxui::Mouse::WheelDown) {
          scroll_pos_ = std::min(1.0f, scroll_pos_ + kWheelScroll);
          return true;
        }
        return false;  // 其他鼠标事件（选择/复制）放行
      }
      return false; });
    }

    void grantPermission(PermissionChoice choice)
    {
      std::lock_guard<std::mutex> lock(permissionMutex_);
      permissionResult_ = choice;
      // AllowForever 通知到 App 层(写 PermissionStore)
      if (choice == PermissionChoice::AllowForever && allowForeverCallback_)
        allowForeverCallback_(permissionRequest_.tool_name, permissionRequest_.input);
      permissionPending_ = false;
      permissionCv_.notify_one();
      // 关键:通知 worker 之前,先让 TUI 线程把"input 框已恢复"这一帧
      // 渲染完,再让出。避免 worker 立即再投递下一个 dialog 抢占键盘。
      // screen_ 非空(在主会话里)就 Post 一帧重绘事件。
      if (screen)
      {
        screen->PostEvent(ftxui::Event::Custom);
        // 小延迟让 TUI 事件循环跑一次 render。
        // 这一行阻塞 TUI 线程约 30ms,对用户来说几乎无感(Enter 后短暂停滞)
        // 但足以让 input 框可见 1 帧,并丢弃堆积的键盘事件。
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
      }
    }

    // 当前输入对应的命令补全候选；返回空 = 不显示补全菜单。
    // 仅当输入以 '/' 开头、不含空格、且不是某个完整命令时显示。
    std::vector<std::pair<std::string, std::string>> completions() const
    {
      std::vector<std::pair<std::string, std::string>> out;
      if (input_text_.empty() || input_text_[0] != '/')
        return out;
      if (input_text_.find(' ') != std::string::npos)
        return out;
      for (const auto &c : commands_)
      {
        if (c.first == input_text_)
          return {}; // 已是完整命令 → 收起菜单
        if (c.first.rfind(input_text_, 0) == 0)
          out.push_back(c);
      }
      return out;
    }

    void onEnter()
    {
      std::string txt = input_text_;
      std::string cmd = trim(txt);
      // 响应进行中（thinking_）只放行退出命令，其余输入仍拦截（避免与 worker 抢 messages）。
      bool isQuit = (cmd == "/quit" || cmd == "/exit");
      if (thinking_ && !isQuit)
        return;
      if (cmd.empty()) // 空或全空白输入，忽略
      {
        input_text_.clear();
        return;
      }
      input_text_.clear();
      if (onSubmit_)
        onSubmit_(txt);
    }

    // waitForPermission:worker 线程阻塞等待;UI 线程通过 PermissionDialog
    // 拿到 Allow / AllowForever / Deny。返回 bool (Allow 或 AllowForever → true)。
    // 持久化(AllowForever)由 App 层在 grantPermission 内回调,这里只上报 choice。
    bool waitForPermission(const std::string &toolName, const json &input)
    {
      // 1) 静默放行:命中持久化规则,直接返回 true,不打搅用户。
      if (allowLookupCallback_ && allowLookupCallback_(toolName, input))
        return true;

      // 2) 弹窗询问
      PermissionRequest req;
      req.tool_name = toolName;
      req.input = input;
      req.description = input.value("description", std::string{});

      if (screen)
      {
        auto self = self_.lock();
        if (self)
        {
          screen->Post([this, req]() mutable
                       {
          // 投递到 UI 线程:实例化弹窗、置 pending 标志、强制重绘。
          permissionRequest_ = req;
          // 真正弹窗:替换 stub。Container::Tab 的 children 必须稳定(stub_ 槽位),
          // Make 真正弹窗 + 用 Maybe 包装(pending=true 才可见)。
          // 加进 inputArea_ 容器,实现物理空间互斥(input 与 dialog
          // 都进 inputArea_ 的 children,各自 Maybe 控制显隐)。
          permissionDialogRaw_ = ftxui::Make<PermissionDialog>(permissionRequest_);
          permissionComponent_ = ftxui::Maybe(
              permissionDialogRaw_,
              [this] { return permissionPending_; });
          if (inputArea_) {
            inputArea_->DetachAllChildren();
            inputArea_->Add(ftxui::Maybe(this->input, [this] { return !permissionPending_; }));
            inputArea_->Add(permissionComponent_);
          }
          permissionPending_ = true;
          // 让 dialog 拿焦点(沿 parent 链通知 active)
          if (permissionDialogRaw_) permissionDialogRaw_->TakeFocus();
          screen->PostEvent(ftxui::Event::Custom); });
        }
      }

      std::unique_lock<std::mutex> lock(permissionMutex_);
      permissionCv_.wait(lock, [this]
                         { return !permissionPending_; });
      PermissionChoice result = permissionResult_;
      return (result == PermissionChoice::Allow || result == PermissionChoice::AllowForever);
    }
  };

  ReplView::ReplView(SubmitCallback onSubmit) : impl_(std::make_shared<Impl>())
  {
    impl_->self_ = impl_;
    impl_->onSubmit_ = std::move(onSubmit);
  }

  ftxui::Component ReplView::component() { return impl_->component; }

  void ReplView::appendMessage(UIMessage msg)
  {
    if (!impl_->screen)
    {
      impl_->messages_.push_back(std::move(msg));
      return;
    }
    auto self = impl_->self_;
    impl_->screen->Post([self, msg = std::move(msg)]()
                        {
    auto p = self.lock();
    if (!p) return;
    // Remove thinking placeholder if exists
    p->messages_.erase(
        std::remove_if(p->messages_.begin(), p->messages_.end(),
            [](const UIMessage& m) { return m.is_thinking; }),
        p->messages_.end());
    p->messages_.push_back(std::move(msg));
    p->stream_text_.clear();
    p->stream_reasoning_.clear();
    p->thinking_ = false;
    p->scroll_pos_ = 1.0f; });
    impl_->screen->PostEvent(ftxui::Event::Custom); // 令帧失效，否则不重绘（见 appendDelta）
  }

  void ReplView::clearMessages()
  {
    if (!impl_->screen)
    {
      impl_->messages_.clear();
      impl_->stream_text_.clear();
      impl_->stream_reasoning_.clear();
      impl_->thinking_ = false;
      return;
    }
    auto self = impl_->self_;
    impl_->screen->Post([self]()
                        {
    auto p = self.lock();
    if (!p) return;
    p->messages_.clear();
    p->stream_text_.clear();
    p->stream_reasoning_.clear();
    p->thinking_ = false;
    p->scroll_pos_ = 1.0f; });
    impl_->screen->PostEvent(ftxui::Event::Custom); // 令帧失效触发重绘
  }

  void ReplView::appendError(const std::string &msg)
  {
    appendMessage({msg, false, true});
  }

  void ReplView::appendDelta(const std::string &text, const std::string &reasoning)
  {
    if (!impl_->screen)
      return;
    auto self = impl_->self_;
    impl_->screen->Post([self, text, reasoning]()
                        {
    auto p = self.lock();
    if (!p) return;
    p->stream_text_ += text;
    p->stream_reasoning_ += reasoning;
    p->scroll_pos_ = 1.0f; });
    // Post 一个 Closure 只在 UI 线程执行回调，不会令帧失效（frame_valid_ 仍为 true），Draw()
    // 会早退、屏幕不重绘——必须投递一个 Event 把 frame_valid_ 置 false 才会重绘（空 Closure 不行）。
    // 但每条增量都投递会导致 O(n²) 重绘洪泛，所以用 redrawQueued_ 合并：仅当当前没有待处理重绘时
    // 才投递一个 Event；其余增量只累积文本、等这次重绘把标志清掉后再放行下一次。
    if (!impl_->redrawQueued_.exchange(true))
      impl_->screen->PostEvent(ftxui::Event::Custom);
  }

  void ReplView::setThinking(bool v)
  {
    if (!impl_->screen)
    {
      impl_->thinking_ = v;
      return;
    }
    auto self = impl_->self_;
    impl_->screen->Post([self, v]()
                        {
    auto p = self.lock();
    if (p) {
      p->thinking_ = v;
      if (v) {
        // Add placeholder thinking message to trigger render
        p->messages_.push_back(UIMessage{"[thinking...]", false, false, true});
      } else {
        // Remove thinking placeholder if exists
        p->messages_.erase(
            std::remove_if(p->messages_.begin(), p->messages_.end(),
                [](const UIMessage& m) { return m.is_thinking; }),
            p->messages_.end());
      }
    } });
    impl_->screen->PostEvent(ftxui::Event::Custom); // 令帧失效，否则不重绘（见 appendDelta）
  }

  std::vector<UIMessage> ReplView::messages() const { return impl_->messages_; }

  void ReplView::setScreen(ftxui::ScreenInteractive *s) { impl_->screen = s; }

  ChatMode ReplView::mode() const { return impl_->mode_; }

  void ReplView::setCommands(std::vector<std::pair<std::string, std::string>> c)
  {
    impl_->commands_ = std::move(c);
  }

  bool ReplView::askPermission(const std::string &toolName, const json &input)
  {
    return impl_->waitForPermission(toolName, input);
  }

  void ReplView::setAllowForeverCallback(AllowForeverCallback cb)
  {
    impl_->allowForeverCallback_ = std::move(cb);
  }

  void ReplView::setAllowLookupCallback(AllowLookupCallback cb)
  {
    impl_->allowLookupCallback_ = std::move(cb);
  }

} // namespace aicoder