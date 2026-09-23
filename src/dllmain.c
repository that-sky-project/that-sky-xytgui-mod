#include <windows.h>

HMODULE hModuleDll = NULL;

BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID reserved) {
  (void)reserved;
  if (reason == DLL_PROCESS_ATTACH) {
    hModuleDll = h;
    DisableThreadLibraryCalls(h);
  }
  return TRUE;
}
