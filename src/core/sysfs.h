// Utilitários para ler /proc e /sys.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace linosd::sysfs {

std::optional<std::string> readText(const std::string& path);  // conteúdo sem '\n' final
std::optional<int64_t> readInt(const std::string& path);
std::optional<double> readDouble(const std::string& path);
std::vector<std::string> listDir(const std::string& path);      // nomes (sem . e ..)
bool exists(const std::string& path);
std::string trim(const std::string& s);
bool startsWith(const std::string& s, const std::string& prefix);

// Procura por pci.ids e devolve "Vendor Device" (ou vazio).
std::string pciName(uint32_t vendor, uint32_t device);

}  // namespace linosd::sysfs
