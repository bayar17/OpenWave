#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace appdeploy::util {

std::string json_escape(std::string_view text);
std::vector<std::string> split(std::string_view text, char separator);
bool starts_with(std::string_view text, std::string_view prefix);
bool ends_with(std::string_view text, std::string_view suffix);

std::string strict_absolute(std::string_view path);
std::string resolve_link(std::string_view link_path, std::string_view target);
bool within(std::string_view path, std::string_view directory);

int open_directory_nofollow(const std::string &path, int flags = 0);
std::string read_regular_file(int directory_fd, const char *name, std::size_t limit, std::uint32_t owner = 0, bool check_owner = true);
void write_file_atomic(int directory_fd, const char *name, const std::string &data, unsigned mode);
void remove_tree(int parent_fd, const char *name);
std::uint64_t now_seconds();

std::string errno_text(int error);

}
