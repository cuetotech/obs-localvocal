#pragma once
#include <cstdlib>
constexpr int LOG_ERROR = 100, LOG_WARNING = 200, LOG_INFO = 300, LOG_DEBUG = 400;
char *obs_module_config_path(const char *);
inline void bfree(void *p)
{
	std::free(p);
}
