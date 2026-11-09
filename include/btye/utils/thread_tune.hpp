#pragma once

#include <string>
#include <vector>

namespace btye {

// Лимит ОС: Linux - 15 байт, macOS - 63 байта, длиннее обрезается по границе символа.
// false - не удалось или ОС не поддерживает (Windows до 10 1607)
bool set_current_thread_name(std::string name);

// процессоры, на которых процессу разрешено выполняться: на Linux с учётом taskset и cpuset контейнера, на
// Windows - в группе процессоров процесса. Не пуст
[[nodiscard]] std::vector<size_t> available_cpus();

// привязывает текущий поток к процессору `cpu` (номер из available_cpus). false - не удалось или ОС не
// поддерживает привязку (macOS)
bool pin_current_thread_to_cpu(size_t cpu) noexcept;

}  // namespace btye
