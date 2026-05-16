#include "utils/progress_bar.hpp"

#include <iomanip>
#include <iostream>

void show_progress(size_t current, size_t total,
                   std::chrono::steady_clock::time_point start_time) {
  static auto previous_time = std::chrono::steady_clock::now();
  auto now = std::chrono::steady_clock::now();
  if (std::chrono::duration_cast<std::chrono::milliseconds>(now - previous_time)
          .count() < 100) {
    return;
  }
  previous_time = now;

  double progress = (double)current / total;
  int barWidth = 40;

  // Print progress bar
  std::cout << "[";
  int pos = barWidth * progress;
  for (int i = 0; i < barWidth; ++i) {
    if (i < pos)
      std::cout << "=";
    else if (i == pos)
      std::cout << ">";
    else
      std::cout << " ";
  }
  std::cout << "] ";

  // Print percentage
  std::cout << std::fixed << std::setprecision(1) << progress * 100.0 << "% ";

  // Calculate estimated remaining time
  double elapsed = std::chrono::duration<double>(now - start_time).count();
  double eta = (elapsed / progress) - elapsed; // estimated remaining
  if (current == 0)
    eta = 0; // avoid division by zero

  std::cout << "ETA: " << std::fixed << std::setprecision(1) << eta << "s/"
            << elapsed << "s\r";
  std::cout.flush();
}
