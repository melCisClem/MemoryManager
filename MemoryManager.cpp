#include "memoryManager.h"
#include <exception>
#include <iostream>
#include <cstdlib>

namespace mem {

	/**
	* @brief ctor
	* @param size_t requestedSize
	*/
	MemoryAllocator::MemoryAllocator(size_t requestedSize)
		: poolCnt(0), maxPools(MAX_POOLS), stats{}, autoExtend(true), extensionSize(0)
	{
		size_t defaultPoolSize;

		if (requestedSize > 0)
			defaultPoolSize = requestedSize;
		else
			defaultPoolSize = 100 * 1024 * 1024; // 100 MB

		stats.poolSize = (defaultPoolSize + POOL_ALIGNMENT - 1) & ~(POOL_ALIGNMENT - 1);
		stats.totalPoolSize = 0;

		memset(freeLists, 0, sizeof(freeLists));
		memset(pools, 0, sizeof(pools));

		// 1 extra pool for overhead so less perf hit when extending
		const int initialPoolCount = 2;
		for (int i = 0; i < initialPoolCount; ++i)
		{
			//void* poolStart = malloc(stats.poolSize);
			void* poolStart = _aligned_malloc(stats.poolSize, POOL_ALIGNMENT);
			if (!poolStart)
			{
				for (int j = 0; j < i; ++j)
					_aligned_free(pools[j].start);

				throw std::bad_alloc();
			}

			pools[i].start = poolStart;
			pools[i].end = (char*)poolStart + stats.poolSize;
			pools[i].size = stats.poolSize;
			pools[i].isExtension = false;
			pools[i].isActive = true;

			stats.totalPoolSize += stats.poolSize;
			poolCnt++;

			initializeSegregatedLists(poolStart, stats.poolSize, i);

#ifdef _DEBUG
			uintptr_t addr = (uintptr_t)poolStart;
			bool isPageAligned = (addr % 4096) == 0;
			bool isCacheAligned = (addr % 64) == 0;

			std::cout << "Pool " << i << ": " << poolStart
				<< " (Page aligned: " << (isPageAligned ? "YES" : "NO")
				<< ", Cache aligned: " << (isCacheAligned ? "YES" : "NO") << ")\n";
#endif
		}
	}

	/**
	* @brief dtor
	*/
	MemoryAllocator::~MemoryAllocator(void)
	{
#ifdef _DEBUG
		printStats();
#endif
		checkForLeaks();

		for (int i = 0; i < NUM_CLASSES; ++i)
			freeLists[i] = nullptr;

		for (int i = 0; i < poolCnt; ++i)
			if (pools[i].isActive)
			{
				_aligned_free(pools[i].start);
				pools[i].isActive = false;
				pools[i].start = nullptr;
				pools[i].end = nullptr;
			}
	}

	/**
	* @brief get singleton instance of memory allocator
	* @return MemoryAllocator&
	*/
	MemoryAllocator& MemoryAllocator::GetInstance()
	{
		static MemoryAllocator instance;
		return instance;
	}

