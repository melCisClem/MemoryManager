/*!
@file Main.cpp
@author ClaudeAI
@date 17th january 2026 17/01/2026
@brief Main entry point for all memory allocator tests
*/

#include "MemoryManager.h"
#include "Benchmark.h"
#include <iostream>
#include <vector>

// Test class for extension tests
class TestObject {
public:
    USE_CUSTOM_ALLOCATOR

        TestObject(int val = 0) : value(val) {
        for (int i = 0; i < 100; ++i) {
            data[i] = val * i;
        }
    }

    int getValue() const { return value; }

private:
    int value;
    int data[100];  // ~400 bytes per object
};

// Forward declarations
void runSimpleExtensionTest();
void runExtensiveExtensionTests();
void runBenchmarkTests();
void runVerboseTest();
void runPoolReclamationTest();
void printMenu();

int main(void)
{
    try {
        int choice = 0;

        while (true) {
            printMenu();
            std::cout << "\nEnter choice (1-6, 0 to exit): ";
            std::cin >> choice;
            std::cin.ignore(); // Clear newline

            switch (choice) {
            case 0:
                std::cout << "\nExiting...\n";
                return 0;

            case 1:
                runSimpleExtensionTest();
                break;

            case 2:
                runExtensiveExtensionTests();
                break;

            case 3:
                runBenchmarkTests();
                break;

            case 4:
                runVerboseTest();
                break;

            case 5:
                runPoolReclamationTest();
                break;

            case 6:
                // Run all tests
                std::cout << "\n\\n========== RUNNING ALL TESTS ==========\n\n";
                runVerboseTest();
                runSimpleExtensionTest();
                runExtensiveExtensionTests();
                runPoolReclamationTest();
                runBenchmarkTests();
                std::cout << "\n\n========== ALL TESTS COMPLETE ==========\n\n";
                break;

            default:
                std::cout << "\nInvalid choice!\n";
                break;
            }

            std::cout << "\nPress Enter to continue...";
            std::cin.get();
        }
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}

void printMenu() {
    std::cout << "\n╔═══════════════════════════════════════════════════════╗\n";
    std::cout << "║     Memory Allocator Test Suite                      ║\n";
    std::cout << "╠═══════════════════════════════════════════════════════╣\n";
    std::cout << "║ 1. Simple Extension Test                             ║\n";
    std::cout << "║ 2. Extensive Extension Tests (All 7 Tests)           ║\n";
    std::cout << "║ 3. Performance Benchmark (vs stdlib)                 ║\n";
    std::cout << "║ 4. Verbose Debug Test                                ║\n";
    std::cout << "║ 5. Pool Reclamation Test                             ║\n";
    std::cout << "║ 6. Run ALL Tests                                     ║\n";
    std::cout << "║ 0. Exit                                               ║\n";
    std::cout << "╚═══════════════════════════════════════════════════════╝\n";
}

void runSimpleExtensionTest() {
    std::cout << "\n========== SIMPLE EXTENSION TEST ==========\n";

    auto& allocator = mem::MemoryAllocator::GetInstance();

    std::cout << "=== Initial State ===\n";
    allocator.printStats();

    std::vector<void*> allocations;
    const size_t blockSize = 20 * 1024 * 1024; // 20 MB per block
    const int numBlocks = 10; // Total: 200 MB

    std::cout << "\n=== Allocating " << numBlocks << " blocks of "
        << blockSize << " bytes each ===\n";
    std::cout << "Total allocation: " << (numBlocks * blockSize) / (1024 * 1024)
        << " MB (should trigger extension)\n\n";

    for (int i = 0; i < numBlocks; ++i) {
        std::cout << "Allocating block " << (i + 1) << "... ";
        try {
            void* ptr = allocator.allocate(blockSize);
            allocations.push_back(ptr);
            std::cout << "Success\n";
        }
        catch (const std::bad_alloc&) {
            std::cout << "Failed!\n";
            break;
        }
    }

    std::cout << "\n=== After Allocations ===\n";
    allocator.printStats();

    std::cout << "\n=== Deallocating all blocks ===\n";
    for (void* ptr : allocations) {
        allocator.deallocate(ptr);
    }

    std::cout << "\n=== After Deallocation ===\n";
    allocator.printStats();
}

void runExtensiveExtensionTests() {
    std::cout << "\n========== EXTENSIVE EXTENSION TESTS ==========\n";

    auto& allocator = mem::MemoryAllocator::GetInstance();

    // Test 1: Basic Extension
    {
        std::cout << "\n=== Test 1: Basic Extension ===\n";
        std::vector<void*> allocations;
        const size_t blockSize = 10 * 1024 * 1024;
        const int numBlocks = 15;

        for (int i = 0; i < numBlocks; ++i) {
            try {
                void* ptr = allocator.allocate(blockSize);
                allocations.push_back(ptr);
            }
            catch (const std::bad_alloc&) {
                std::cout << "Allocation failed at block " << i + 1 << "\n";
                break;
            }
        }
        allocator.printStats();
        for (void* ptr : allocations) allocator.deallocate(ptr);
    }

    // Test 2: Manual Extension
    {
        std::cout << "\n=== Test 2: Manual Extension ===\n";
        allocator.extendPool(200 * 1024 * 1024);
        allocator.printStats();
    }

    // Test 3: Auto-Extension Off (FIXED VERSION)
    {
        std::cout << "\n=== Test 3: Auto-Extension Disabled ===\n";
        allocator.setAutoExtender(false);

        // First, make sure we exhaust available memory
        std::vector<void*> fillUp;
        try {
            // Allocate everything available
            while (true) {
                void* ptr = allocator.allocate(10 * 1024 * 1024); // 10MB chunks
                fillUp.push_back(ptr);
            }
        }
        catch (const std::bad_alloc&) {
            // Expected - ran out of memory
        }

        std::cout << "Filled up available memory with " << fillUp.size() << " allocations\n";

        // NOW try to allocate more - should fail
        try {
            void* ptr = allocator.allocate(50 * 1024 * 1024); // 50 MB
            std::cout << "ERROR: Unexpected success - auto-extend should be disabled!\n";
            allocator.deallocate(ptr);
        }
        catch (const std::bad_alloc&) {
            std::cout << "Failed as expected (auto-extend disabled)\n";
        }

        // Clean up
        for (void* ptr : fillUp) {
            allocator.deallocate(ptr);
        }

        allocator.setAutoExtender(true);
        allocator.printStats();
    }

    // Test 4: Multiple Extensions
    {
        std::cout << "\n=== Test 4: Multiple Extensions ===\n";
        std::vector<void*> allocations;
        const size_t blockSize = 30 * 1024 * 1024;

        for (int i = 0; i < 20; ++i) {
            try {
                void* ptr = allocator.allocate(blockSize);
                allocations.push_back(ptr);
            }
            catch (const std::bad_alloc&) {
                break;
            }
        }
        allocator.printStats();
        for (void* ptr : allocations) allocator.deallocate(ptr);
    }

    // Test 5: With Custom Objects
    {
        std::cout << "\n=== Test 5: Extension with Custom Objects ===\n";
        std::vector<TestObject*> objects;

        for (int i = 0; i < 50000; ++i) {
            try {
                objects.push_back(new TestObject(i));
            }
            catch (const std::bad_alloc&) {
                break;
            }
        }
        std::cout << "Created " << objects.size() << " objects\n";
        allocator.printStats();
        for (TestObject* obj : objects) delete obj;
    }

    // Test 6: Max Pools Limit
    {
        std::cout << "\n=== Test 6: Max Pools Limit ===\n";
        allocator.setMaxPools(3);
        std::vector<void*> allocations;

        for (int i = 0; i < 10; ++i) {
            try {
                void* ptr = allocator.allocate(50 * 1024 * 1024);
                allocations.push_back(ptr);
            }
            catch (const std::bad_alloc&) {
                std::cout << "Hit max pools limit at block " << i + 1 << "\n";
                break;
            }
        }
        allocator.printStats();
        for (void* ptr : allocations) allocator.deallocate(ptr);
        allocator.setMaxPools(32);
    }

    std::cout << "\n=== All Extension Tests Complete ===\n";
}


void runPoolReclamationTest() {
    std::cout << "\n========== POOL RECLAMATION TEST ==========\n";

    auto& allocator = mem::MemoryAllocator::GetInstance();

    std::cout << "\n=== Step 1: Initial State ===\n";
    allocator.printStats();

    // Create lots of allocations to trigger extensions
    std::vector<void*> allocations;
    std::cout << "\n=== Step 2: Allocating 300MB to trigger extensions ===\n";

    for (int i = 0; i < 30; ++i) {
        try {
            void* ptr = allocator.allocate(10 * 1024 * 1024); // 10MB each
            allocations.push_back(ptr);
        }
        catch (const std::bad_alloc&) {
            std::cout << "Allocation failed at block " << i + 1 << "\n";
            break;
        }
    }

    allocator.printStats();

    // Free everything
    std::cout << "\n=== Step 3: Freeing all allocations ===\n";
    for (void* ptr : allocations) {
        allocator.deallocate(ptr);
    }
    allocations.clear();

    allocator.printStats();

    // Show reclaimable info
    std::cout << "\n=== Step 4: Checking for reclaimable pools ===\n";
    size_t reclaimable = allocator.getReclaimableMemory();
    int poolCount = allocator.getReclaimablePoolCount();
    std::cout << "Found " << poolCount << " empty extension pools\n";
    std::cout << "Total reclaimable: " << reclaimable << " bytes ("
        << reclaimable / (1024 * 1024) << " MB)\n";

    // Reclaim unused pools
    std::cout << "\n=== Step 5: Reclaiming unused pools ===\n";
    size_t reclaimed = allocator.reclaimUnusedPools();
    std::cout << "Successfully reclaimed " << reclaimed << " bytes ("
        << reclaimed / (1024 * 1024) << " MB)\n";

    allocator.printStats();

    // Test that allocator still works after reclamation
    std::cout << "\n=== Step 6: Testing allocator after reclamation ===\n";
    std::vector<void*> newAllocs;

    for (int i = 0; i < 5; ++i) {
        void* ptr = allocator.allocate(1024 * 1024); // 1MB each
        newAllocs.push_back(ptr);
    }

    std::cout << "Successfully allocated 5MB after reclamation\n";
    allocator.printStats();

    // Cleanup
    for (void* ptr : newAllocs) {
        allocator.deallocate(ptr);
    }

    std::cout << "\n=== Pool Reclamation Test Complete ===\n";
}

void runBenchmarkTests() {
    std::cout << "\n========== PERFORMANCE BENCHMARK ==========\n";
    GameEngineBenchmark::runAllTests();
}

void runVerboseTest() {
    std::cout << "\n========== VERBOSE DEBUG TEST ==========\n";

    mem::MMConfig config(true);
    mem::MemoryManager allocator(config, 4096);

    std::cout << "Initial state:\n";
    allocator.printStats();

    void* p1 = allocator.allocate(50);
    void* p2 = allocator.allocate(200);
    void* p3 = allocator.allocate(15);

    std::cout << "\nAfter allocations:\n";
    allocator.printStats();

    allocator.deallocate(p2);

    std::cout << "\nAfter freeing p2:\n";
    allocator.printStats();

    void* p4 = allocator.allocate(180);

    std::cout << "\nAfter allocating p4:\n";
    allocator.printStats();

    allocator.deallocate(p1);
    allocator.deallocate(p3);
    allocator.deallocate(p4);

    std::cout << "\nAfter freeing all (fragmented):\n";
    allocator.printStats();

    std::cout << "\nAfter defragmentation:\n";
    allocator.printStats();
}