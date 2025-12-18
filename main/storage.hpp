#pragma once
#include "storage_manager.hpp"
#include "esp_err.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include <string>
#include "esp_heap_caps.h"
#include <stdio.h>
// #include <dirent.h>
#include <cstring>
#include <cstddef>
#include <fstream>
#include <vector>
#include <algorithm>

namespace Storage {
    esp_err_t init();
    esp_err_t saveGlobalConfigFile(GlobalConfig* cfg);
    esp_err_t saveCredentialConfigFile(CredentialConfig* cd_cfg);
}