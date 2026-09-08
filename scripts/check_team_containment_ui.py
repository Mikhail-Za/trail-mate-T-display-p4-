#!/usr/bin/env python3
"""Compile and exercise live Team containment UI function bodies."""

from pathlib import Path
import os
import resource
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "modules/ui_shared/src/ui/screens/team/team_page_components.cpp"
HANDLER = "void handle_kick_confirm(lv_event_t*)"
NOTIFIER = "void notify_send_failed_detail(const char* action, team::TeamService::SendError err)"


def extract_body(source: str, marker: str) -> str:
    start = source.index(marker)
    brace = source.index("{", start)
    depth = 0
    for pos in range(brace, len(source)):
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
            if depth == 0:
                return source[brace : pos + 1]
    raise RuntimeError(f"unterminated body for {marker}")


def harness(handler_body: str, notifier_body: str) -> str:
    return r'''
#include <cassert>
#include <string>
#include <vector>

struct lv_event_t {};
struct State {};
struct Reducer {};
struct Runtime {};
struct TeamPageRandomByteAdapter {};
struct TeamPageKickConfirmDeferredAdapter {};
enum class TeamPage { StatusInTeam };

namespace team {
struct TeamService {
    enum class SendError {
        None, KeysNotReady, EncodeFail, EncryptFail, MeshSendFail,
        UnsupportedByProtocol, SecurityUnavailable
    };
};
}

struct Effects {
    bool accepted;
    int failures;
    team::TeamService::SendError error;
};
void notify_send_failed_detail(const char*, team::TeamService::SendError);

namespace ui {
namespace i18n { const char* tr(const char* text) { return text; } }
struct SystemNotification {
    static void show(const char* text, int) { notices.emplace_back(text); }
    static std::vector<std::string> notices;
};
std::vector<std::string> SystemNotification::notices;
}

Effects next_effects{false, 0, team::TeamService::SendError::None};
int state_applies = 0;
int saves = 0;
int navigations = 0;

State command_state_from_page() { return {}; }
Reducer current_command_reducer() { return {}; }
Runtime current_runtime_port() { return {}; }
void apply_kick_confirm_failures(const Effects& effects)
{
    for (int i = 0; i < effects.failures; ++i)
        notify_send_failed_detail("Kick", effects.error);
}
void apply_command_state_to_page(const State&) { ++state_applies; }
void save_state_to_store() { ++saves; }
void nav_reset(TeamPage) { ++navigations; }

namespace app {
struct MessagingFacade { unsigned getSelfNodeId() const { return 1; } };
MessagingFacade messagingFacade() { return {}; }
}

struct TeamPageKickConfirmAction {
    Effects confirmKick(State&, const Reducer&, const Runtime&,
                        TeamPageRandomByteAdapter&,
                        TeamPageKickConfirmDeferredAdapter&, unsigned) const
    {
        return next_effects;
    }
};
''' + "\n" + NOTIFIER + "\n" + notifier_body + "\n" + HANDLER + "\n" + handler_body + r'''

void reset_counts()
{
    ::ui::SystemNotification::notices.clear();
    state_applies = saves = navigations = 0;
}

int main()
{
    next_effects = {false, 0, team::TeamService::SendError::None};
    reset_counts();
    handle_kick_confirm(nullptr);
    assert(::ui::SystemNotification::notices.empty());
    assert(state_applies == 0 && saves == 0 && navigations == 0);

    next_effects = {false, 1, team::TeamService::SendError::SecurityUnavailable};
    reset_counts();
    handle_kick_confirm(nullptr);
    assert(::ui::SystemNotification::notices.size() == 1);
    assert(::ui::SystemNotification::notices[0] == "Kick: secure removal unavailable");
    assert(state_applies == 0 && saves == 0 && navigations == 0);

    next_effects = {true, 0, team::TeamService::SendError::None};
    reset_counts();
    handle_kick_confirm(nullptr);
    assert(::ui::SystemNotification::notices.empty());
    assert(state_applies == 1 && saves == 1 && navigations == 1);

    ::ui::SystemNotification::notices.clear();
    notify_send_failed_detail("Kick", team::TeamService::SendError::SecurityUnavailable);
    notify_send_failed_detail("Request Keys", team::TeamService::SendError::SecurityUnavailable);
    notify_send_failed_detail("Status", team::TeamService::SendError::MeshSendFail);
    assert(::ui::SystemNotification::notices.size() == 3);
    assert(::ui::SystemNotification::notices[0] == "Kick: secure removal unavailable");
    assert(::ui::SystemNotification::notices[1] == "Request Keys: secure key update unavailable");
    assert(::ui::SystemNotification::notices[2] == "Status: queue full");
}
'''


def main() -> None:
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    source = SOURCE.read_text()
    handler_body = extract_body(source, HANDLER)
    notifier_body = extract_body(source, NOTIFIER)
    guard = """    if (!effects.accepted)\n    {\n        return;\n    }\n"""
    mutated_body = handler_body.replace(guard, "", 1)
    if mutated_body == handler_body:
        raise RuntimeError("kick-confirm rejection guard not found")

    with tempfile.TemporaryDirectory() as temp:
        cpp = Path(temp) / "handler.cpp"
        exe = Path(temp) / "handler"
        command = [os.environ.get("CXX", "g++"), "-std=c++17", "-Wall",
                   "-Wextra", "-Werror", "-fuse-ld=bfd", str(cpp), "-o", str(exe)]

        cpp.write_text(harness(handler_body, notifier_body))
        subprocess.run(command, check=True)
        subprocess.run([str(exe)], check=True)

        cpp.write_text(harness(mutated_body, notifier_body))
        subprocess.run(command, check=True)
        mutation = subprocess.run([str(exe)], capture_output=True, text=True)
        expected_failure = "state_applies == 0 && saves == 0 && navigations == 0"
        if mutation.returncode == 0 or expected_failure not in mutation.stderr:
            raise RuntimeError("guard-removal mutation did not fail the state counter assertion")

    print("team containment UI handler and guard-removal mutation checks passed")


if __name__ == "__main__":
    main()
