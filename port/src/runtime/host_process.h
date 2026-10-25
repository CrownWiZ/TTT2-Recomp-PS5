#pragma once

namespace host_process {
// Exit immediately after the caller has drained logs. Guest worker threads and
// render teardown are deliberately not unwound on fatal or menu-requested exit.
[[noreturn]] void ExitNow(int status);
}
