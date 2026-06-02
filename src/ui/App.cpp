#include "App.h"
#include "ReplView.h"
#include "core/AgentLoop.h"
#include "core/Message.h"
#include "core/Errors.h"
#include "core/Json.h"
#include "core/PermissionStore.h"
#include "commands/CommandRouter.h"
#include "llm/StreamDelta.h"
#include "sessions/Session.h"
#include "sessions/SessionStore.h"
#include "util/ThreadPool.h"
#include "ftxui/screen/screen.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/loop.hpp"
#include "ftxui/dom/elements.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <thread>

namespace aicoder
{

  namespace {
  std::string nowIsoLocal() {
    using namespace std::chrono;
    std::time_t t = system_clock::to_time_t(system_clock::now());
    std::tm tm{}; localtime_r(&t, &tm);
    std::ostringstream os;
    os << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S%z");
    std::string s = os.str();
    if (s.size() >= 5 && (s[s.size() - 5] == '+' || s[s.size() - 5] == '-'))
      s.insert(s.size() - 2, ":");
    return s;
  }
  }  // namespace

  // 反思模式下"生成→批判→修订"的最大轮数。
  constexpr int kReflectRounds = 3;
  // 计划模式下最多执行的步骤数。
  constexpr int kPlanExecuteMaxSteps = 6;

  class App::Impl
  {
  public:
    // 声明顺序与 ctor 初始化列表一致，避免 -Wreorder：replView 最后构造
    // （它的 lambda 捕获 this，所以放最末更安全）。
    ftxui::ScreenInteractive screen = ftxui::ScreenInteractive::Fullscreen();
    ThreadPool pool{1};  // 1 线程，任务串行执行（当前模型为单对话）
    AgentLoop &agentLoop;
    CommandRouter &router;
    std::vector<Message> messages;
    SessionStore &store;
    std::string sessionId_;
    std::string model_;
    ReplView replView;

    Impl(AgentLoop &loop, CommandRouter &router, std::vector<Message> initialMsgs,
         SessionStore &store, std::string sid, std::string m)
        : agentLoop(loop), router(router),
          messages(std::move(initialMsgs)),
          store(store), sessionId_(std::move(sid)), model_(std::move(m)),
          replView([this](const std::string &input)
                   { onSubmit(input); })
    {
      // 关键顺序：先回放历史，再 setScreen。
      // FTXUI 的 task_sender_ 在 Loop 启动时才 Install；在那之前任何 screen->Post 都会被静默
      // 丢弃（Post 内部 `if (!task_sender_) return;`）。所以一旦 setScreen 之后再调
      // appendMessage，回放消息全部 Post 进空队列、被丢光，屏幕看不到任何历史。
      // 趁 screen 还没设上，appendMessage 走 screen-null 同步分支直接 push 进 messages_，
      // Loop 启动后第一帧就能渲染出来。
      for (const auto& msg : messages) {
        if (msg.role == Role::User) {
          replView.appendMessage({assistantText(msg), true, false});
        } else if (msg.role == Role::Assistant) {
          std::string text = assistantText(msg);
          if (!text.empty()) replView.appendMessage({text, false, false});
        }
        // Tool messages: skip — they're plumbing.
      }

      replView.setScreen(&screen);
      std::vector<std::pair<std::string, std::string>> cmds;
      for (const auto &c : router.commands())
        cmds.push_back({c.name, c.description});
      replView.setCommands(std::move(cmds));

      // 持久化层接线:工具执行前查规则,选 AllowForever 时落规则。
      // store 必须在 App 生命周期内有效;这里 lifetime 等于整个 TUI 会话,够用。
      static PermissionStore permStore;
      replView.setAllowLookupCallback(
          [](const std::string &tool, const json &input)
          { return permStore.isAllowed(tool, input); });
      replView.setAllowForeverCallback(
          [](const std::string &tool, const json &input)
          { permStore.allowForever(tool, input); });
    }

    void saveTurn() {
      SessionData d;
      d.id = sessionId_;
      d.model = model_;
      for (const auto& m : messages)
        if (m.role != Role::System) d.messages.push_back(m);
      auto existing = store.load(sessionId_);
      std::string now = nowIsoLocal();
      d.created_at = existing ? existing->created_at : now;
      d.updated_at = now;
      store.save(sessionId_, d);
    }

