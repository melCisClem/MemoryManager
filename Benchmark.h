/*!
@file Benchmark.h
@author ClaudeAI
@date 10th january 2026 10/01/2026
@brief Game Engine focused memory allocation benchmark
*/

#pragma once

#include "MemoryManager.h"
#include <chrono>
#include <vector>
#include <random>
#include <iomanip>


namespace mem {
    // Config struct for API compatibility
    struct MMConfig {
        bool verbose;
        MMConfig(bool v = false) : verbose(v) {}
    };

    // Wrapper class that forwards to the singleton MemoryAllocator
    class MemoryManager {
    public:
        MemoryManager(const MMConfig& config, size_t size) {
            // The actual allocator is a singleton, so we just get reference
            // Note: size parameter ignored since singleton is already initialized
            verbose = config.verbose;
        }

        void* allocate(size_t size) {
            return MemoryAllocator::GetInstance().allocate(size);
        }

        void deallocate(void* ptr) {
            MemoryAllocator::GetInstance().deallocate(ptr);
        }

        void optimizeMemory() {
            MemoryAllocator::GetInstance().optimizeMemory();
        }

        void printStats() {
            MemoryAllocator::GetInstance().printStats();
        }

        void checkForLeaks() const {
            MemoryAllocator::GetInstance().checkForLeaks();
        }

        bool hasLeaks() const {
            return MemoryAllocator::GetInstance().hasLeaks();
        }

    private:
        bool verbose;
    };
}

using namespace std::chrono;

// Common game engine structures
struct Transform {
    float position[3];
    float rotation[4];  // quaternion
    float scale[3];
};

struct Entity {
    int id;
    Transform transform;
    int componentMask;
    void* userData;
};

struct Particle {
    float position[3];
    float velocity[3];
    float color[4];
    float lifetime;
    float size;
};

struct AudioSource {
    int soundId;
    float position[3];
    float volume;
    bool isPlaying;
    char padding[64];  // Simulate audio buffer metadata
};

class GameEngineBenchmark {
public:
    static void runAllTests() {
        std::cout << "\n=== Memory Allocator Benchmark Custom Vs StdLib ===\n\n";

        testFrameAllocations(10000);
        testEntityLifecycle(50000);
        testParticleSystem(100000);
        testAudioSources(100000);
        testLevelLoading(10);
        testRealisticGameLoop(3600);

        std::cout << "\n=== Benchmark Complete ===\n";
    }

private:
    static void testFrameAllocations(int frameCount) {
        std::cout << "Test 1: Frame Allocations (" << frameCount << " frames)\n";

        auto startCustom = high_resolution_clock::now();
        {
            mem::MMConfig config(false);
            mem::MemoryManager allocator(config, 1024 * 1024 * 50);

            for (int frame = 0; frame < frameCount; frame++) {
                std::vector<void*> frameAllocs;
                for (int i = 0; i < 50; i++) {
                    void* ptr = allocator.allocate(128);
                    if (ptr) frameAllocs.push_back(ptr);
                }
                for (void* ptr : frameAllocs) {
                    allocator.deallocate(ptr);
                }
            }
        }
        auto endCustom = high_resolution_clock::now();
        auto durationCustom = duration_cast<microseconds>(endCustom - startCustom);

        auto startStd = high_resolution_clock::now();
        {
            for (int frame = 0; frame < frameCount; frame++) {
                std::vector<char*> frameAllocs;
                for (int i = 0; i < 50; i++) {
                    frameAllocs.push_back(new char[128]);
                }
                for (char* ptr : frameAllocs) {
                    delete[] ptr;
                }
            }
        }
        auto endStd = high_resolution_clock::now();
        auto durationStd = duration_cast<microseconds>(endStd - startStd);

        printResults("Frame Allocations", durationCustom, durationStd, frameCount);
    }

    static void testEntityLifecycle(int totalEntities) {
        std::cout << "\nTest 2: Entity Lifecycle (spawn/despawn waves)\n";

        auto startCustom = high_resolution_clock::now();
        {
            mem::MMConfig config(false);
            mem::MemoryManager allocator(config, 1024 * 1024 * 50);

            std::vector<void*> activeEntities;
            int spawned = 0;

            while (spawned < totalEntities) {
                int waveSize = 100;
                for (int i = 0; i < waveSize && spawned < totalEntities; i++) {
                    void* entity = allocator.allocate(sizeof(Entity));
                    if (entity) {
                        activeEntities.push_back(entity);
                        spawned++;
                    }
                }

                if (activeEntities.size() > 200) {
                    int despawnCount = 50;
                    for (int i = 0; i < despawnCount && !activeEntities.empty(); i++) {
                        allocator.deallocate(activeEntities.back());
                        activeEntities.pop_back();
                    }
                }
            }

            for (void* entity : activeEntities) {
                allocator.deallocate(entity);
            }
        }
        auto endCustom = high_resolution_clock::now();
        auto durationCustom = duration_cast<microseconds>(endCustom - startCustom);

        auto startStd = high_resolution_clock::now();
        {
            std::vector<Entity*> activeEntities;
            int spawned = 0;

            while (spawned < totalEntities) {
                int waveSize = 100;
                for (int i = 0; i < waveSize && spawned < totalEntities; i++) {
                    activeEntities.push_back(new Entity());
                    spawned++;
                }

                if (activeEntities.size() > 200) {
                    int despawnCount = 50;
                    for (int i = 0; i < despawnCount && !activeEntities.empty(); i++) {
                        delete activeEntities.back();
                        activeEntities.pop_back();
                    }
                }
            }

            for (Entity* entity : activeEntities) {
                delete entity;
            }
        }
        auto endStd = high_resolution_clock::now();
        auto durationStd = duration_cast<microseconds>(endStd - startStd);

        printResults("Entity Lifecycle", durationCustom, durationStd, totalEntities);
    }