	/**
	* @brief size up the list into blocks
	* @param void* poolStart, size_t poolSize, int poolIdx
	* @return void
	*/
	void MemoryAllocator::initializeSegregatedLists(void* poolStart, size_t poolSize, int poolIdx)
	{
		char* curr = (char*)poolStart;
		char* poolEnd = (char*)poolStart + poolSize;

		for (int classIdx = 0; classIdx < OVERFLOW_CLASS; ++classIdx)
		{
			size_t blockSize = getClassSize(classIdx);
			int numBlocks;
			if (classIdx <= 3)       numBlocks = 500;
			else if (classIdx <= 7)  numBlocks = 200;
			else if (classIdx <= 10) numBlocks = 50;
			else if (classIdx <= 13) numBlocks = 20;
			else if (classIdx <= 16) numBlocks = 5;
			else if (classIdx <= 19) numBlocks = 2;
			else                     numBlocks = 1;

			for (int i = 0; i < numBlocks && (curr + blockSize) <= poolEnd; ++i)
			{
				Block* block = (Block*)curr;
				block->size = blockSize;
				block->isFree = true;
				block->poolIdx = poolIdx;
				block->next = nullptr;
				block->prev = nullptr;

				addToFreeList(block);
				curr += blockSize;
			}
		}

		// handle remaining space
		size_t remaining = (char*)poolEnd - curr;
		while (remaining >= MIN_BLOCK_SIZE)
		{
			size_t blockSize;
			if (remaining >= MAX_BLOCK_SIZE * 2)
				blockSize = MAX_BLOCK_SIZE * 2;
			else if (remaining >= MAX_BLOCK_SIZE)
				blockSize = MAX_BLOCK_SIZE;
			else if (remaining >= MAX_BLOCK_SIZE / 2)
				blockSize = MAX_BLOCK_SIZE / 2;
			else
				blockSize = remaining;

			Block* block = (Block*)curr;
			block->size = blockSize;
			block->isFree = true;
			block->poolIdx = poolIdx;
			block->next = nullptr;
			block->prev = nullptr;

			addToFreeList(block);

			curr += blockSize;
			remaining = (char*)poolEnd - curr;
		}

		if (remaining > 0)
			memset(curr, 0, remaining);
	}

	/**
	* @brief allocate memory
	* @param size_t size
	* @return void*
	*/
	void* MemoryAllocator::allocate(size_t size, bool print)
	{
		if (!size)
		{
#ifdef _DEBUG
			std::cerr << "MemoryAllocator::allocate error > Invalid allocation size\n";
#endif
			throw std::bad_alloc{};
		}

		size_t alignedHeaderSize = (HEADER_SIZE + 7) & ~7;
		size_t totalRequired = size + alignedHeaderSize;
		totalRequired = (totalRequired + 7) & ~7; // 8-byte block alignment

		Block* block = findBlock(totalRequired);
		if (!block && autoExtend)
		{
#ifdef _DEBUG
			std::cout << "No block found. Attempting to extend pool...\n";
#endif
			if (extendPool(totalRequired > extensionSize ? totalRequired : 0))
				block = findBlock(totalRequired);
		}
		if (!block)
		{
#ifdef _DEBUG
			std::cerr << "MemoryAllocator::allocate error > Allocation of " << totalRequired << " bytes failed. Out of memory.\n";
#endif
			throw std::bad_alloc{};
		}
		if (block->isFree)
			removeFromFreeList(block);

		block->isFree = false;
		block->next = nullptr;
		block->prev = nullptr;

		stats.allocated += block->size;
		stats.totalAllocations++;

		if (print)
		{
#ifdef _DEBUG
			std::cout << "Memory Manager: Allocated:" << block->size << " bytes (requested: "
				<< totalRequired << ", wasted: " << (block->size - totalRequired) << ")\n";
#endif
		}

		return (char*)block + alignedHeaderSize;
	}

	/**
	* @brief deallocate memory
	* @param void* obj
	* @return void
	*/
	void MemoryAllocator::deallocate(void* obj)
	{
		if (!obj)
		{
#ifdef _DEBUG
			std::cerr << "MemoryAllocator::deallocate error > invalid memory block ptr\n";
#endif
			throw std::bad_alloc{};
		}

		size_t alignedHeaderSize = (HEADER_SIZE + 7) & ~7;
		Block* block = (Block*)((char*)obj - alignedHeaderSize);
		if (!isValidBlock(block))
		{
#ifdef _DEBUG
			std::cerr << "MemoryAllocator::deallocate error > Invalid block\n";
#endif
			throw std::bad_alloc{};
		}

		if (block->isFree)
		{
#ifdef _DEBUG
			std::cerr << "MemoryAllocator::deallocate error > Double free detected, memory block size " << block->size;
#endif
			throw std::bad_alloc{};
		}

		block->isFree = true;
		block->next = nullptr;
		block->prev = nullptr;

		stats.allocated -= block->size;
		stats.totalDeallocations++;

		addToFreeList(block);
	}

