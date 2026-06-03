#include "ui/MarkdownTable.h"
#include <ftxui/screen/screen.hpp>
#include <gtest/gtest.h>
#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

using namespace aicoder;

// ---------------------------------------------------------------------------
// Mock callbacks
// ---------------------------------------------------------------------------
namespace {

std::vector<std::string> mockWrap(const std::string& s, int width) {
  if (width <= 0) return {};
  std::vector<std::string> lines;
  for (size_t i = 0; i < s.size(); i += static_cast<size_t>(width))
    lines.push_back(s.substr(i, static_cast<size_t>(width)));
  if (lines.empty())
    lines.emplace_back();
  return lines;
}

ftxui::Element mockFormat(const std::string& s) { return ftxui::text(s); }

} // anonymous namespace

// ---------------------------------------------------------------------------
// isTableSeparator
// ---------------------------------------------------------------------------

TEST(MarkdownTable, IsSeparatorValid) {
  // Standard 3-dash separator.
  EXPECT_TRUE(isTableSeparator("| --- | --- | --- |"));
  // With alignment colons.
  EXPECT_TRUE(isTableSeparator("|:---|:---:| ---:|"));
  // No leading/trailing pipe.
  EXPECT_TRUE(isTableSeparator("--- | --- | ---"));
  // 3 dashes is the minimum per cell.
  EXPECT_TRUE(isTableSeparator("|---|"));
}

TEST(MarkdownTable, IsSeparatorInvalid) {
  // Empty input.
  EXPECT_FALSE(isTableSeparator(""));
  // Plain text, no pipes.
  EXPECT_FALSE(isTableSeparator("hello world"));
  // Contains non-dash, non-colon characters.
  EXPECT_FALSE(isTableSeparator("| abc | def |"));
  // Only two dashes per cell → not enough.
  EXPECT_FALSE(isTableSeparator("| -- | -- |"));
  // Missing cell content between pipes.
  EXPECT_FALSE(isTableSeparator("| | |"));
}

// ---------------------------------------------------------------------------
// parseTableRow
// ---------------------------------------------------------------------------

TEST(MarkdownTable, ParseTableRowBasic) {
  auto cells = parseTableRow("| Name | Age | City |");
  ASSERT_EQ(cells.size(), 3u);
  EXPECT_EQ(cells[0], "Name");
  EXPECT_EQ(cells[1], "Age");
  EXPECT_EQ(cells[2], "City");
}

TEST(MarkdownTable, ParseTableRowNoleadingPipe) {
  auto cells = parseTableRow("Name | Age | City");
  ASSERT_EQ(cells.size(), 3u);
  EXPECT_EQ(cells[0], "Name");
  EXPECT_EQ(cells[1], "Age");
  EXPECT_EQ(cells[2], "City");
}

TEST(MarkdownTable, ParseTableRowSingleColumn) {
  auto cells = parseTableRow("| data |");
  ASSERT_EQ(cells.size(), 1u);
  EXPECT_EQ(cells[0], "data");
}

TEST(MarkdownTable, ParseTableRowEmptyCells) {
  // Deliberately empty cells.
  auto cells = parseTableRow("| a |  | c |");
  ASSERT_EQ(cells.size(), 3u);
  EXPECT_EQ(cells[0], "a");
  EXPECT_EQ(cells[1], "");  // empty after trim
  EXPECT_EQ(cells[2], "c");
}

// ---------------------------------------------------------------------------
// parseAlignment
// ---------------------------------------------------------------------------

TEST(MarkdownTable, ParseAlignmentBasic) {
  // :---  → Left   (colon only on left)
  // :---: → Center (colons on both sides)
  // ---:  → Right  (colon only on right)
  auto aligns = parseAlignment("|:---|:---:| ---:|");
  ASSERT_EQ(aligns.size(), 3u);
  EXPECT_EQ(aligns[0], TableAlign::Left);
  EXPECT_EQ(aligns[1], TableAlign::Center);
  EXPECT_EQ(aligns[2], TableAlign::Right);
}

TEST(MarkdownTable, ParseAlignmentDefaultLeft) {
  // No colons → default to Left.
  auto aligns = parseAlignment("| --- | --- | --- |");
  ASSERT_EQ(aligns.size(), 3u);
  for (const auto& a : aligns)
    EXPECT_EQ(a, TableAlign::Left);
}

// ---------------------------------------------------------------------------
// parseTableBlock
// ---------------------------------------------------------------------------

