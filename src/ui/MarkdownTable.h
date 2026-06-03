#pragma once
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>
#include <ftxui/dom/elements.hpp>

namespace aicoder {

enum class TableAlign { Left, Center, Right };

struct ParsedTable {
  std::vector<std::string> headers;
  std::vector<TableAlign> alignments;
  std::vector<std::vector<std::string>> data;
};

using TextWrapper = std::function<std::vector<std::string>(const std::string&, int)>;
using InlineFormatter = std::function<ftxui::Element(const std::string&)>;

bool isTableRow(const std::string& line);
bool isTableSeparator(const std::string& line);
std::vector<std::string> parseTableRow(const std::string& line);
std::vector<TableAlign> parseAlignment(const std::string& line);
std::optional<std::pair<ParsedTable, size_t>> parseTableBlock(
    const std::vector<std::string>& lines, size_t startIndex);
ftxui::Element renderTable(const ParsedTable& table, int termWidth,
                           TextWrapper wrapFn, InlineFormatter formatFn);

} // namespace aicoder
