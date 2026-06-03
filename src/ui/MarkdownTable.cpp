#include "ui/MarkdownTable.h"
#include <algorithm>
#include <cctype>
#include <sstream>

namespace aicoder {

namespace {

// Trim leading and trailing whitespace (spaces and tabs).
std::string trim(const std::string& s) {
  auto start = std::find_if_not(s.begin(), s.end(),
                                [](unsigned char c) { return std::isspace(c); });
  auto end = std::find_if_not(s.rbegin(), s.rend(),
                              [](unsigned char c) { return std::isspace(c); })
                 .base();
  return (start < end) ? std::string(start, end) : std::string();
}

// A line is a horizontal rule if it consists only of 3+ dashes,
// asterisks, or underscores, with optional interspersed whitespace.
bool isHorizontalRule(const std::string& line) {
  std::string t = trim(line);
  if (t.size() < 3) return false;
  char c = t[0];
  if (c != '-' && c != '*' && c != '_') return false;
  return std::all_of(t.begin(), t.end(),
                     [c](char ch) { return ch == c || std::isspace(ch); });
}

} // anonymous namespace

bool isTableRow(const std::string& line) {
  // Must contain a pipe.
  if (line.find('|') == std::string::npos) return false;
  // Must parse into at least one cell.
  auto cells = parseTableRow(line);
  if (cells.empty()) return false;
  // Must not be a horizontal rule.
  if (isHorizontalRule(line)) return false;
  return true;
}

std::vector<std::string> parseTableRow(const std::string& line) {
  std::vector<std::string> cells;
  std::string s = line;

  // Strip leading pipe if present.
  if (!s.empty() && s.front() == '|') s = s.substr(1);
  // Strip trailing pipe if present.
  if (!s.empty() && s.back() == '|') s.pop_back();

  std::istringstream ss(s);
  std::string cell;
  while (std::getline(ss, cell, '|')) {
    cells.push_back(trim(cell));
  }
  return cells;
}

bool isTableSeparator(const std::string& line) {
  if (line.find('|') == std::string::npos) return false;
  auto cells = parseTableRow(line);
  if (cells.empty()) return false;

  for (const auto& cell : cells) {
    std::string t = trim(cell);
    if (t.empty()) return false;

    // Strip optional leading colon.
    if (t.front() == ':') t = t.substr(1);
    // Strip optional trailing colon.
    if (!t.empty() && t.back() == ':') t.pop_back();
    // Trim any remaining whitespace.
    t = trim(t);

    // Must contain at least 3 dashes.
    if (t.size() < 3) return false;
    // All remaining characters must be dashes.
    if (!std::all_of(t.begin(), t.end(), [](char c) { return c == '-'; }))
      return false;
  }
  return true;
}

std::vector<TableAlign> parseAlignment(const std::string& line) {
  std::vector<TableAlign> aligns;
  auto cells = parseTableRow(line);
  for (const auto& cell : cells) {
    std::string t = trim(cell);
    bool leftColon = (!t.empty() && t.front() == ':');
    bool rightColon = (!t.empty() && t.back() == ':');
    if (leftColon && rightColon) {
      aligns.push_back(TableAlign::Center);
    } else if (rightColon) {
      aligns.push_back(TableAlign::Right);
    } else {
      aligns.push_back(TableAlign::Left);
    }
  }
  return aligns;
}

std::optional<std::pair<ParsedTable, size_t>> parseTableBlock(
    const std::vector<std::string>& lines, size_t startIndex) {
  if (startIndex >= lines.size()) return std::nullopt;

  // Line 0: must be a table row (header) that is NOT a separator.
  if (!isTableRow(lines[startIndex]) || isTableSeparator(lines[startIndex]))
    return std::nullopt;

  auto headers = parseTableRow(lines[startIndex]);
  if (headers.empty()) return std::nullopt;
  size_t colCount = headers.size();

  // Line 1: must be a separator row.
  if (startIndex + 1 >= lines.size()) return std::nullopt;
  if (!isTableSeparator(lines[startIndex + 1])) return std::nullopt;

  auto separatorCells = parseTableRow(lines[startIndex + 1]);
  if (separatorCells.size() != colCount) return std::nullopt;

  auto alignments = parseAlignment(lines[startIndex + 1]);
  // Ensure the alignment vector has an entry for every column.
  while (alignments.size() < colCount)
    alignments.push_back(TableAlign::Left);

  // Lines 2+: data rows.  Stop at the first non-table-row line.
  std::vector<std::vector<std::string>> data;
  size_t i = startIndex + 2;
  while (i < lines.size()) {
    if (!isTableRow(lines[i])) break;
    auto cells = parseTableRow(lines[i]);
    if (cells.size() != colCount) break;
    data.push_back(std::move(cells));
    ++i;
  }

  ParsedTable table;
  table.headers = std::move(headers);
  table.alignments = std::move(alignments);
  table.data = std::move(data);

  return std::make_pair(std::move(table), i - startIndex);
}

ftxui::Element renderTable(const ParsedTable& table, int termWidth,
                           TextWrapper wrapFn, InlineFormatter formatFn) {
  if (termWidth <= 0) termWidth = 80;

  const size_t colCount = table.headers.size();
  if (colCount == 0)
    return ftxui::text("");

  // Step 1: compute desired width per column (longest un-wrapped cell text).
  std::vector<int> desired(colCount, 0);
  for (size_t c = 0; c < colCount; ++c) {
    desired[c] = static_cast<int>(table.headers[c].size());
    for (const auto& row : table.data) {
      if (c < row.size())
        desired[c] = std::max(desired[c], static_cast<int>(row[c].size()));
    }
    desired[c] = std::max(desired[c], 5); // minimum column width
  }

  // Step 2: distribute terminal width proportionally.
  int totalDesired = 0;
  for (int w : desired) totalDesired += w;

  std::vector<int> colWidths(colCount, 5);
  int allocated = 0;
  for (size_t c = 0; c < colCount; ++c) {
    if (totalDesired > 0)
      colWidths[c] = std::max(5, desired[c] * termWidth / totalDesired);
    if (c == colCount - 1) {
      // Last column soaks up the remainder so the table fills the terminal.
      colWidths[c] = termWidth - allocated;
      if (colWidths[c] < 5) colWidths[c] = 5;
    }
    allocated += colWidths[c];
  }

  // Helper: wrap + format + align a single cell.
  auto buildCell = [&](const std::string& text, int width,
                       TableAlign align) -> ftxui::Element {
    auto wrapped = wrapFn(text, width);
    ftxui::Elements lines;
    for (const auto& w : wrapped) {
      auto formatted = formatFn(w);
      if (align == TableAlign::Center)
        lines.push_back(
            ftxui::hbox({ftxui::filler(), formatted, ftxui::filler()}));
      else if (align == TableAlign::Right)
        lines.push_back(ftxui::hbox({ftxui::filler(), formatted}));
      else
        lines.push_back(ftxui::hbox({formatted, ftxui::filler()}));
    }
    return ftxui::vbox(std::move(lines)) |
           ftxui::size(ftxui::WIDTH, ftxui::EQUAL, width);
  };

  // Step 3: assemble gridbox rows.
  std::vector<ftxui::Elements> rows;

  // Header row (bold).
  {
    ftxui::Elements headerCells;
    for (size_t c = 0; c < colCount; ++c) {
      TableAlign align =
          (c < table.alignments.size()) ? table.alignments[c] : TableAlign::Left;
      // For headers, wrap + format then apply bold on top.
      auto formatted = formatFn(table.headers[c]) | ftxui::bold;
      ftxui::Element cell;
      if (align == TableAlign::Center)
        cell = ftxui::hbox({ftxui::filler(), formatted, ftxui::filler()});
      else if (align == TableAlign::Right)
        cell = ftxui::hbox({ftxui::filler(), formatted});
      else
        cell = ftxui::hbox({formatted, ftxui::filler()});
      headerCells.push_back(cell |
                            ftxui::size(ftxui::WIDTH, ftxui::EQUAL,
                                        colWidths[c]));
    }
    rows.push_back(std::move(headerCells));
  }

  // Separator row.
  {
    ftxui::Elements sepCells;
    for (size_t c = 0; c < colCount; ++c) {
      sepCells.push_back(ftxui::separatorCharacter("-") |
                         ftxui::size(ftxui::WIDTH, ftxui::EQUAL,
                                     colWidths[c]));
    }
    rows.push_back(std::move(sepCells));
  }

  // Data rows.
  for (const auto& dataRow : table.data) {
    ftxui::Elements dataCells;
    for (size_t c = 0; c < colCount; ++c) {
      TableAlign align =
          (c < table.alignments.size()) ? table.alignments[c] : TableAlign::Left;
      std::string cellText = (c < dataRow.size()) ? dataRow[c] : std::string();
      dataCells.push_back(buildCell(cellText, colWidths[c], align));
    }
    rows.push_back(std::move(dataCells));
  }

  return ftxui::gridbox(std::move(rows)) | ftxui::flex_grow;
}

} // namespace aicoder
