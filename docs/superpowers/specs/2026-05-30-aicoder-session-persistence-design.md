# AICoder 会话持久化与命令行恢复设计

## Context

AICoder 当前的会话(`std::vector<Message>`)完全在进程内,`/quit` 或关掉终端后**全部丢失**。本设计加上**本地会话持久化**与**命令行恢复**:每轮自动写盘到全局目录,启动时可用 `-c` 自动接上次、或 `--resume` 弹列表挑历史。

适用范围:本期只接 `aicoder_tui`(用户明示);控制台 `aicoder` 后续应用同一 Session 模块即可。

已定决策(brainstorm 出来的):

| 决策点 | 结论 |
|---|---|
| 写盘时机 | **每轮完成后**(模型回复或报错)立刻写;原子写(tmp + rename) |
| `/clear` 语义 | **轮换到新 id**(新建目录);旧会话留在盘上供 `--resume` |
| `-c` 找哪个 | 按目录 mtime 取最新 |
| system prompt | **不存**,恢复时按当前 `kBaseSystemPrompt` + `loadRules` + `skillReg.promptList()` 重新构造 |
| CLI 库 | 不引第三方,手解析 argv(只有 `-c` / `--resume` 两个 flag) |

## 存储布局

```
~/.aicoder/sessions/
├── 20260530_133412_a3f2/
│   └── session.json
├── 20260530_142100_b81e/
│   └── session.json
└── ...
```

- 根目录:`~/.aicoder/sessions/`(全局,Workspace 已有 `globalDir()` 可拼)。
- 每会话独立子目录,**id 格式 `YYYYMMDD_HHMMSS_xxxx`**(本地时间戳 + 4 字符随机十六进制)。同秒并发也不会撞名。
- 单文件:`<dir>/session.json`(后续若要扩展可在同目录加 `attachments/` 等)。

## `session.json` schema

```json
{
  "schema_version": 1,
  "id": "20260530_133412_a3f2",
  "created_at": "2026-05-30T13:34:12+08:00",
  "updated_at": "2026-05-30T13:42:00+08:00",
  "model": "deepseek-v4-pro",
  "messages": [
    { "role": "user",      "content": [{"type":"text","text":"..."}] },
    { "role": "assistant", "content": [
        {"type":"text","text":"..."},
        {"type":"tool_use","id":"x","name":"y","input":{...}}
      ], "reasoning_content": "..." },
    { "role": "tool",      "content": [
        {"type":"tool_result","tool_use_id":"x","content":"...","is_error":false}
      ] }
  ]
}
```

- `role` ∈ `{user, assistant, tool}`(`system` 不存)。
- `content` 是 content-block 数组,与现有 `Message` 模型一一对应:
  - `TextBlock` → `{"type":"text","text":...}`
  - `ToolUseBlock` → `{"type":"tool_use","id":...,"name":...,"input":...}`
  - `ToolResultBlock` → `{"type":"tool_result","tool_use_id":...,"content":...,"is_error":...}`
- `reasoning_content` 只在 `role:assistant` 出现且非空时写入(deepseek-reasoner 思维链)。
- `model` 写入时取当前 `config.model`(每次 save 都用最新的),用于 `--resume` 列表里显示;**它是信息性字段**——`-c`/`--resume` 加载会话时,实际调用的 LLM 仍按**当前 config** 走,而非回切到 session.json 里保存的模型。
- 时间戳 ISO-8601 含时区。

**不存 system message**:恢复时按当前的 `kBaseSystemPrompt` + `loadRules(globalDir, cwd)` + `skillReg.promptList()` 重新构造,以反映项目规则/技能的最新状态。代价:若依赖旧规则的回答与新规则有出入,用户可感知;价值:项目演进期更友好,且没有"系统提示与对话内容不一致"的隐藏耦合。

## 生命周期

| 启动方式 | 行为 |
|---|---|
| `aicoder_tui` | `store.newId()` → 新建目录,空 messages;首次 turn 后才真正写出 `session.json` |
| `aicoder_tui -c` | `store.latestId()` → 加载;后续每轮写回**同一文件**(继续这条会话)。无任何历史 → 友好提示并退回新会话 |
| `aicoder_tui --resume` | `ResumePicker` 全屏列表(见下);Enter 加载、Esc 开新会话;空列表友好提示并按"无参"启动 |
| `/clear` | `sessionId = store.newId();` 新建目录,清空 messages,立即把空状态写一次。旧目录留在盘上 |
| `/quit` | 无特殊动作(上一轮已写) |

## 写盘

- 触发点:每轮**完成后**——`App::Impl::onSubmit` 的 worker 线程在 `rv->appendMessage(reply)` 之后(成功路径)和 `rv->appendError(...)` 之后(异常路径)各调一次 `store.save(sessionId, messages)`。
- 原子写:`SessionStore::save` 把序列化 JSON 先写到 `session.json.tmp`,`std::rename` 覆盖到 `session.json`。崩溃只可能留下 `.tmp` 或上一次完整的 `session.json`,不会写半截。
- 线程:save 在 worker 线程里跑(I/O 阻塞),不抢 UI 主线程;`messages` 在 UI 主线程修改、worker 读 —— App 现有架构已是「worker 后台跑、UI 主线程改」,worker 调用 `appendMessage` 时 messages 已由 worker 自己写入,save 紧随其后读自身刚写的内容,**单 worker** 顺序内一致。

## CLI 解析(`main_tui.cpp`)

手写 ~20 行解析,无第三方库:

