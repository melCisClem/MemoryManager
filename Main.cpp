#include "MemoryManager.h"
#include "Benchmark.h"

#include <fstream>
#include <sstream>
#include <filesystem>

#define VERBOSE_TEST 1
#define TEST 1

void CompileFromFile(std::string const& filepath)
{
    std::ifstream shader_file(filepath);
    if (!shader_file) {
        std::cout <<  "Error opening file " + filepath;
        return;
    }
    std::stringstream buffer;
    buffer << shader_file.rdbuf();
    shader_file.close();

    std::string const& s = buffer.str();
    char const* shader_code[] = { s.c_str() };
}

int main(void)
{
    try {
#if TEST 
        std::cout << "Testing crash \n";
        std::cout << "Current working directory: " << std::filesystem::current_path() << std::endl;
        CompileFromFile("../TestAssets/GUI.vert");
        CompileFromFile("../TestAssets/GUI.frag");
        CompileFromFile("../TestAssets/Line.vert");
        CompileFromFile("../TestAssets/Line.frag");
        CompileFromFile("../TestAssets/Screen.vert");
        CompileFromFile("../TestAssets/Screen.frag");
        CompileFromFile("../TestAssets/Sprite.vert");
        CompileFromFile("../TestAssets/Sprite.frag");
        std::cout << '\n';
#else
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
