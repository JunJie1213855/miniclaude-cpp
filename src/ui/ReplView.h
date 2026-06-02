#pragma once
#include <string>
#include <vector>
#include <functional>
#include <utility>
#include "ftxui/component/component.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "core/Json.h"

namespace aicoder
{

  struct UIMessage
  {
    std::string text;
    bool is_user = false;
    bool is_error = false;
    bool is_thinking = false; // true for placeholder thinking message
    bool is_tool = false;     // true for a tool-call line (⚙ 工具名 + 参数 + 结果摘要)
  };

  // 对话模式：Tab 键切换。Normal=普通 run；Reflection=runWithReflection + 每步自检。
  enum class ChatMode
  {
    Normal,
    Reflection,
    PlanExecute
  };

  class ReplView
  {
  public:
    using SubmitCallback = std::function<void(const std::string &text)>;

    ReplView(SubmitCallback onSubmit);

    ftxui::Component component();

    void appendMessage(UIMessage msg);
    // 清空屏幕上显示的全部消息（供 /clear 使用；与 App 的对话历史分开存储）。
    void clearMessages();
    void appendError(const std::string &msg);
    void appendDelta(const std::string &text, const std::string &reasoning);
    void setThinking(bool v);
    std::vector<UIMessage> messages() const;
    void setScreen(ftxui::ScreenInteractive *s);
    // 当前对话模式（由 Tab 键切换）。提交时 App 据此选择 run / runWithReflection。
    ChatMode mode() const;

    // 设置命令补全列表（data-driven，替代硬编码的 kSlashCommands）。
    void setCommands(std::vector<std::pair<std::string, std::string>> commands);
    // 设置当前模型名（供 welcome 页展示）。
    void setModel(const std::string& model);

    using ExitRequestedCallback = std::function<void()>;
    void setOnExitRequested(ExitRequestedCallback cb);

    // 权限询问：worker 线程调此方法等待用户批准（同步阻塞）
    // 内部 Post 到 UI 线程弹出 PermissionDialog,等待用户选 Allow / AllowForever / Deny。
    // 选 AllowForever 时,回调 onAllowForever(toolName, input) 会被调用
    // —— App 层在此回调里写 PermissionStore。
    using AllowForeverCallback = std::function<void(const std::string &toolName, const json &input)>;
    void setAllowForeverCallback(AllowForeverCallback cb);

    // 命中持久化规则时,worker 不进弹窗直接放行。App 在此注入 PermissionStore 查询。
    using AllowLookupCallback = std::function<bool(const std::string &toolName, const json &input)>;
    void setAllowLookupCallback(AllowLookupCallback cb);

    bool askPermission(const std::string &toolName, const json &input);

    // 工具调用展示：每次工具执行后追加一行"⚙ 工具名 + 参数预览 + 结果摘要"，
    // 让用户看到 aicoder 背后调了什么（只读 / 受限工具都显示）。argsJson 超长会截断。
    void appendToolCall(const std::string &name, const std::string &argsJson,
                        const std::string &result, bool isError);

  private:
    class Impl;
    std::shared_ptr<Impl> impl_;
  };

}