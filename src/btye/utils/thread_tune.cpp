#include "btye/utils/thread_tune.hpp"

#include <algorithm>
#include <string>
#include <thread>

#if defined(_WIN32)
  #include <windows.h>
#elif defined(__linux__)
  #include <pthread.h>
  #include <sched.h>
#elif defined(__APPLE__)
  #include <pthread.h>
#endif

namespace btye {

// обрезает до max байт, не разрывая символ UTF-8
[[maybe_unused]] static void truncate_utf8(std::string& s, size_t max) noexcept {
  if (s.size() <= max)
    return;
  while (max > 0 && (static_cast<unsigned char>(s[max]) & 0xC0) == 0x80)
    --max;
  s.resize(max);
}

#if defined(_WIN32)

bool set_current_thread_name(std::string name) {
  // SetThreadDescription есть с Windows 10 1607, поэтому ищется во время выполнения
  using set_description_t = HRESULT(WINAPI*)(HANDLE, PCWSTR);
  static const auto set_description = reinterpret_cast<set_description_t>(
      reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetThreadDescription")));
  if (!set_description)
    return false;
  int n = MultiByteToWideChar(CP_UTF8, 0, name.data(), int(name.size()), nullptr, 0);
  std::wstring wname(size_t(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, name.data(), int(name.size()), wname.data(), n);
  return SUCCEEDED(set_description(GetCurrentThread(), wname.c_str()));
}

std::vector<size_t> available_cpus() {
  DWORD_PTR process_mask = 0;
  DWORD_PTR system_mask = 0;
  std::vector<size_t> cpus;
  if (GetProcessAffinityMask(GetCurrentProcess(), &process_mask, &system_mask)) {
    for (size_t i = 0; i < sizeof(process_mask) * 8; ++i)
      if (process_mask & (DWORD_PTR(1) << i))
        cpus.push_back(i);
  }
  if (cpus.empty())
    cpus.push_back(0);
  return cpus;
}

bool pin_current_thread_to_cpu(size_t cpu) noexcept {
  if (cpu >= sizeof(DWORD_PTR) * 8)
    return false;
  return SetThreadAffinityMask(GetCurrentThread(), DWORD_PTR(1) << cpu) != 0;
}

#elif defined(__linux__)

bool set_current_thread_name(std::string name) {
  truncate_utf8(name, 15);
  return pthread_setname_np(pthread_self(), name.c_str()) == 0;
}

std::vector<size_t> available_cpus() {
  cpu_set_t set;
  CPU_ZERO(&set);
  std::vector<size_t> cpus;
  if (sched_getaffinity(0, sizeof(set), &set) == 0) {
    for (size_t i = 0; i < CPU_SETSIZE; ++i)
      if (CPU_ISSET(i, &set))
        cpus.push_back(i);
  }
  if (cpus.empty())
    cpus.push_back(0);
  return cpus;
}

bool pin_current_thread_to_cpu(size_t cpu) noexcept {
  if (cpu >= CPU_SETSIZE)
    return false;
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(cpu, &set);
  return pthread_setaffinity_np(pthread_self(), sizeof(set), &set) == 0;
}

#else  // macOS и прочие: привязки к процессору нет

bool set_current_thread_name(std::string name) {
  #if defined(__APPLE__)
  truncate_utf8(name, 63);
  return pthread_setname_np(name.c_str()) == 0;
  #else
  (void)name;
  return false;
  #endif
}

std::vector<size_t> available_cpus() {
  std::vector<size_t> cpus(std::max(1u, std::thread::hardware_concurrency()));
  for (size_t i = 0; i < cpus.size(); ++i)
    cpus[i] = i;
  return cpus;
}

bool pin_current_thread_to_cpu(size_t) noexcept {
  return false;
}

#endif

}  // namespace btye
