#pragma once

// The core of the UI runtime is plain C++ (no engine header): the module SwcUi builds it for the client, and
// tools/test-ui.sh builds it with the system compiler. The engine hides the functions of a module by default
// (-fvisibility-ms-compat): the classes that the game module uses have this mark.
#if defined(__GNUC__) || defined(__clang__)
#define SWCUI_EXPORT __attribute__((visibility("default")))
#else
#define SWCUI_EXPORT
#endif