TEST(MarkdownTable, ParseTableBlockBasic) {
  std::vector<std::string> lines = {
      "| Name | Age |",
      "| ---  | ---:|",
      "| Alice | 30 |",
      "| Bob   | 25 |",
  };
  auto result = parseTableBlock(lines, 0);
  ASSERT_TRUE(result.has_value());
  const auto& [table, consumed] = *result;
  EXPECT_EQ(consumed, 4u);

  ASSERT_EQ(table.headers.size(), 2u);
  EXPECT_EQ(table.headers[0], "Name");
  EXPECT_EQ(table.headers[1], "Age");

  ASSERT_EQ(table.alignments.size(), 2u);
  EXPECT_EQ(table.alignments[0], TableAlign::Left);
  EXPECT_EQ(table.alignments[1], TableAlign::Right);

  ASSERT_EQ(table.data.size(), 2u);
  EXPECT_EQ(table.data[0][0], "Alice");
  EXPECT_EQ(table.data[0][1], "30");
  EXPECT_EQ(table.data[1][0], "Bob");
  EXPECT_EQ(table.data[1][1], "25");
}

TEST(MarkdownTable, ParseTableBlockNoDataRows) {
  std::vector<std::string> lines = {
      "| X | Y |",
      "| --- | --- |",
  };
  auto result = parseTableBlock(lines, 0);
  ASSERT_TRUE(result.has_value());
  const auto& [table, consumed] = *result;
  EXPECT_EQ(consumed, 2u);
  EXPECT_EQ(table.headers.size(), 2u);
  EXPECT_TRUE(table.data.empty());
}

TEST(MarkdownTable, ParseTableBlockMismatchedColumns) {
  // A data row with a different column count should stop collection.
  std::vector<std::string> lines = {
      "| A | B |",
      "| --- | --- |",
      "| 1 | 2 |",      // matches 2 columns
      "| 3 | 4 | 5 |",  // 3 columns → mismatch → stop here
      "| 6 | 7 |",      // would match but already stopped
  };
  auto result = parseTableBlock(lines, 0);
  ASSERT_TRUE(result.has_value());
  const auto& [table, consumed] = *result;
  // consumed = header(1) + separator(1) + first data row(1) = 3
  EXPECT_EQ(consumed, 3u);
  ASSERT_EQ(table.data.size(), 1u);
  EXPECT_EQ(table.data[0][0], "1");
  EXPECT_EQ(table.data[0][1], "2");
}

TEST(MarkdownTable, ParseTableBlockStopsAtNonTable) {
  std::vector<std::string> lines = {
      "| Col |",
      "| --- |",
      "| a   |",
      "This is not a table row.",
      "| b   |",
  };
  auto result = parseTableBlock(lines, 0);
  ASSERT_TRUE(result.has_value());
  const auto& [table, consumed] = *result;
  // consumed: header(1) + sep(1) + data "a"(1) = 3; non-table line breaks.
  EXPECT_EQ(consumed, 3u);
  ASSERT_EQ(table.data.size(), 1u);
  EXPECT_EQ(table.data[0][0], "a");
}

TEST(MarkdownTable, ParseTableBlockInvalidStart) {
  // No table at the given index.
  std::vector<std::string> lines = {"just text"};
  EXPECT_FALSE(parseTableBlock(lines, 0).has_value());
}

TEST(MarkdownTable, ParseTableBlockStartIndexOutOfBounds) {
  std::vector<std::string> lines = {"a", "b"};
  EXPECT_FALSE(parseTableBlock(lines, 5).has_value());
}

// ---------------------------------------------------------------------------
// isTableRow
// ---------------------------------------------------------------------------

TEST(MarkdownTable, IsTableRowTrue) {
  EXPECT_TRUE(isTableRow("| a | b | c |"));
  EXPECT_TRUE(isTableRow("a | b | c"));          // no leading pipe
  EXPECT_TRUE(isTableRow("| single cell |"));    // single column
}

TEST(MarkdownTable, IsTableRowFalseForNonTable) {
  EXPECT_FALSE(isTableRow("plain text line"));
  EXPECT_FALSE(isTableRow(""));
  // A line of only dashes (no pipes) is a horizontal rule, not a table row.
  EXPECT_FALSE(isTableRow("---"));
}

// ---------------------------------------------------------------------------
// Render
// ---------------------------------------------------------------------------

// Helper: render element to a flat text string via ftxui::Screen.
std::string renderToString(ftxui::Element el, int fixedWidth = 80) {
  auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(fixedWidth),
                                      ftxui::Dimension::Fit(el));
  ftxui::Render(screen, el);
  std::string out;
  for (int y = 0; y < screen.dimy(); ++y) {
    for (int x = 0; x < screen.dimx(); ++x)
      out += screen.at(x, y);
    out += '\n';
  }
  return out;
}