    static void testParticleSystem(int particleCount) {
        std::cout << "\nTest 3: Particle System (burst spawning)\n";

        auto startCustom = high_resolution_clock::now();
        {
            mem::MMConfig config(false);
            mem::MemoryManager allocator(config, 1024 * 1024 * 50);

            std::vector<void*> particles;

            for (int burst = 0; burst < 10; burst++) {
                for (int i = 0; i < particleCount / 10; i++) {
                    void* particle = allocator.allocate(sizeof(Particle));
                    if (particle) particles.push_back(particle);
                }

                int deathCount = (int)particles.size() / 3;
                for (int i = 0; i < deathCount; i++) {
                    allocator.deallocate(particles[i]);
                }
                particles.erase(particles.begin(), particles.begin() + deathCount);
            }

            for (void* particle : particles) {
                allocator.deallocate(particle);
            }
        }
        auto endCustom = high_resolution_clock::now();
        auto durationCustom = duration_cast<microseconds>(endCustom - startCustom);

        auto startStd = high_resolution_clock::now();
        {
            std::vector<Particle*> particles;

            for (int burst = 0; burst < 10; burst++) {
                for (int i = 0; i < particleCount / 10; i++) {
                    particles.push_back(new Particle());
                }

                int deathCount = (int)particles.size() / 3;
                for (int i = 0; i < deathCount; i++) {
                    delete particles[i];
                }
                particles.erase(particles.begin(), particles.begin() + deathCount);
            }

            for (Particle* particle : particles) {
                delete particle;
            }
        }
        auto endStd = high_resolution_clock::now();
        auto durationStd = duration_cast<microseconds>(endStd - startStd);

        printResults("Particle Bursts", durationCustom, durationStd, particleCount);
    }

    static void testAudioSources(int operations) {
        std::cout << "\nTest 4: Audio Source Pool (dynamic audio)\n";

        auto startCustom = high_resolution_clock::now();
        {
            mem::MMConfig config(false);
            mem::MemoryManager allocator(config, 1024 * 1024 * 50);

            std::vector<void*> activeSources;

            for (int i = 0; i < operations; i++) {
                if (activeSources.size() < 32) {
                    void* source = allocator.allocate(sizeof(AudioSource));
                    if (source) activeSources.push_back(source);
                }

                if (!activeSources.empty() && (i % 10 == 0)) {
                    allocator.deallocate(activeSources.front());
                    activeSources.front() = activeSources.back();
                    activeSources.pop_back();
                }
            }

            for (void* source : activeSources) {
                allocator.deallocate(source);
            }
        }
        auto endCustom = high_resolution_clock::now();
        auto durationCustom = duration_cast<microseconds>(endCustom - startCustom);

        auto startStd = high_resolution_clock::now();
        {
            std::vector<AudioSource*> activeSources;

            for (int i = 0; i < operations; i++) {
                if (activeSources.size() < 32) {
                    activeSources.push_back(new AudioSource());
                }

                if (!activeSources.empty() && (i % 10 == 0)) {
                    delete activeSources.front();
                    activeSources.front() = activeSources.back();
                    activeSources.pop_back();
                }
            }

            for (AudioSource* source : activeSources) {
                delete source;
            }
        }
        auto endStd = high_resolution_clock::now();
        auto durationStd = duration_cast<microseconds>(endStd - startStd);

        printResults("Audio Source Pool", durationCustom, durationStd, operations);
    }