	/**
	* @brief extends the memory pool
	* @param size_t additionalSize
	* @return bool
	*/
	bool MemoryAllocator::extendPool(size_t additionalSize)
	{
		if (poolCnt >= maxPools)
		{
			size_t reclaimed = reclaimUnusedPools();
			if (reclaimed == 0)
			{
#ifdef _DEBUG
				std::cerr << "MemoryAllocator::extendPool error > Maximum pool limit (" << maxPools << ") reached\n";
#endif
				return false;
			}
		}

		size_t newPoolSize;
		if (additionalSize > 0)
		{
			size_t buffer = 15 * 1024 * 1024;
			newPoolSize = additionalSize + buffer;
		}
		else
		{
			// extend by 50% of current pool size
			size_t half = stats.totalPoolSize / 2;
			size_t min = 100 * 1024 * 1024; // min 100mb
			newPoolSize = (half > min) ? half : min;
		}
		// align with min block size
		newPoolSize = (newPoolSize + MIN_BLOCK_SIZE - 1) & ~(MIN_BLOCK_SIZE - 1);

		void* newPool = _aligned_malloc(newPoolSize, POOL_ALIGNMENT);
		if (!newPool)
		{
#ifdef _DEBUG
			std::cerr << "MemoryAllocator::extendPool error > Failed to allocate extension of " << newPoolSize << " bytes\n";
#endif
			return false;
		}

		int newPoolIdx = -1;
		for (int i = 0; i < poolCnt; ++i)
		{
			if (!pools[i].isActive)
			{
				newPoolIdx = i;
				break;
			}
		}

		if (newPoolIdx == -1)
		{
			if (poolCnt < maxPools)
				newPoolIdx = poolCnt++;
			else return false;
		}

		pools[newPoolIdx].start = newPool;
		pools[newPoolIdx].end = (char*)newPool + newPoolSize;
		pools[newPoolIdx].size = newPoolSize;
		pools[newPoolIdx].isExtension = true;
		pools[newPoolIdx].isActive = true;

		initializeSegregatedLists(newPool, newPoolSize, newPoolIdx);

		stats.totalPoolSize += newPoolSize;
		stats.extensionCount++;
		stats.activeExtensions++;

#ifdef _DEBUG
		std::cout << "Memory pool extended by " << newPoolSize << " bytes (Extension no." << stats.extensionCount << ", Total pools: " << poolCnt << ")\n";
#endif
		return true;
	}

	/**
	* @brief reclaims pools that arent used/empty
	* @return size_t
	*/
	size_t MemoryAllocator::reclaimUnusedPools()
	{
		size_t reclaimedTotal = 0;
		int reclaimedPoolCount = 0;

		bool poolMarkedForDeletion[MAX_POOLS] = { false };
		for (int i = 1; i < poolCnt; ++i)
		{
			if (pools[i].isActive && pools[i].isExtension)
			{
				bool poolIsEmpty = true;
				char* curr = (char*)pools[i].start;

				while (curr < (char*)pools[i].end)
				{
					Block* block = (Block*)curr;
					if (!block->isFree)
					{
						poolIsEmpty = false;
						break;
					}
					curr += block->size;
				}

				if (poolIsEmpty)
				{
					poolMarkedForDeletion[i] = true;
				}
			}
		}

		for (int classIdx = 0; classIdx < NUM_CLASSES; ++classIdx)
		{
			Block* curr = freeLists[classIdx];
			Block* prev = nullptr;

			while (curr)
			{
				Block* next = curr->next;
				int pIdx = getPoolIndex(curr);

				if (pIdx != -1 && poolMarkedForDeletion[pIdx])
				{
					if (prev)
						prev->next = next;
					else
						freeLists[classIdx] = next;

					if (next)
						next->prev = prev;
				}
				else
					prev = curr;
				curr = next;
			}
		}

		for (int i = 1; i < poolCnt; ++i)
		{
			if (poolMarkedForDeletion[i])
			{
				reclaimedTotal += pools[i].size;
				reclaimedPoolCount++;

				_aligned_free(pools[i].start);
				pools[i].isActive = false;
				pools[i].start = nullptr;
				pools[i].end = nullptr;
			}
		}

		stats.totalPoolSize -= reclaimedTotal;
		stats.activeExtensions -= reclaimedPoolCount;

#ifdef _DEBUG
		if (reclaimedTotal > 0)
			std::cout << "Reclaimed " << reclaimedPoolCount << " pools (" << reclaimedTotal << " bytes total)\n";
#endif

		return reclaimedTotal;
	}