TEST(MarkdownTable, RenderBasicTable) {
  ParsedTable table;
  table.headers = {"Name", "Value"};
  table.alignments = {TableAlign::Left, TableAlign::Left};
  table.data = {{"Alpha", "100"}, {"Beta", "200"}};

  auto el = renderTable(table, 40, mockWrap, mockFormat);
  auto out = renderToString(el, 40);

  // Header cells.
  EXPECT_NE(out.find("Name"), std::string::npos);
  EXPECT_NE(out.find("Value"), std::string::npos);
  // Data cells.
  EXPECT_NE(out.find("Alpha"), std::string::npos)
      << "missing 'Alpha' in:\n" << out;
  EXPECT_NE(out.find("100"), std::string::npos);
  EXPECT_NE(out.find("Beta"), std::string::npos);
  EXPECT_NE(out.find("200"), std::string::npos);
  // Separator row should contain dashes.
  EXPECT_NE(out.find("-"), std::string::npos);
}

TEST(MarkdownTable, RenderAlignmentRight) {
  // Verify that a table with right-aligned columns renders without crash
  // and the cell text appears in the output. (Gridbox renders cells at
  // content width, so precise spatial position cannot be tested via Screen.)
  ParsedTable table;
  table.headers = {"Item", "Price"};
  table.alignments = {TableAlign::Left, TableAlign::Right};
  table.data = {{"Alpha", "42"}};

  auto el = renderTable(table, 60, mockWrap, mockFormat);
  auto out = renderToString(el, 60);

  // Verify all cell text appears.
  EXPECT_NE(out.find("Item"), std::string::npos)
      << "missing 'Item' in:\n" << out;
  EXPECT_NE(out.find("Price"), std::string::npos)
      << "missing 'Price' in:\n" << out;
  EXPECT_NE(out.find("Alpha"), std::string::npos)
      << "missing 'Alpha' in:\n" << out;
  EXPECT_NE(out.find("42"), std::string::npos)
      << "missing '42' in:\n" << out;

  // Verify column ordering: left-to-right in the data row.
  auto posAlpha = out.find("Alpha");
  auto pos42 = out.find("42");
  EXPECT_LT(posAlpha, pos42)
      << "\"Alpha\" should appear before \"42\" in:\n" << out;
}

TEST(MarkdownTable, RenderEmptyDataOk) {
  ParsedTable table;
  table.headers = {"Col1", "Col2"};
  table.alignments = {TableAlign::Left, TableAlign::Center};
  // No data rows.

  auto el = renderTable(table, 40, mockWrap, mockFormat);
  auto out = renderToString(el, 40);

  // Must contain the headers.
  EXPECT_NE(out.find("Col1"), std::string::npos);
  EXPECT_NE(out.find("Col2"), std::string::npos);
  // Should contain separator dashes.
  EXPECT_NE(out.find("-"), std::string::npos);
  // Renders without crash.
}

TEST(MarkdownTable, RenderEmptyHeadersReturnsEmpty) {
  ParsedTable table;
  // No headers at all.
  auto el = renderTable(table, 40, mockWrap, mockFormat);
  auto out = renderToString(el, 40);
  // Should produce a minimal (empty or nearly empty) element.
  // The function returns ftxui::text("") for zero columns.
  EXPECT_TRUE(out.empty() || out.find_first_not_of(" \n") == std::string::npos);
}

TEST(MarkdownTable, RenderAlignmentCenter) {
  // Verify that a table with center-aligned columns renders without crash
  // and the cell text appears in the output.
  ParsedTable table;
  table.headers = {"Name", "Score"};
  table.alignments = {TableAlign::Left, TableAlign::Center};
  table.data = {{"Alice", "92"}};

  auto el = renderTable(table, 60, mockWrap, mockFormat);
  auto out = renderToString(el, 60);

  // Verify all cell text appears.
  EXPECT_NE(out.find("Name"), std::string::npos)
      << "missing 'Name' in:\n" << out;
  EXPECT_NE(out.find("Score"), std::string::npos)
      << "missing 'Score' in:\n" << out;
  EXPECT_NE(out.find("Alice"), std::string::npos)
      << "missing 'Alice' in:\n" << out;
  EXPECT_NE(out.find("92"), std::string::npos)
      << "missing '92' in:\n" << out;

  // Verify column ordering: left-to-right in the Screen output.
  auto posAlice = out.find("Alice");
  auto pos92 = out.find("92");
  EXPECT_LT(posAlice, pos92)
      << "\"Alice\" should appear before \"92\" in:\n" << out;
}