    static void testLevelLoading(int levelCount) {
        std::cout << "\nTest 5: Level Load/Unload (bulk operations)\n";

        auto startCustom = high_resolution_clock::now();
        {
            mem::MMConfig config(false);
            mem::MemoryManager allocator(config, 1024 * 1024 * 50);

            for (int level = 0; level < levelCount; level++) {
                std::vector<void*> levelData;

                for (int i = 0; i < 500; i++) {
                    void* entity = allocator.allocate(sizeof(Entity));
                    if (entity) levelData.push_back(entity);
                }

                for (int i = 0; i < 1000; i++) {
                    void* obj = allocator.allocate(64);
                    if (obj) levelData.push_back(obj);
                }

                for (int i = 0; i < 200; i++) {
                    void* data = allocator.allocate(256);
                    if (data) levelData.push_back(data);
                }

                for (void* ptr : levelData) {
                    allocator.deallocate(ptr);
                }
            }
        }
        auto endCustom = high_resolution_clock::now();
        auto durationCustom = duration_cast<microseconds>(endCustom - startCustom);

        auto startStd = high_resolution_clock::now();
        {
            for (int level = 0; level < levelCount; level++) {
                std::vector<void*> levelData;

                for (int i = 0; i < 500; i++) {
                    levelData.push_back(new Entity());
                }

                for (int i = 0; i < 1000; i++) {
                    levelData.push_back(new char[64]);
                }

                for (int i = 0; i < 200; i++) {
                    levelData.push_back(new char[256]);
                }

                for (void* ptr : levelData) {
                    delete ptr;
                }
            }
        }
        auto endStd = high_resolution_clock::now();
        auto durationStd = duration_cast<microseconds>(endStd - startStd);

        printResults("Level Load/Unload", durationCustom, durationStd, levelCount);
    }

    static void testRealisticGameLoop(int frameCount) {
        std::cout << "\nTest 6: Realistic Game Loop (60 frames)\n";

        auto startCustom = high_resolution_clock::now();
        {
            mem::MMConfig config(false);
            mem::MemoryManager allocator(config, 1024 * 1024 * 50);

            std::vector<void*> entities;
            std::vector<void*> particles;
            std::vector<void*> tempBuffers;

            for (int frame = 0; frame < frameCount; frame++) {
                if (frame % 10 == 0 && entities.size() < 200) {
                    for (int i = 0; i < 5; i++) {
                        void* entity = allocator.allocate(sizeof(Entity));
                        if (entity) entities.push_back(entity);
                    }
                }

                if (frame % 5 == 0) {
                    for (int i = 0; i < 20; i++) {
                        void* particle = allocator.allocate(sizeof(Particle));
                        if (particle) particles.push_back(particle);
                    }
                }

                for (int i = 0; i < 10; i++) {
                    void* temp = allocator.allocate(128);
                    if (temp) tempBuffers.push_back(temp);
                }

                if (particles.size() > 100) {
                    for (int i = 0; i < 30; i++) {
                        allocator.deallocate(particles.back());
                        particles.pop_back();
                    }
                }

                for (void* temp : tempBuffers) {
                    allocator.deallocate(temp);
                }
                tempBuffers.clear();
            }

            for (void* e : entities) allocator.deallocate(e);
            for (void* p : particles) allocator.deallocate(p);
        }
        auto endCustom = high_resolution_clock::now();
        auto durationCustom = duration_cast<microseconds>(endCustom - startCustom);

        auto startStd = high_resolution_clock::now();
        {
            std::vector<Entity*> entities;
            std::vector<Particle*> particles;
            std::vector<char*> tempBuffers;

            for (int frame = 0; frame < frameCount; frame++) {
                if (frame % 10 == 0 && entities.size() < 200) {
                    for (int i = 0; i < 5; i++) {
                        entities.push_back(new Entity());
                    }
                }

                if (frame % 5 == 0) {
                    for (int i = 0; i < 20; i++) {
                        particles.push_back(new Particle());
                    }
                }

                for (int i = 0; i < 10; i++) {
                    tempBuffers.push_back(new char[128]);
                }

                if (particles.size() > 100) {
                    for (int i = 0; i < 30; i++) {
                        delete particles.back();
                        particles.pop_back();
                    }
                }

                for (char* temp : tempBuffers) {
                    delete[] temp;
                }
                tempBuffers.clear();
            }

            for (Entity* e : entities) delete e;
            for (Particle* p : particles) delete p;
        }
        auto endStd = high_resolution_clock::now();
        auto durationStd = duration_cast<microseconds>(endStd - startStd);

        printResults("Game Loop", durationCustom, durationStd, frameCount);
    }

    static void printResults(const std::string& testName,
        microseconds customTime,
        microseconds stdTime,
        int operations = 0) {
        double ratio = static_cast<double>(customTime.count()) / stdTime.count();
        double improvement = ((stdTime.count() - customTime.count()) /
            static_cast<double>(stdTime.count())) * 100.0;

        std::cout << std::fixed << std::setprecision(2);
        std::cout << "  " << std::setw(20) << std::left << testName << ": ";
        std::cout << "Custom=" << std::setw(8) << customTime.count() << "us  ";
        std::cout << "Std=" << std::setw(8) << stdTime.count() << "us  ";

        if (ratio < 1.0) {
            std::cout << std::setw(6) << std::abs(improvement) << "% faster";
        }
        else {
            std::cout << std::setw(6) << std::abs(improvement) << "% slower";
        }

        if (operations > 0) {
            double customPerOp = static_cast<double>(customTime.count()) / operations;
            double stdPerOp = static_cast<double>(stdTime.count()) / operations;
            std::cout << "  (" << customPerOp << " vs " << stdPerOp << " us/op)";
        }

        std::cout << "\n";
    }
};