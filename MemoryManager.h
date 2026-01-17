#pragma once
//#pragma pack(1)  <- if wan to mess ard with packing

#include <iostream>

#define UseCPPMemManager_ true

#if UseCPPMemManager_
#define USE_CUSTOM_ALLOCATOR \
		static void* operator new(std::size_t size) { \
			return mem::MemoryAllocator::GetInstance().allocate(size); \
		} \
		static void operator delete(void* ptr) noexcept { \
			if (ptr) mem::MemoryAllocator::GetInstance().deallocate(ptr); \
		} \
		static void* operator new[](std::size_t size) { \
			return mem::MemoryAllocator::GetInstance().allocate(size); \
		} \
		static void operator delete[](void* ptr) noexcept { \
			if (ptr) mem::MemoryAllocator::GetInstance().deallocate(ptr); \
		}
#else
#define USE_CUSTOM_ALLOCATOR
#endif

namespace mem {

	struct Block {
		size_t size;
		bool isFree;
		Block* next;
		Block* prev;
		int poolIdx; // to see which pool it belongs
	};

	struct BlockFooter {
		size_t size;
		bool isFree;
	};

	struct MemoryPool {
		void* start;
		void* end;
		size_t size;
		bool isExtension;
		bool isActive;
	};

	struct MAStats {
		MAStats(void) : poolSize{ 0 }, allocated{ 0 }, freeBytes{ 0 },
			totalAllocations{ 0 }, totalDeallocations{ 0 },
			extensionCount{ 0 }, activeExtensions{ 0 }, totalPoolSize{ 0 } {
		};

		size_t poolSize;
		size_t allocated;
		size_t freeBytes;
		size_t totalAllocations;
		size_t totalDeallocations;
		size_t extensionCount; // Total ever created
		size_t activeExtensions; // Currently active
		size_t totalPoolSize; // all pool + tgt
	};

	// segregated free list
	class MemoryAllocator {
	private:
		// Size Classes: 64, 128, 256, 512, 1024, 2048, 4096, 8192+ btyes
		static const int NUM_CLASSES = 8;
		static const size_t MIN_BLOCK_SIZE = 64;
		static const size_t MAX_BLOCK_SIZE = 8192;
		static const size_t HEADER_SIZE = sizeof(Block);
		static const size_t FOOTER_SIZE = sizeof(BlockFooter);
		static const size_t OVERHEAD = HEADER_SIZE + FOOTER_SIZE;
		static const int MAX_POOLS = 32;

	public:
		MemoryAllocator(size_t requestedSize);
		MemoryAllocator(MemoryAllocator const&) = delete;
		MemoryAllocator& operator=(MemoryAllocator const&) = delete;
		~MemoryAllocator(void);

		static MemoryAllocator& GetInstance();

		void* allocate(size_t size);
		void deallocate(void* obj);
		void optimizeMemory() { defrag(); }

		void poolSize(size_t); // to modify pool size
		void setAutoExtender(bool enable) { autoExtend = enable; }
		void setExtensionSize(size_t size) { extensionSize = size; }
		void setMaxPools(int max) { maxPools = (max <= MAX_POOLS) ? max : MAX_POOLS; }
		bool extendPool(size_t additionalSize = 0);
		size_t reclaimUnusedPools();
		size_t getReclaimableMemory() const;
		int getReclaimablePoolCount() const;

		void printStats(void);
		void checkForLeaks() const;
		bool hasLeaks() const { return stats.allocated > 0; }

	private:
		int getSizeClass(size_t size);
		size_t getClassSize(int classIdx);
		Block* findBlock(size_t totalSize);
		void splitBlock(Block* block, size_t size);

		void removeFromFreeList(Block* block);
		void addToFreeList(Block* block);
		Block* coalesce(Block* block);
		void defrag(void);

		BlockFooter* getFooter(void* blockStart, size_t blockSize);
		Block* getPrevBlock(Block* block);
		Block* getNextBlock(Block* block);
		bool isValidBlock(void* ptr);
		int getPoolIndex(void* ptr);

	private:
		Block* freeLists[NUM_CLASSES];
		MemoryPool pools[MAX_POOLS];
		int poolCnt;
		int maxPools;
		MAStats stats;

		bool autoExtend;
		size_t extensionSize; // default is 0
	};

}
