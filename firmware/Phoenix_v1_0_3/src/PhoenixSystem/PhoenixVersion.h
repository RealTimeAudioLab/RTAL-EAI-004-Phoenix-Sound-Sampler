#pragma once
#define PHX_VERSION_STRING "1.0.3"
#define PHX_BUILD_CODE "FINAL SILENT"
#define PHX_BUILD_NAME "FINAL SILENT - Error-only Serial / Non-Blocking Disk Utilities & Bank Cache"
#define PHX_FULL_VERSION "Project Phoenix v" PHX_VERSION_STRING " " PHX_BUILD_CODE
#define PHX_BANK_FORMAT_VERSION 15
#define PHX_MULTISAMPLE_FORMAT_VERSION 10

// Phoenix v1.0.3 FINAL SILENT serial policy
// 0 = completely silent, 1 = errors + one boot version line, 2 = full development/info output
#ifndef PHX_SERIAL_LEVEL
#define PHX_SERIAL_LEVEL 1
#endif
