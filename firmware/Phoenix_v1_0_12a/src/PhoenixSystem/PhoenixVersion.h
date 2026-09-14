#pragma once
#define PHX_VERSION_STRING "1.0.12a"
#define PHX_BUILD_CODE "ACTIVE BANK + ENCODER"
#define PHX_BUILD_NAME "Active Bank Persistence & Encoder Bank Manager"
#define PHX_FULL_VERSION "Project Phoenix v" PHX_VERSION_STRING " " PHX_BUILD_CODE
#define PHX_BANK_FORMAT_VERSION 15
#define PHX_MULTISAMPLE_FORMAT_VERSION 10

// Phoenix v1.0.12a Robust Last Bank Restore serial policy
// 0 = completely silent, 1 = errors + one boot version line, 2 = full development/info output
#ifndef PHX_SERIAL_LEVEL
#define PHX_SERIAL_LEVEL 1
#endif