    void onSubmit(const std::string &input)
    {
      auto outcome = router.handle(input, messages);
      if (outcome.result == CommandResult::Quit)
      {
        screen.Exit();
        return;
      }
      if (outcome.result == CommandResult::Cleared)
      {
        sessionId_ = store.newId();
        saveTurn();
        replView.clearMessages(); // 清屏（messages_ 与对话历史分开存储，需单独清）
        replView.appendMessage({"[对话已清空]", false, false});
        return;
      }
      if (outcome.result == CommandResult::Reloaded)
      {
        // 刷新补全菜单（router.commands() 现在含新发现的技能）。
        // 注意：system prompt 的「## 可用技能」是启动时一次性构造的，本次不更新——
        // 菜单和 skill 工具按 name 查找会用到新技能，但模型不会通过 prompt 自动学到。
        std::vector<std::pair<std::string, std::string>> cmds;
        for (const auto &c : router.commands())
          cmds.push_back({c.name, c.description});
        replView.setCommands(std::move(cmds));
        replView.appendMessage({outcome.prompt, false, false});
        return;
      }
      messages.push_back(userText(outcome.result == CommandResult::Prompt ? outcome.prompt : input));
      replView.appendMessage({input, true, false});
      replView.setThinking(true);

      ChatMode mode = replView.mode(); // 提交时锁定当前模式（Tab 切换）
      auto loopPtr = &agentLoop;
      auto msgsPtr = &messages;
      auto rv = &replView;
      auto storePtr = &store;
      std::string sid = sessionId_;
      std::string model = model_;
      pool.submit([=]() {
      try {
        auto onDelta = [rv](const StreamDelta& d) {
          rv->appendDelta(d.text, d.reasoning);
        };
        auto onPerm = [rv](const std::string& toolName, const json& in) {
          return rv->askPermission(toolName, in);
        };
        std::string reply;
        if (mode == ChatMode::Reflection) {
          loopPtr->setSelfCheck(true);  // 反思模式：外层批判 + 每步自检
          reply = loopPtr->runWithReflection(*msgsPtr, kReflectRounds, onDelta, onPerm);
        } else if (mode == ChatMode::PlanExecute) {
          loopPtr->setSelfCheck(false);
          // onInfo：把计划作为持久气泡展示，并重置 thinking 占位（appendMessage 会清流式
          // 缓冲并移除占位），使后续步骤的流式增量仍能显示。
          auto onInfo = [rv](const std::string& text) {
            rv->appendMessage({text, false, false});
            rv->setThinking(true);
          };
          reply = loopPtr->runPlanExecute(*msgsPtr, kPlanExecuteMaxSteps, onDelta,
                                          onPerm, onInfo);
        } else {
          loopPtr->setSelfCheck(false);
          reply = loopPtr->run(*msgsPtr, onDelta, onPerm);
        }
        rv->appendMessage({reply, false, false});
      } catch (const LlmError& e) {
        rv->appendError(std::string("[错误] ") + e.what());
      }
      // Save session snapshot (succeeded or failed — either way messages was mutated).
      SessionData d;
      d.id = sid;
      d.model = model;
      for (const auto& m : *msgsPtr)
        if (m.role != Role::System) d.messages.push_back(m);
      auto existing = storePtr->load(sid);
      std::string now = nowIsoLocal();
      d.created_at = existing ? existing->created_at : now;
      d.updated_at = now;
      storePtr->save(sid, d); });
    }
  };

  App::App(AgentLoop &loop, CommandRouter &router, const std::string& /*systemPrompt*/,
           SessionStore& store, std::string initialSessionId,
           std::vector<Message> initialMessages, std::string model)
      : impl_(std::make_unique<Impl>(loop, router, std::move(initialMessages),
                                     store, std::move(initialSessionId), std::move(model))) {}

  App::~App() = default;

  void App::run()
  {
    auto root = impl_->replView.component();
    auto withBorder = ftxui::Renderer(root, [root]
                                      { return root->Render()
                                            // | ftxui::border
                                            ; });
    impl_->screen.Loop(withBorder);
  }

}
