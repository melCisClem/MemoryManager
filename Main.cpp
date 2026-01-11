#include "MemoryManager.h"
#include "Benchmark.h"

#define VERBOSE_TEST 0;

int main(void)
{
    try {

#if VERBOSE_TEST
        mem::MMConfig MemConfig(true);
        mem::MemoryManager allocator(MemConfig, 4096);

        std::cout << "Initial state:\n";
        allocator.printStats();

        void* p1 = allocator.allocate(50);
        void* p2 = allocator.allocate(200);
        void* p3 = allocator.allocate(15);

        std::cout << "\n\nAfter allocations:\n";
        allocator.printStats();

        allocator.deallocate(p2);

        std::cout << "\n\nAfter freeing p2:\n";
        allocator.printStats();

        void* p4 = allocator.allocate(180);

        std::cout << "\n\nAfter allocating p4:\n";
        allocator.printStats();

        allocator.deallocate(p1);
        allocator.deallocate(p3);
        allocator.deallocate(p4);

        std::cout << "\n\nAfter freeing all (fragmented):\n";
        allocator.printStats();

        allocator.optimizeMemory();
        std::cout << "\n\nAfter defragmentation:\n";
        allocator.printStats();
#else
        GameEngineBenchmark::runAllTests();
#endif
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "Press Enter to continue...";
    std::cin.get();

    return 0;
}