	/**
	* @brief const func calcs reclaimable memory
	* @return size_t
	*/
	size_t MemoryAllocator::getReclaimableMemory() const
	{
		size_t reclaimable = 0;

		// skip initial pool 0
		for (int i = 1; i < poolCnt; ++i)
		{
			if (!pools[i].isActive || !pools[i].isExtension)
				continue;

			bool poolIsEmpty = true;
			Block* current = (Block*)pools[i].start;
			void* poolEnd = pools[i].end;

			while ((void*)current < poolEnd)
			{
				if (current->size == 0 || !current->isFree)
				{
					poolIsEmpty = false;
					break;
				}

				if (current->size == 0 || current->size > pools[i].size)
					break;
				current = (Block*)((char*)current + current->size);
			}

			if (poolIsEmpty)
				reclaimable += pools[i].size;
		}
		return reclaimable;
	}

	/**
	* @brief const func calcs num of reclaimable pools
	* @return int
	*/
	int MemoryAllocator::getReclaimablePoolCount() const
	{
		int cnt = 0;

		// skip initial pool 0
		for (int i = 1; i < poolCnt; ++i)
		{
			if (!pools[i].isActive || !pools[i].isExtension)
				continue;

			bool poolIsEmpty = true;
			Block* current = (Block*)pools[i].start;
			void* poolEnd = pools[i].end;

			while ((void*)current < poolEnd)
			{
				if (current->size == 0 || !current->isFree)
				{
					poolIsEmpty = false;
					break;
				}

				if (current->size == 0 || current->size > pools[i].size)
					break;
				current = (Block*)((char*)current + current->size);
			}
			if (poolIsEmpty)
				cnt++;
		}
		return cnt;
	}

	/**
	* @brief prints stats abt memory pool
	*/
	void MemoryAllocator::printStats(void)
	{
		std::cout << "\n\n------------- Memory Pool Statistics -------------------\n";
		std::cout << "  Initial pool size: " << stats.poolSize << " bytes (" << stats.poolSize / (1024 * 1024) << " MB)\n";
		std::cout << "  Total pool size: " << stats.totalPoolSize << " bytes (" << stats.totalPoolSize / (1024 * 1024) << " MB)\n";
		std::cout << "  Pool total extensions: " << stats.extensionCount << "\n";
		std::cout << "  Pool active extensions: " << stats.activeExtensions << "\n";

		std::cout << "  Active pools: " << poolCnt << "/" << MAX_POOLS;
		if (maxPools < MAX_POOLS)
			std::cout << " (limit set to " << maxPools << ")\n";
		else
			std::cout << "\n";

		std::cout << "  Allocated: " << stats.allocated << " bytes (" << stats.allocated / (1024 * 1024) << " MB)\n";
		std::cout << "  Free: " << (stats.totalPoolSize - stats.allocated) << " bytes (" << (stats.totalPoolSize - stats.allocated) / (1024 * 1024) << " MB)\n";

		if (stats.totalPoolSize > 0) {
			double utilization = (stats.allocated * 100.0) / stats.totalPoolSize;
			std::cout << "  Utilization: " << utilization << "%\n";
		}

		size_t reclaimable = getReclaimableMemory();
		int reclaimablePools = getReclaimablePoolCount();
		if (reclaimable > 0)
			std::cout << "  Reclaimable memory: " << reclaimable << " bytes in " << reclaimablePools << " pools\n";

		std::cout << "\nPool Details:\n";
		for (int i = 0; i < poolCnt; ++i) {
			if (pools[i].isActive) {
				std::cout << "  Pool " << i << " (" << (pools[i].isExtension ? "Extension" : "Initial") << "): "
					<< pools[i].size << " bytes at " << pools[i].start << "\n";
			}
		}

		std::cout << "\nFree lists:\n";
		for (int i = 0; i < NUM_CLASSES; i++) {
			int count = 0;
			size_t totalFree = 0;
			Block* curr = freeLists[i];

			while (curr) {
				if (!isValidBlock(curr))
				{
					std::cerr << "Memory Allocator WARNING: Invalid block in free list " << i << " at " << curr << "\n";
					break;
				}
				count++;
				totalFree += curr->size;
				curr = curr->next;
			}

			if (count > 0)
			{
				size_t classSize = getClassSize(i);
				std::cout << "  Class " << i;
				if (classSize != 0)
					std::cout << " (~" << getClassSize(i) << " bytes): ";
				else
					std::cout << " (Variable Overflow): ";

				std::cout << count << " blocks, " << totalFree << " bytes\n";
			}
		}
		std::cout << "--------------------------------------------------------\n";
	}

