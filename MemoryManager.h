#pragma once
#include <cstdlib>
#include <exception>
#include <iostream>

namespace mem {

	struct Block {
		size_t size;
		bool isFree;
		Block* next;
		Block* prev;
	};

	struct BlockFooter {
		size_t size;
		bool isFree;
	};

	struct MMConfig {
		MMConfig(bool useCustom = false) : UseCPPMemManager{ useCustom } {};

		bool UseCPPMemManager;
	};

	struct MMStats {
		MMStats(void) : poolSize{ 0 }, allocated{ 0 }, freeBytes{ 0 } {};

		size_t poolSize;
		size_t allocated;
		size_t freeBytes;
	};

	// segregated free list
	class MemoryManager {
	private:
		// Size Classes: 64, 128, 256, 512, 1024, 2048, 4096, 8192+ btyes
		static const int NUM_CLASSES = 8;
		static const size_t MIN_BLOCK_SIZE = 64;
		static const size_t HEADER_SIZE = sizeof(Block);
		static const size_t FOOTER_SIZE = sizeof(BlockFooter);
		static const size_t OVERHEAD = HEADER_SIZE + FOOTER_SIZE;

	public:
		MemoryManager(MMConfig const& con, size_t size);
		~MemoryManager(void);

		void* allocate(size_t size);
		void deallocate(void* obj);

		void printStats(void);

	private:
		int getSizeClass(size_t size);
		size_t getClassSize(int classIdx);
		Block* findBlock(size_t totalSize);
		void splitBlock(Block* block, size_t size);

		void removeFromFreeList(Block* block);
		void addToFreeList(Block* block);
		Block* coalesce(Block* block);

		BlockFooter* getFooter(void* blockStart, size_t blockSize);
		Block* getPrevBlock(Block* block);
		Block* getNextBlock(Block* block);
		bool isValidBlock(void* ptr);

	private:
		MMConfig config;
		Block* freeLists[NUM_CLASSES];
		void* poolStart;
		void* poolEnd;
		MMStats stats;
	};
}
