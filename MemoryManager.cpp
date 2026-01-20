#include "MemoryManager.h"
#include <exception>
#include <iostream>
#include <cstdlib>

#define NOMINMAX
#include <windows.h>

size_t getAvailableMemory()
{
#ifdef _WIN32
	MEMORYSTATUSEX status;
	status.dwLength = sizeof(status);
	GlobalMemoryStatusEx(&status);
	return static_cast<size_t>(status.ullAvailPhys);
#else
	return 0;
#endif
}

namespace mem {

	MemoryAllocator::MemoryAllocator(size_t requestedSize = 0)
		: poolCnt(0), maxPools(MAX_POOLS), stats{}, autoExtend(true), extensionSize(0)
	{
		size_t availableMem = getAvailableMemory();
		size_t defaultPoolSize;

		if (requestedSize > 0)
			defaultPoolSize = requestedSize;
		else
			defaultPoolSize = static_cast<size_t>(availableMem * 0.00001); // 0.00001% of available system ram

		size_t minPoolSize = 100 * 1024 * 1024; // 100 MB

		stats.poolSize = std::max(minPoolSize, defaultPoolSize);
		stats.totalPoolSize = stats.poolSize;

		memset(freeLists, 0, sizeof(freeLists));
		memset(pools, 0, sizeof(pools));

		void* poolStart = malloc(stats.poolSize);
		if (!poolStart)
			throw std::bad_alloc();
		pools[0].start = poolStart;
		pools[0].end = (char*)poolStart + stats.poolSize;
		pools[0].size = stats.poolSize;
		pools[0].isExtension = false;
		pools[0].isActive = true;
		poolCnt = 1;

		initializeSegregatedLists(poolStart, stats.poolSize, 0);
	}

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
				free(pools[i].start);
				pools[i].isActive = false;
				pools[i].start = nullptr;
				pools[i].end = nullptr;
			}
	}

	MemoryAllocator& MemoryAllocator::GetInstance()
	{
		static MemoryAllocator instance;
		return instance;
	}

	// cutting the free list up
	void MemoryAllocator::initializeSegregatedLists(void* poolStart, size_t poolSize, int poolIdx)
	{
		char* curr = (char*)poolStart;
		char* poolEnd = (char*)poolStart + poolSize;

		for (int classIdx = 0; classIdx < 12; ++classIdx)
		{
			size_t blockSize = getClassSize(classIdx);
			int numBlocks = (classIdx < 6) ? 100 : 20;

			for (int i = 0; i < numBlocks && (curr + blockSize) <= poolEnd; ++i)
			{
				Block* block = (Block*)curr;
				block->size = blockSize;
				block->isFree = true;
				block->poolIdx = poolIdx;
				addToFreeList(block);
				curr += blockSize;
			}
		}

		// Carve ALL remaining space into largest blocks
		size_t largestSize = getClassSize(NUM_CLASSES - 1);
		while ((curr + largestSize) <= poolEnd) {
			Block* block = (Block*)curr;
			block->size = largestSize;
			block->isFree = true;
			block->poolIdx = poolIdx;

			getFooter(block, largestSize)->size = largestSize;
			getFooter(block, largestSize)->isFree = true;

			addToFreeList(block);
			curr += largestSize;
		}

		// handle remaining space
		size_t remaining = (char*)poolEnd - curr;
		if (remaining >= MIN_BLOCK_SIZE)
		{
			Block* block = (Block*)curr;
			block->size = remaining;
			block->isFree = true;
			block->poolIdx = poolIdx;
			block->next = nullptr;

			BlockFooter* footer = getFooter(block, remaining);
			footer->size = remaining;
			footer->isFree = true;

			addToFreeList(block);
		}
		else if (remaining > 0)
			memset(curr, 0, remaining);
	}

	void* MemoryAllocator::allocate(size_t size)
	{
		if (!size)
		{
#ifdef _DEBUG
			std::cerr << "MemoryAllocator::allocate error > Invalid allocation size\n";
#endif
			throw std::bad_alloc{};
		}

		size_t alignedHeaderSize = (HEADER_SIZE + 7) & ~7;
		size_t alignedFooterSize = (FOOTER_SIZE + 7) & ~7;

		size_t totalRequired = size + alignedHeaderSize + alignedFooterSize;
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
		BlockFooter* footer = getFooter(block, block->size);
		footer->isFree = false;

		stats.allocated += block->size;
		stats.totalAllocations++;

#ifdef _DEBUG
		std::cout << "Memory Manager: Allocated:" << block->size << " bytes (requested: "
			<< totalRequired << ", wasted: " << (block->size - totalRequired) << ")\n";
#endif

		return (char*)block + alignedHeaderSize;
	}

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

		BlockFooter* footer = getFooter(block, block->size);
		footer->isFree = true;

		stats.allocated -= block->size;
		stats.totalDeallocations++;

		addToFreeList(block);
	}

	void MemoryAllocator::poolSize(size_t size)
	{
		stats.poolSize = size;
	}

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
		
		size_t largestBlock = getClassSize(NUM_CLASSES - 1);
		size_t newPoolSize;

		if (additionalSize > 0)
		{
			size_t buffer = 15 * 1024 * 1024; 
			newPoolSize = additionalSize + buffer + (OVERHEAD * 100);
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

		void* newPool = malloc(newPoolSize);
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
			Block** currPtr = &freeLists[classIdx];
			while (*currPtr)
			{
				Block* block = *currPtr;
				int pIdx = getPoolIndex(block);

				if (pIdx != -1 && poolMarkedForDeletion[pIdx])
				{
					*currPtr = block->next;
				}
				else
				{
					currPtr = &(block->next);
				}
			}
		}

		for (int i = 1; i < poolCnt; ++i)
		{
			if (poolMarkedForDeletion[i])
			{
				reclaimedTotal += pools[i].size;
				reclaimedPoolCount++;

				free(pools[i].start);
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

	void MemoryAllocator::printStats(void)
	{
		std::cout << "\n\n------------- Memory Pool Statistics -------------------\n";
		std::cout << "  Initial pool size: " << stats.poolSize << " bytes\n";
		std::cout << "  Total pool size: " << stats.totalPoolSize << " bytes\n";
		std::cout << "  Pool total extensions: " << stats.extensionCount << "\n";
		std::cout << "  Pool active extensions: " << stats.activeExtensions << "\n";

		std::cout << "  Active pools: " << poolCnt << "/" << MAX_POOLS;
		if (maxPools < MAX_POOLS)
			std::cout << " (limit set to " << maxPools << ")\n";
		else
			std::cout << "\n";

		std::cout << "  Allocated: " << stats.allocated << " bytes\n";
		std::cout << "  Free: " << (stats.totalPoolSize - stats.allocated) << " bytes\n";

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

			if (count > 0) {
				std::cout << "  Class " << i << " (~" << getClassSize(i)
					<< " bytes): " << count << " blocks, "
					<< totalFree << " bytes\n";
			}
		}
		std::cout << "--------------------------------------------------------\n";
	}

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

	int MemoryAllocator::getSizeClass(size_t size)
	{
		if (size <= 64) return 0;
		if (size <= 128) return 1;
		if (size <= 256) return 2;
		if (size <= 512) return 3;
		if (size <= 1024) return 4;
		if (size <= 2048) return 5;
		if (size <= 4096) return 6;
		if (size <= 8192) return 7;
		if (size <= 16384) return 8;
		if (size <= 32768) return 9;
		if (size <= 65536) return 10;
		if (size <= 131072) return 11; // 128KB
		if (size <= 1048576) return 12; // 1MB
		if (size <= 10485760) return 13; // 10MB
		return 14; // 25MB
	}

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
		if (classIdx == 9) return 32768;
		if (classIdx == 10) return 65536;
		if (classIdx == 11) return 131072; // 128KB
		if (classIdx == 12) return 1048576; // 1MB
		if (classIdx == 13) return 10485760; // 10MB
		return 26214400; // 25MB
	}

	Block* MemoryAllocator::findBlock(size_t totalSize)
	{
		int classIdx = getSizeClass(totalSize);

		for (int i = classIdx; i < NUM_CLASSES; ++i) 
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

	void MemoryAllocator::removeFromFreeList(Block* block)
	{
		if (!block || !isValidBlock(block))
		{
#ifdef _DEBUG
			std::cerr << "MemoryAllocator::removeFromFreeList: Invalid block pointer!\n";
#endif
			throw std::bad_alloc{};
		}

		if (!block->isFree)
		{
#ifdef _DEBUG
			std::cerr << "MemoryAllocator::removeFromFreeList: Block is not free!\n";
#endif
			throw std::bad_alloc{};
		}

		int classIdx = getSizeClass(block->size);

		if (freeLists[classIdx] == block) // if head
		{
			freeLists[classIdx] = block->next;
			block->next = nullptr;
			return;
		}

		// else find prev node
		Block* prev = freeLists[classIdx];
		Block* curr = prev ? prev->next : nullptr;

		while (curr)
		{
			if (curr == block)
			{
				prev->next = curr->next;
				block->next = nullptr;
				return;
			}
			prev = curr;
			curr = curr->next;
		}

#ifdef _DEBUG
		std::cerr << "MemoryAllocator::removeFromFreeList: Block not found in free list!\n";
#endif
		throw std::bad_alloc{};
	}

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
		block->next = freeLists[classIdx];
		freeLists[classIdx] = block;
	}

	BlockFooter* MemoryAllocator::getFooter(void* blockStart, size_t blockSize)
	{
		return (BlockFooter*)((char*)blockStart + blockSize - FOOTER_SIZE);
	}

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

	bool MemoryAllocator::isValidBlock(void* ptr)
	{
		if (!ptr) return false;

		Block* block = (Block*)ptr;
		int hintIdx = block->poolIdx;

		if (hintIdx >= 0 && hintIdx < poolCnt && pools[hintIdx].isActive) 
			if (ptr >= pools[hintIdx].start && ptr < pools[hintIdx].end)
				return true;

		// fallback: search all pools
		for (int i = 0; i < poolCnt; ++i) 
			if (pools[i].isActive && ptr >= pools[i].start && ptr < pools[i].end) 
				return true;

		return false;
	}

	int MemoryAllocator::getPoolIndex(void* ptr)
	{
		if (!ptr)
		{
#ifdef _DEBUG
			std::cerr << "MemoryAllocator::getPoolIndex error > Invalid Pool Index\n";
#endif
			return -1;
		}
		Block* block = (Block*)((char*)ptr - (HEADER_SIZE + 7 & ~7));
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

