#pragma once

#include <anyany/anyany.hpp>

#include "btye/actor.hpp"

namespace btye::noexport {

// SIGINT/SIGTERM (Windows - Ctrl+C и Ctrl+Break) вызывают request_stop на акторе `a0`. Вызывается на потоке
// актора 0 после создания его контекста и до создания потоков остальных акторов.
// Возвращает функцию, которая заканчивает обработку, её вызывают на акторе 0. После неё сигналы
// обрабатывает система
aa::move_only_fn<void() noexcept> handle_interrupt(actor& a0);

}  // namespace btye::noexport
