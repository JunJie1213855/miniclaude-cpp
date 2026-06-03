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
#include "skills/SkillRegistry.h"
#include "subagent/SubAgentRegistry.h"
#include "subagent/SubAgentManager.h"
#include "subagent/SubAgentTask.h"
#include "ui/ResumePicker.h"
#include "commands/CreateCommands.h"
#include "commands/ResourceGenerator.h"
#include "workspace/Workspace.h"
#include "util/ThreadPool.h"
#include "ftxui/screen/screen.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/loop.hpp"
#include "ftxui/dom/elements.hpp"

#include <chrono>
#include <ctime>
#include <functional>
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
    std::shared_ptr<std::atomic<bool>> cancel_;
    std::shared_ptr<std::atomic<bool>> taskRunning_;
    SubAgentManager* subAgentManager_ = nullptr;
    SubAgentRegistry* subAgentReg_ = nullptr;
    SkillRegistry* skillReg_ = nullptr;
    std::string systemPrompt_;

    Impl(AgentLoop &loop, CommandRouter &router, const std::string& systemPrompt,
         std::vector<Message> initialMsgs,
         SessionStore &store, std::string sid, std::string m)
        : agentLoop(loop), router(router),
          systemPrompt_(systemPrompt),
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
      replView.setModel(model_);

      // 持久化层接线:工具执行前查规则,选 AllowForever 时落规则。
      // store 必须在 App 生命周期内有效;这里 lifetime 等于整个 TUI 会话,够用。
      static PermissionStore permStore;
      replView.setAllowLookupCallback(
          [](const std::string &tool, const json &input)
          { return permStore.isAllowed(tool, input); });
      replView.setAllowForeverCallback(
          [](const std::string &tool, const json &input)
          { permStore.allowForever(tool, input); });

      cancel_ = std::make_shared<std::atomic<bool>>(false);
      taskRunning_ = std::make_shared<std::atomic<bool>>(false);
      agentLoop.setCancelToken(cancel_);

      replView.setOnExitRequested([this] {
        // 安全退出：保存会话后关闭 TUI
        cancel_->store(true);
        saveTurn();
        screen.Exit();
      });

      router.setSessionsCallback([this] { onSessionsCommand(); });
    }

    void setSubAgentRegistry(SubAgentRegistry* reg) {
      subAgentReg_ = reg;
    }

    void setSkillRegistry(SkillRegistry* reg) {
      skillReg_ = reg;
    }

    void onAgentsCommand() {
      replView.appendMessage({"[可用子代理]", false, false});
      if (!subAgentReg_ || subAgentReg_->empty()) {
        replView.appendMessage({"(无可用子代理，请在 ~/.aicoder/agents/ 下创建 AGENT.md)", false, false});
        return;
      }
      for (const auto& def : subAgentReg_->list()) {
        std::string line = def.name + " — " + def.description;
        replView.appendMessage({line, false, false});
      }
      replView.appendMessage({"[用法: /agent <名称> <任务描述> 或直接对话中指定子代理]", false, false});
    }

    void onSkillsCommand() {
      replView.appendMessage({"[可用技能]", false, false});
      if (!skillReg_) {
        replView.appendMessage({"(技能系统未初始化)", false, false});
        return;
      }
      auto skills = skillReg_->list();
      if (skills.empty()) {
        replView.appendMessage({"(无可用技能，请在 ~/.aicoder/skills/ 下创建 SKILL.md)", false, false});
        return;
      }
      for (const auto& s : skills) {
        std::string line = s.name + " — " + s.description;
        replView.appendMessage({line, false, false});
      }
      replView.appendMessage({"[用法: /<技能名> 直接调用]", false, false});
    }

    // 注册"资源被创建出来后,需要做的副作用":(向 registry 加一项 /
    // 注入命令 / 拼到 system prompt)。worker 与快速路径都会调用,放在
    // handleCreateCommand 之外以复用。
    enum class CreatePost { AddSkill, InjectCommand, AddAgent, AppendRule };

    // 在调用方决定方向后,把资源注册到对应 registry。
    // 必须线程安全:在快速路径(UI 线程)和 worker 线程都会被调用。
    // 这些 registry 内部走 std::mutex 或单写者,直接调用即可。
    void applyCreatePost(const CreateOutcome &r, CreatePost post)
    {
      if (!r.error.empty())
        return;
      switch (post)
      {
        case CreatePost::AddSkill:
          if (skillReg_) skillReg_->addOne(r.path);
          break;
        case CreatePost::InjectCommand:
          router.injectCommand(CommandTemplate{r.name, r.description, r.body});
          break;
        case CreatePost::AddAgent:
          if (subAgentReg_) subAgentReg_->addOne(r.path);
          break;
        case CreatePost::AppendRule:
          systemPrompt_ += "\n\n" + r.body;
          break;
      }
    }

    void refreshCommandMenu()
    {
      std::vector<std::pair<std::string, std::string>> cmds;
      for (const auto &c : router.commands())
        cmds.push_back({c.name, c.description});
      replView.setCommands(std::move(cmds));
    }

    void handleCreateCommand(const std::string &cmd, const std::string &arg) {
      namespace fs = std::filesystem;

      // 1) 解析用户输入 → {name, description, body}
      auto parsed = parseCreateArgs(arg);
      if (!parsed) {
        replView.appendMessage({"[缺少名称]", false, false});
        return;
      }

      // 2) 映射到具体的资源种类、目录、工厂、注册动作
      std::string kind;       // 传给 generateResource 用
      fs::path dir;           // 写入目录
      // 工厂函数指针:接 (name, desc, body, dir) → CreateOutcome
      using FactoryFn = std::function<CreateOutcome(const std::string &,
                                                    const std::string &,
                                                    const std::string &,
                                                    const fs::path &)>;
      FactoryFn factory;
      CreatePost post{};

      if (cmd == "/create_skill") {
        kind = "skill"; dir = globalDir() / "skills";
        factory = [](const std::string &n, const std::string &d,
                     const std::string &b, const fs::path &p) { return createSkill(n, d, b, p); };
        post = CreatePost::AddSkill;
      } else if (cmd == "/create_command") {
        kind = "command"; dir = globalDir() / "commands";
        factory = [](const std::string &n, const std::string &d,
                     const std::string &b, const fs::path &p) { return createCommand(n, d, b, p); };
        post = CreatePost::InjectCommand;
      } else if (cmd == "/create_agent") {
        kind = "agent"; dir = globalDir() / "agents";
        factory = [](const std::string &n, const std::string &d,
                     const std::string &b, const fs::path &p) { return createAgent(n, d, b, p); };
        post = CreatePost::AddAgent;
      } else if (cmd == "/create_rule") {
        kind = "rule"; dir = globalDir() / "rules";
        factory = [](const std::string &n, const std::string &d,
                     const std::string &b, const fs::path &p) { return createRule(n, d, b, p); };
        post = CreatePost::AppendRule;
      } else {
        replView.appendMessage({"[未知 create 命令: " + cmd + "]", false, false});
        return;
      }

      // 3) body 非空 → 走快速路径:直接写文件,无 LLM 调用
      if (!parsed->body.empty()) {
        CreateOutcome r = factory(parsed->name, parsed->description, parsed->body, dir);
        if (!r.error.empty()) {
          replView.appendMessage({r.error, false, false});
          return;
        }
        applyCreatePost(r, post);
        refreshCommandMenu();
        replView.appendMessage({r.success, false, false});
        return;
      }

      // 4) body 为空 → 派发到线程池调 LLM,避免阻塞 UI
      std::string name = parsed->name;
      std::string hint = parsed->description.empty()
                             ? "user wants a resource named " + name
                             : parsed->description;
      replView.setThinking(true);

      // 抓取 worker 所需的指针。router / registries 假定在
      // 进程内写者单线程化(快速路径发生在 onSubmit 同步段,Llm 调用
      // 发生在 worker;两者不会同时发生,因为 onSubmit 进入 LLM 段前
      // 不会派发新的 onSubmit 任务)。systemPrompt_ 在快速路径上由
      // UI 线程写,worker 路径下我们也只由 worker 写。
      auto rv = &replView;
      auto agentLoopPtr = &agentLoop;
      pool.submit([rv, factory, dir, post, name, hint, kind, agentLoopPtr, this]() {
        GeneratedResource gr = generateResource(agentLoopPtr->client(), kind, hint, name);
        if (gr.body.empty()) {
          rv->appendMessage({"[LLM 未返回内容,无法生成 " + kind + "]", false, false});
          rv->setThinking(false);
          return;
        }
        // 给用户看一眼 LLM 干了什么(预览前 200 字符)
        std::string preview = gr.body;
        if (preview.size() > 200) preview = preview.substr(0, 200) + "…";
        rv->appendMessage({"[已生成] name=" + gr.name +
                               ", desc=" + (gr.description.empty() ? "(无)" : gr.description) +
                               ", body=" + preview,
                           false, false});
        CreateOutcome r = factory(gr.name, gr.description, gr.body, dir);
        if (!r.error.empty()) {
          rv->appendMessage({r.error, false, false});
          rv->setThinking(false);
          return;
        }
        // 后置动作(注册/注入/追加到 system prompt)
        this->applyCreatePost(r, post);
        // 刷新补全菜单 + 终态
        this->refreshCommandMenu();
        rv->appendMessage({r.success, false, false});
        rv->setThinking(false);
      });
    }

    void setSubAgentManager(SubAgentManager* mgr) {
      subAgentManager_ = mgr;
      if (mgr) {
        mgr->setCancelToken(cancel_);
        mgr->setOnToolCall([this](const std::string& toolName,
                                  const json& /*input*/,
                                  const std::string& result,
                                  bool isError) {
          std::string display;
          if (isError) {
            display = "[子代理错误] " + toolName + ": " + result;
          } else {
            display = "[子代理] " + toolName + " ✓";
          }
          replView.appendMessage({display, false, false});
        });
        mgr->setUiNotify([this](const SubAgentTaskInfo& info) {
          std::string line;
          switch (info.status) {
            case SubAgentTaskStatus::Completed:
              line = "[后台子代理 " + info.agent_name + " 完成 (" + info.task_id + ")]";
              break;
            case SubAgentTaskStatus::Cancelled:
              line = "[后台子代理 " + info.agent_name + " 已取消 (" + info.task_id + ")]";
              break;
            case SubAgentTaskStatus::Failed:
              line = "[后台子代理 " + info.agent_name + " 失败 (" + info.task_id + "): " + info.error + "]";
              break;
            default:
              return;
          }
          replView.appendMessage({line, false, false});
        });
      }
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

    void onSessionsCommand()
    {
      bool wasRunning = taskRunning_->load();
      if (wasRunning) {
        cancel_->store(true);
        for (int i = 0; i < 120 && taskRunning_->load(); ++i)
          std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }
      saveTurn();
      auto newId = showResumePicker(this->store, this->sessionId_);
      if (newId && *newId != this->sessionId_) {
        auto data = this->store.load(*newId);
        this->sessionId_ = *newId;
        this->messages.clear();
        this->replView.clearMessages();
        if (data) {
          this->messages = std::move(data->messages);
          for (const auto& msg : this->messages) {
            if (msg.role == Role::User) {
              this->replView.appendMessage(UIMessage{assistantText(msg), true, false});
            } else if (msg.role == Role::Assistant) {
              std::string text = assistantText(msg);
              if (!text.empty())
                this->replView.appendMessage(UIMessage{text, false, false});
            }
          }
        }
        this->replView.appendMessage(UIMessage{"[已切换到会话 " + *newId + "]", false, false});
      }
      if (!taskRunning_->load())
        cancel_->store(false);
    }

    void onSubmit(const std::string &input)
    {
      // /sessions 是 App 级命令：需要访问 SessionStore、cancel token 等，
      // 无法通过纯函数 CommandRouter 完成，在此直接拦截。
      if (input == "/sessions") {
        onSessionsCommand();
        return;
      }
      if (input == "/agents") {
        onAgentsCommand();
        return;
      }
      if (input == "/skills") {
        onSkillsCommand();
        return;
      }
      if (input.rfind("/create_", 0) == 0) {
        size_t sp = input.find(' ');
        std::string cmd = sp == std::string::npos ? input : input.substr(0, sp);
        std::string arg = sp == std::string::npos ? "" : input.substr(sp + 1);
        handleCreateCommand(cmd, arg);
        return;
      }
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
      auto taskRunning = taskRunning_;
      pool.submit([=]() {
      taskRunning->store(true);
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
      storePtr->save(sid, d);
      taskRunning->store(false); });
    }
  };

  App::App(AgentLoop &loop, CommandRouter &router, const std::string& systemPrompt,
           SessionStore& store, std::string initialSessionId,
           std::vector<Message> initialMessages, std::string model)
      : impl_(std::make_unique<Impl>(loop, router, systemPrompt, std::move(initialMessages),
                                     store, std::move(initialSessionId), std::move(model))) {}

  App::~App() = default;

  void App::setSubAgentManager(SubAgentManager* mgr) {
    impl_->setSubAgentManager(mgr);
  }

  void App::setSubAgentRegistry(SubAgentRegistry* reg) {
    impl_->setSubAgentRegistry(reg);
  }

  void App::setSkillRegistry(SkillRegistry* reg) {
    impl_->setSkillRegistry(reg);
  }

  void App::run()
  {
    auto root = impl_->replView.component();
    auto withBorder = ftxui::Renderer(root, [root]
                                      { return root->Render()
                                            // | ftxui::border
                                            ; });
    // 禁止 FTXUI 强制接管 Ctrl+C：由 ReplView::CatchEvent 实现双击退出。
    impl_->screen.ForceHandleCtrlC(false);
    impl_->screen.Loop(withBorder); // 循环渲染
  }

}