```text
aicoder_tui                # 新会话
aicoder_tui -c             # 恢复 mtime 最新会话
aicoder_tui --resume       # 弹列表挑会话
aicoder_tui -c --resume    # 同时给：--resume 优先
其它任意参数               # 打印 usage 退出
```

错误参数 → 写一段 usage 到 stderr,exit 2。

## `ResumePicker`(--resume 的菜单,TUI 专属)

- 一个独立的 FTXUI 全屏 `Loop`(在 `App` 之前跑),用 `ftxui::Menu` + 自定义 entry 渲染。
- 列表来源:`store.listSessions()`(读 sessions 根目录,跳过损坏的、按 `updated_at` 倒序)。
- 每行展示:`updated_at(本地时间)  model  首条用户消息预览(<=60 字截断)  [N 条]`。
- 键位:↑↓ 移动,Enter 选中并退出 Loop 返回选中的 id,Esc / q 不选(返回空 optional)。
- 空列表:渲染一行提示「无历史会话,按任意键开始新会话」,任意键退出。
- 选中后 `main` 用该 id 加载 messages,以正常路径启动 `App`。

## 模块布局

```
src/sessions/Session.{h,cpp}        # 数据模型 + Message↔JSON 序列化(SessionData{id,created_at,updated_at,model,messages})
src/sessions/SessionStore.{h,cpp}   # rootDir/newId/listSessions/latestId/load/save 的封装
src/ui/ResumePicker.{h,cpp}         # showResumePicker(store) → std::optional<std::string> selectedId
src/main_tui.cpp                    # 改:argv 解析 → 决定 sessionId 与初始 messages → 传给 App
src/ui/App.{h,cpp}                  # 改:构造新增 (SessionStore&, sessionId, initial messages);
                                    #   App::Impl 持有可变成员 std::string sessionId_(被 /clear 改写);
                                    #   每轮 save;/clear 轮换 id
```

`SessionStore` 的关键接口:

```cpp
class SessionStore {
public:
  explicit SessionStore(std::filesystem::path root);     // 默认 globalDir()/"sessions"
  std::string newId() const;                              // 时间戳 + 随机
  std::vector<SessionInfo> listSessions() const;          // 按 updated_at 倒序，跳过损坏
  std::optional<std::string> latestId() const;
  std::optional<SessionData> load(const std::string& id); // 含 try/catch JSON 异常
  void save(const std::string& id, const SessionData&);   // 原子写；自动创建 <root>/<id>/
};

struct SessionInfo {
  std::string id;
  std::string updated_at;     // ISO-8601
  std::string model;
  std::string preview;        // 首条 user 消息前 60 字
  size_t message_count;
};
```

## 健壮性

- **目录缺失**:`save` 首次调用时 `create_directories(root/id)`;`SessionStore` 构造时也 `create_directories(root)`(根目录不存在则建)。
- **JSON 异常**:`load` 内部 `try { json::parse(...) } catch (...)` → 返回 `nullopt`;`listSessions` 跳过解析失败的目录。
- **`-c` 无历史**:返回到新会话路径,首屏打印一行「[未找到历史会话,开始新会话]」。
- **`-c` 找到但损坏**:同上 fallback + 提示。
- **schema_version 不匹配**:本版只接受 `1`,未知版本 `load` 返回 nullopt(将来加迁移)。
- **首条用户消息预览取不出**:`preview` 为空,列表用 `(无预览)` 占位。

## 数据流

```
启动
 ├─ 无参   → sessionId = store.newId();          initialMsgs = {}
 ├─ -c     → sessionId = store.latestId()?{}:newId(); initialMsgs = store.load(sessionId)?.messages
 └─ --resume → id = ResumePicker.show(store);
              sessionId = id ? id : store.newId();
              initialMsgs = id ? store.load(id)->messages : {}

App 启动时把 initialMsgs 注入 messages（systemPrompt 仍在前面,这部分 App bug 范围外）。

每轮：
  appendMessage(reply) / appendError(err)
    ↓
  store.save(sessionId, SessionData{id, created_at, now(), model, messages})

/clear:
  sessionId = store.newId()
  messages.clear()    （仍保留 System，参考现有 router.handle(/clear)）
  store.save(sessionId, SessionData{id, now(), now(), model, messages})
  UI 清屏
```

## 测试(GoogleTest + 临时目录)

- `SessionStore`:
  - `newId` 唯一性(连续生成 N 个不重复)。
  - `save` 原子写(写一半模拟:制造 `.tmp` 残留,下次 `load` 仍取到旧 `session.json`);自动创建目录。
  - `load` 缺失/损坏/版本不符 → `nullopt`。
  - `listSessions` 按 `updated_at` 倒序;跳过损坏目录。
  - `latestId` 选 mtime 最新。
- `Session`(序列化):
  - 三类 content block(text / tool_use / tool_result)round-trip。
  - `reasoning_content` 仅 assistant 且非空时输出;反序列化容忍缺失。
- `ResumePicker`、`main` 的 argv 解析:逻辑层(选 id 的纯函数)单测;TUI 渲染部分不在自动化测试范围(沿用现有 ReplView 不测的惯例)。

## 不在本期范围

- 控制台 `aicoder` 的 `-c`/`--resume`(模块通用,后续接入)。
- 会话保留策略(删除旧的);手动 `/sessions delete <id>`。
- 持久化 system prompt(选择每次重建)。
- 会话重命名/打标签/搜索。
- 多进程同 id 写盘的并发保护。
- 加密(本地明文 JSON 即可,与 Claude Code 风格一致)。
