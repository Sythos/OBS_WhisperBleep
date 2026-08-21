// SPDX-License-Identifier: MIT
// SPDX-FileCopyrightText: 2026 Sythos (www.sythos.net)

#include <iostream>
#include <string>

int main() {
  std::string line;
  int request_count = 0;
  while (std::getline(std::cin, line)) {
    ++request_count;
    std::cout << "{\"status\":\"ready\",\"request_count\":"
              << request_count << ",\"echo\":\"" << line << "\"}"
              << std::endl;
  }
  return 0;
}
