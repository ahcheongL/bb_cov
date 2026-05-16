#include <chrono>
#include <iostream>

void show_progress(size_t current, size_t total,
                   std::chrono::steady_clock::time_point start_time);

#define PROGRESS_BAR_END() std::cout << std::endl;