	/**
	* @brief const func to check for mem leaks
	*/
	void MemoryAllocator::checkForLeaks() const
	{
		std::cout << "\n=== Memory Leak Report ===\n";
		std::cout << "Total allocations: " << stats.totalAllocations << "\n";
		std::cout << "Total deallocations: " << stats.totalDeallocations << "\n";
		std::cout << "Still allocated: " << stats.allocated << " bytes\n";

		if (stats.allocated > 0)
		{
			std::cerr << "WARNING: Memory leak detected! " << stats.allocated << " bytes not freed.\n";

			int leakCount = 0;
			for (int poolIdx = 0; poolIdx < poolCnt; ++poolIdx)
			{
				if (!pools[poolIdx].isActive)
					continue;

				Block* current = (Block*)pools[poolIdx].start;
				void* poolEnd = pools[poolIdx].end;

				while ((void*)current < poolEnd)
				{
					if (current->size == 0)
					{
						std::cerr << "  ERROR: Corrupt block header (size 0) at " << current << ". Terminating pool scan\n";
						break;
					}

					if (!current->isFree)
					{
						leakCount++;
						std::cerr << "  Leaked block #" << leakCount << " (Pool " << poolIdx << "):"
							<< current->size << " bytes at " << current << "\n";
					}

					current = (Block*)((char*)current + current->size);
				}
			}
		}
		else
			std::cout << "No memory leaks detected!\n";
		std::cout << "============================\n\n";
	}

	/**
	* @brief get size class given class idx
	* @param int class idx
	* @return size_t
	*/
	size_t MemoryAllocator::getClassSize(int classIdx)
	{
		if (classIdx == 0) return 64;
		if (classIdx == 1) return 128;
		if (classIdx == 2) return 256;
		if (classIdx == 3) return 512;
		if (classIdx == 4) return 1024;
		if (classIdx == 5) return 2048;
		if (classIdx == 6) return 4096;
		if (classIdx == 7) return 8192;
		if (classIdx == 8) return 16384;
		if (classIdx == 9)  return 24576;
		if (classIdx == 10) return 32768;
		if (classIdx == 11) return 65536;
		if (classIdx == 12) return 131072;
		if (classIdx == 13) return 262144;
		if (classIdx == 14) return 524288;
		if (classIdx == 15) return 786432;
		if (classIdx == 16) return 1048576;
		if (classIdx == 17) return 2097152;
		if (classIdx == 18) return 3145728;
		if (classIdx == 19) return 4194304;
		if (classIdx == 20) return 8388608;
		return 0;
	}

	/**
	* @brief search for suitable memory block given size of allocation
	* @param size_t totalsize
	* @return Block*
	*/
	Block* MemoryAllocator::findBlock(size_t totalSize)
	{
		int classIdx = getSizeClass(totalSize);

		// find exact fit
		//if (freeLists[classIdx])
		//{
		Block* currBlk = freeLists[classIdx];
		if (currBlk && currBlk->size >= totalSize)
			return currBlk;
		//}

		// overflow use best fit
		if (classIdx == OVERFLOW_CLASS)
		{
			Block* curr = freeLists[OVERFLOW_CLASS];
			Block* bestFit = nullptr;
			size_t bestFitWaste = SIZE_MAX;

			while (curr)
			{
				if (curr->size >= totalSize)
				{
					size_t waste = curr->size - totalSize;
					if (waste == 0)
						return curr; // perfect fit

					if (waste < bestFitWaste)
					{
						bestFit = curr;
						bestFitWaste = waste;
					}
				}
				curr = curr->next;
			}

			return bestFit;
		}

		// search for larger
		for (int i = classIdx + 1; i < NUM_CLASSES; ++i)
		{
			if (freeLists[i])
			{
				Block* block = freeLists[i];
				if (block->size >= totalSize)
					return block;
			}
		}

		return nullptr;
	}

