#include "TestHarness.h"
#include <windows.h>

int main() {
    // Enable UTF-8 console output for crisp characters
    SetConsoleOutputCP(CP_UTF8);
    return TestFramework::TestRegistry::Instance().RunAll();
}
