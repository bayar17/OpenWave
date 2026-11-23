#pragma once
// Standalone Qt adapter: only inline WTF helpers and public WPE APIs are used.
// The engine stays unchanged; -z,defs rejects unresolved adapter symbols.
#define BUILDING_WPE__ 1
#define WTF_PLATFORM_WPE 1
#define USE_GLIB 1
#define USE_SOUP 1
#define USE_SYSTEM_MALLOC 1
#define ENABLE_2022_GLIB_API 1
#define ENABLE_WPE_PLATFORM 1
#define BUILDING_WebKit 1
#include <wtf/Platform.h>
#include <wtf/Assertions.h>

#include <new>
