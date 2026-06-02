#pragma once
#include "ftxui/dom/elements.hpp"
#include "ftxui/component/event.hpp"
#include <string>

namespace aicoder {

enum class PermissionChoice {
    Allow,
    AllowForever,
    Deny
};

struct PermissionRequest {
    std::string tool_name;
    std::string arguments;
    std::string description;
};

class PermissionDialog {
public:
    explicit PermissionDialog(PermissionRequest request);

    ftxui::Element Render() const;
    bool OnEvent(ftxui::Event event);

    bool Finished() const { return finished_; }
    PermissionChoice Result() const { return result_; }
    void Reset();

    void SelectNext();
    void SelectPrev();
    void Confirm();
    void Deny();

private:
    PermissionRequest request_;
    int selected_index_ = 0;
    bool finished_ = false;
    PermissionChoice result_ = PermissionChoice::Deny;
    static constexpr int kItemCount = 3;
};

}
