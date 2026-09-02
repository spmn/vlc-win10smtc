#pragma once

#include <cstddef>
#include <vector>

// Largest image in the host executable's icon, as encoded bytes. Empty if it has none.
std::vector<std::byte> ReadHostIcon();