	/**
	* @brief removes block from free list
	* @param Block* block
	*/
	void MemoryAllocator::removeFromFreeList(Block* block)
	{
#ifdef _DEBUG
		if (!block || !isValidBlock(block))
		{
			std::cerr << "MemoryAllocator::removeFromFreeList: Invalid block pointer!\n";
			throw std::bad_alloc{};
		}
#endif
		if (!block->isFree)
		{
#ifdef _DEBUG
			std::cerr << "MemoryAllocator::removeFromFreeList: Block is not free!\n";
#endif
			throw std::bad_alloc{};
		}

		int classIdx = getSizeClass(block->size);

		if (block->prev)
			block->prev->next = block->next;
		else
			freeLists[classIdx] = block->next; // Block was head

		if (block->next)
			block->next->prev = block->prev;

		block->next = nullptr;
		block->prev = nullptr;
	}

	/**
	* @brief add block to free list
	* @param Block* block
	*/
	void MemoryAllocator::addToFreeList(Block* block)
	{
		if (!block || !isValidBlock(block))
		{
#ifdef _DEBUG
			std::cerr << "MemoryAllocator::addToFreeList: Invalid block!\n";
#endif
			return;
		}

		int classIdx = getSizeClass(block->size);
		block->prev = nullptr;
		block->next = freeLists[classIdx];

		if (freeLists[classIdx])
			freeLists[classIdx]->prev = block;
		freeLists[classIdx] = block;
	}

	/**
	* @brief get next block
	* @param Block* block
	* @return Block*
	*/
	Block* MemoryAllocator::getNextBlock(Block* block)
	{
		Block* nextBlock = (Block*)((char*)block + block->size);
		if (!isValidBlock(nextBlock))
		{
#ifdef _DEBUG
			std::cerr << "MemoryAllocator::getNextBlock error > next memory block is invalid\n";
#endif
			return nullptr;
		}
		return nextBlock;
	}

	/**
	* @brief checks if given block ptr is valid
	* @param void* ptr
	* @return bool
	*/
	bool MemoryAllocator::isValidBlock(void* ptr)
	{
		if (!ptr)
			return false;

		Block* block = (Block*)ptr;
		int hintIdx = block->poolIdx;

		// direct check (O(1))
		if (hintIdx >= 0 && hintIdx < poolCnt)
		{
			MemoryPool const& pool = pools[hintIdx];
			if (pool.isActive && ptr >= pool.start && ptr < pool.end)
				return true;
		}

		// fallback: search all pools (O(n))
		for (int i = 0; i < poolCnt; ++i)
		{
			if (!pools[i].isActive)
				continue;

			if (ptr >= pools[i].start && ptr < pools[i].end)
				return true;
		}

		return false;
	}

	/**
	* @brief get idx of pool
	* @param void* ptr
	* @return int
	*/
	int MemoryAllocator::getPoolIndex(void* ptr)
	{
		if (!ptr)
		{
#ifdef _DEBUG
			std::cerr << "MemoryAllocator::getPoolIndex error > Invalid Pool Index\n";
#endif
			return -1;
		}
		Block* block = (Block*)ptr;
		int hintIdx = block->poolIdx;

		// direct check (O(1))
		if (hintIdx >= 0 && hintIdx < poolCnt && pools[hintIdx].isActive)
			if (ptr >= pools[hintIdx].start && ptr < pools[hintIdx].end)
				return hintIdx;

		// fallback: search all pools (O(n))
		for (int i = 0; i < poolCnt; ++i)
			if (pools[i].isActive && ptr >= pools[i].start && ptr < pools[i].end)
				return i;

		return -1;
	}
}

