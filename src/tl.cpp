#include "../dep/tl/include/tl/system.h"

#include "../dep/freetype/include/freetype/freetype.h"
#if ARCH_X64
#pragma comment(lib, "dep/freetype/win64/freetype.lib")
#else
#pragma comment(lib, "dep/freetype/win32/freetype.lib")
#endif

#define TL_IMPL
#define TL_MAIN
#include "../dep/tl/include/tl/profiler.h"
#include "../dep/tl/include/tl/common.h"
#include "../dep/tl/include/tl/window.h"
#include "../dep/tl/include/tl/opengl.h"
#include "../dep/tl/include/tl/font.h"
