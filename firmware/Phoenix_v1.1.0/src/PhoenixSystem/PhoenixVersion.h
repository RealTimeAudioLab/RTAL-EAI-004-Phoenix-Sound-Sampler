#pragma once
#define PHX_VERSION_STRING "1.1.0"
#define PHX_BUILD_CODE "PHX110-FINAL-SD10-NBEMPTYFIX1"
#define PHX_BUILD_NAME "Phoenix v1.1.0 Final"
#define PHX_FULL_VERSION "Project Phoenix v" PHX_VERSION_STRING " " PHX_BUILD_CODE
#define PHX_BANK_FORMAT_VERSION 16
#define PHX_MULTISAMPLE_FORMAT_VERSION 10

// Phoenix v1.1.0 FINAL serial policy
// 0 = completely silent release build
// 1 = errors + one boot version line
// 2 = full development/info output
#ifndef PHX_SERIAL_LEVEL
#define PHX_SERIAL_LEVEL 0
#endif
