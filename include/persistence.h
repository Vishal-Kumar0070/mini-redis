#pragma once
#include "store.h"
#include <string>

// Save current store to a file (snapshot)
void save_snapshot(Store& store, const std::string& filepath);

// Load snapshot from file into store on startup
void load_snapshot(Store& store, const std::string& filepath);
