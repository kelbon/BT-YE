#pragma once

#include <anyany/anyany.hpp>

namespace btye::noexport {

// при падении (SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS) печатает сигнал и стек в stderr, затем падает как
// без обработчика (core dump, код выхода). Печать не signal safe: если не получится, процесс просто упадёт.
// Возвращает функцию, которая возвращает прежние обработчики
aa::move_only_fn<void() noexcept> handle_crash();

}  // namespace btye::noexport
