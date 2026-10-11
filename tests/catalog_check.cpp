#include "adapters/catalog_json.h"
#include <array>
#include <iostream>

int main() {
  std::array<char, 4096> buffer{};
  std::string body;
  while (std::cin) {
    std::cin.read(buffer.data(), buffer.size());
    const auto count = static_cast<std::size_t>(std::cin.gcount());
    if (count > vibemon::MaxResponseBytes - body.size()) { std::cerr << "Response exceeds device limit\n"; return 1; }
    body.append(buffer.data(), count);
  }
  vibemon::Catalog catalog;
  const auto result = vibemon::decodeCatalog(body, catalog);
  if (!result.ok) { std::cerr << result.message << '\n'; return 1; }
  std::size_t resources = 0, accounts = 0;
  for (const auto& item : catalog.items) {
    if (item.selection.kind == vibemon::ItemKind::Resource) ++resources;
    else ++accounts;
  }
  std::cout << "{\"ok\":true,\"resources\":" << resources << ",\"accounts\":" << accounts << "}\n";
}
