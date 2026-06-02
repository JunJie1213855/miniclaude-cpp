#pragma once
#include <string>

namespace aicoder
{
  // 流式显示增量：仅供 UI 实时展示，不参与工具调用判断。
  struct StreamDelta
  {
    std::string text;      // content 增量（最终答复正文）
    std::string reasoning; // reasoning_content 增量（思维链）
  };
}
