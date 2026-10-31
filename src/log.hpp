#pragma once

void log_open(void* module);
void log_close();
void log(const char* fmt, ...);
