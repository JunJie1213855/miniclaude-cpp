#include "tools/BuiltinTools.h"

#include "tools/BashTool.h"
#include "tools/CreateDirTool.h"
#include "tools/CreateFileTool.h"
#include "tools/DeleteFileTool.h"
#include "tools/EditFileTool.h"
#include "tools/GlobTool.h"
#include "tools/GrepTool.h"
#include "tools/ListDirTool.h"
#include "tools/MoveFileTool.h"
#include "tools/ReadFileTool.h"
#include "tools/WriteFileTool.h"

namespace aicoder {

void registerBuiltinTools(ToolRegistry& r) {
  // 只读
  r.registerTool(makeReadFileTool());
  r.registerTool(makeListDirTool());
  r.registerTool(makeGlobTool());
  r.registerTool(makeGrepTool());
  // 副作用（needsPermission=true）
  r.registerTool(makeWriteFileTool());
  r.registerTool(makeEditFileTool());
  r.registerTool(makeCreateFileTool());
  r.registerTool(makeCreateDirTool());
  r.registerTool(makeDeleteFileTool());
  r.registerTool(makeMoveFileTool());
  r.registerTool(makeBashTool());
}

}  // namespace aicoder
