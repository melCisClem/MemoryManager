#pragma once

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
		Block* next;
		Block* prev;
		int poolIdx;
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
		size_t allocated; // bytes
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
		static const size_t POOL_ALIGNMENT = 4096;
		static const int NUM_CLASSES = 22;
		static const int OVERFLOW_CLASS = 21;
		static const size_t MIN_BLOCK_SIZE = 64; // this cannot be less than overhead (40bytes)
		static const size_t MAX_BLOCK_SIZE = 8388608; // 8mb
		static const size_t HEADER_SIZE = sizeof(Block);
		static const int MAX_POOLS = 128;

	public:
		MemoryAllocator(size_t requestedSize = 0);
		MemoryAllocator(MemoryAllocator const&) = delete;
		MemoryAllocator& operator=(MemoryAllocator const&) = delete;
		~MemoryAllocator(void);

		static MemoryAllocator& GetInstance();

		void initializeSegregatedLists(void* poolStart, size_t poolSize, int poolIdx);

		void* allocate(size_t size, bool print = true);
		void deallocate(void* obj);

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

		void removeFromFreeList(Block* block);
		void addToFreeList(Block* block);

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


	/**
	* @brief get class idx given size of class
	* @param size_t size
	* @return int
	*/
	inline int MemoryAllocator::getSizeClass(size_t size)
	{
		// common sizes
		if (size <= 512)
		{
			if (size <= 64)  return 0;
			if (size <= 128) return 1;
			if (size <= 256) return 2;
			return 3;
		}

		// Less common sizes
		if (size <= 1024)    return 4;
		if (size <= 2048)    return 5;
		if (size <= 4096)    return 6;
		if (size <= 8192)    return 7;
		if (size <= 16384)   return 8;
		if (size <= 24576)   return 9;
		if (size <= 32768)   return 10;
		if (size <= 65536)   return 11;
		if (size <= 131072)  return 12;
		if (size <= 262144)  return 13;
		if (size <= 524288)  return 14;
		if (size <= 786432)  return 15;
		if (size <= 1048576) return 16;
		if (size <= 2097152) return 17;
		if (size <= 3145728) return 18;
		if (size <= 4194304) return 19;
		if (size <= 8388608) return 20;
		return OVERFLOW_CLASS;
	}

}
