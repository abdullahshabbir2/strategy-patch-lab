#include <cstdint>
// Deliberately identifiable, owned patch site for reproducible binary analysis.
// x86-64 Windows host build, not TriCore machine code.
extern "C" __attribute__((naked,noinline,used,section(".patch"))) uint32_t strategy_features() {
    __asm__ volatile("mov $0x5A170000, %eax\n\tret");
}
