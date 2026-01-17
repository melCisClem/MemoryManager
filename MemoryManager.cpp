#include "memoryManager.h"
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
#endif
	return 0;
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
			defaultPoolSize = static_cast<size_t>(availableMem * 0.0001); // 0.01% of available system ram

		size_t minPoolSize = 100 * 1024 * 1024; // 100 MB
		size_t maxPoolSize = 4ULL * 1024 * 1024; // 4 GB

		stats.poolSize = std::max(minPoolSize, std::min(defaultPoolSize, maxPoolSize));
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

		// init entire pool as 1 large free block
		Block* initialBlock = (Block*)poolStart;
		initialBlock->size = stats.poolSize;
		initialBlock->isFree = true;
		initialBlock->next = nullptr;
		initialBlock->prev = nullptr;
		initialBlock->poolIdx = 0;

		BlockFooter* footer = getFooter(initialBlock, stats.poolSize);
		footer->size = stats.poolSize;
		footer->isFree = true;

		int classIdx = getSizeClass(stats.poolSize);
		freeLists[classIdx] = initialBlock;
	}

	MemoryAllocator::~MemoryAllocator(void)
	{
#ifdef _DEBUG
		printStats();
#endif
		checkForLeaks();

		for (int i = 0; i < poolCnt; ++i)
			if (pools[i].isActive)
				free(pools[i].start);
	}

	MemoryAllocator& MemoryAllocator::GetInstance()
	{
		static MemoryAllocator instance;
		return instance;
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

		size_t totalSize = size + OVERHEAD;
		totalSize = (totalSize + 7) & ~7; // 8byte alignment

		if (totalSize < MIN_BLOCK_SIZE)
			totalSize = MIN_BLOCK_SIZE;

		Block* block = findBlock(totalSize);
		if (!block && autoExtend)
		{
#ifdef _DEBUG
			std::cout << "No block found. Attempting to extend pool...\n";
#endif
			size_t extendSize = totalSize * 2;
			size_t quarterPool = stats.poolSize / 4;
			if (extendSize < quarterPool) // add overhead
				extendSize = quarterPool;

			if (extendPool(extendSize))
				block = findBlock(totalSize);
		}
		if (!block)
		{
#ifdef _DEBUG
			std::cerr << "MemoryAllocator::allocate error > Allocation of " << totalSize << " bytes failed. Out of memory.\n";
#endif
			throw std::bad_alloc{};
		}
		removeFromFreeList(block);

		if (block->size >= totalSize + MIN_BLOCK_SIZE)
			splitBlock(block, totalSize);

		block->isFree = false;
		BlockFooter* footer = getFooter(block, block->size);
		footer->isFree = false;

		stats.allocated += block->size;
		stats.totalAllocations++;

#ifdef _DEBUG
		std::cout << "Memory Manager: Allocated:" << totalSize << " bytes of memory\n";
#endif

		return (char*)block + HEADER_SIZE;
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

		Block* block = (Block*)((char*)obj - HEADER_SIZE);

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
		BlockFooter* footer = getFooter(block, block->size);
		footer->isFree = true;

		stats.allocated -= block->size;
		stats.totalDeallocations++;

		block = coalesce(block);

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
		size_t newPoolSize;
		if (additionalSize > 0)
			newPoolSize = additionalSize;
		else if (extensionSize > 0)
			newPoolSize = extensionSize;
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
		int newPoolIdx = poolCnt;
		pools[newPoolIdx].start = newPool;
		pools[newPoolIdx].end = (char*)newPool + newPoolSize;
		pools[newPoolIdx].size = newPoolSize;
		pools[newPoolIdx].isExtension = true;
		pools[newPoolIdx].isActive = true;
		poolCnt++;

		Block* newBlock = (Block*)newPool;
		newBlock->size = newPoolSize;
		newBlock->isFree = true;
		newBlock->next = nullptr;
		newBlock->prev = nullptr;
		newBlock->poolIdx = newPoolIdx;

		BlockFooter* footer = getFooter(newBlock, newPoolSize);
		footer->size = newPoolSize;
		footer->isFree = true;

		addToFreeList(newBlock);

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
		size_t reclaimed = 0;
		int reclaimedCnt = 0;

		// last pool to pool 1 (skip pool 0)
		for (int i = poolCnt - 1; i >= 0; --i)
		{
			if (!pools[i].isActive || !pools[i].isExtension)
				continue;

			bool poolIsEmpty = true;
			Block* current = (Block*)pools[i].start;
			void* poolEnd = pools[i].end;

			while((void*)current < poolEnd)
			{
				if (!current->isFree)
				{
					poolIsEmpty = false;
					break;
				}

				if (current->size == 0 || current->size > pools[i].size)
					break;

				current = (Block*)((char*)current + current->size);
			}

			if (poolIsEmpty)
			{
				current = (Block*)pools[i].start;
				while ((void*)current < poolEnd)
				{
					if(current->isFree)
						removeFromFreeList(current);

					if (current->size == 0 || current->size > pools[i].size)
						break;

					current = (Block*)((char*)current + current->size);
				}

				size_t poolSize = pools[i].size;
				free(pools[i].start);

				reclaimed += pools[i].size;
				reclaimedCnt++;

				pools[i].isActive = false;
				pools[i].start = nullptr;
				pools[i].end = nullptr;
				pools[i].size = 0;

				stats.totalPoolSize -= poolSize;

#ifdef _DEBUG
				std::cout << "Reclaimed pool " << i << " (" << reclaimed << " bytes)\n";
#endif
			}
		}

		// Compact pool array (skip pool 0)
		int writeIdx = 1;
		for (int readIdx = 1; readIdx < poolCnt; ++readIdx)
		{
			if (pools[readIdx].isActive)
			{
				if (writeIdx != readIdx)
				{
					pools[writeIdx] = pools[readIdx];

					Block* current = (Block*)pools[writeIdx].start;
					void* poolEnd = pools[writeIdx].end;
					while ((void*)current < poolEnd)
					{
						current->poolIdx = writeIdx;

						if (current->size == 0 || current->size > pools[writeIdx].size)
							break;
						current = (Block*)((char*)current + current->size);
					}
				}
				writeIdx++;
			}
		}

		// clear remaining pools
		for (int i = writeIdx; i < poolCnt; ++i)
		{
			pools[i].isActive = false;
			pools[i].start = nullptr;
			pools[i].end = nullptr;
			pools[i].size = 0;
		}
		poolCnt = writeIdx;

		stats.activeExtensions -= reclaimedCnt;

#ifdef _DEBUG
		if (reclaimed > 0)
			std::cout << "Reclaimed " << reclaimedCnt << " pools (" << reclaimed << " bytes total)\n";
#endif
		return reclaimed;
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
				if (!current->isFree)
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
		for(int i = 1; i < poolCnt; ++i)
		{
			if (!pools[i].isActive || !pools[i].isExtension)
				continue;

			bool poolIsEmpty = true;
			Block* current = (Block*)pools[i].start;
			void* poolEnd = pools[i].end;

			while ((void*)current < poolEnd)
			{
				if (!current->isFree)
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
		if(reclaimable > 0)
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
		return 7;
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
		return 8192;
	}

	Block* MemoryAllocator::findBlock(size_t totalSize)
	{
		try
		{
			int classIdx = getSizeClass(totalSize);

			// if got exact size
			if (freeLists[classIdx])
				return freeLists[classIdx];

			// find next largest block
			for (int i = classIdx; i < NUM_CLASSES; ++i)
				if (freeLists[i])
					return freeLists[i];

			defrag(); // defrag n try agn
			for (int i = classIdx; i < NUM_CLASSES; ++i)
				if (freeLists[i])
					return freeLists[i];
		}
		catch (std::exception const& e)
		{
			std::cerr << "MemoryAllocator::findblock error > Out of memory" << e.what() << std::endl;
			throw std::bad_alloc{};
		}
		return nullptr; // to remove warning
	}

	void MemoryAllocator::splitBlock(Block* block, size_t size)
	{
		size_t remainingSize = block->size - size;

		if (remainingSize >= MIN_BLOCK_SIZE)
		{
			// resize current block
			block->size = size;
			BlockFooter* footer = getFooter(block, block->size);
			footer->size = size;

			// create new free block from remainingSize 
			Block* newBlock = (Block*)((char*)block + size);
			newBlock->size = remainingSize;
			newBlock->isFree = true;
			newBlock->next = nullptr;
			newBlock->prev = nullptr;
			newBlock->poolIdx = block->poolIdx;

			BlockFooter* newFooter = getFooter(newBlock, newBlock->size);
			newFooter->size = remainingSize;
			newFooter->isFree = true;

			addToFreeList(newBlock);
		}
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

		if (block->prev)
			block->prev->next = block->next;
		else
			freeLists[classIdx] = block->next;

		if (block->next)
			block->next->prev = block->prev;

		block->next = nullptr;
		block->prev = nullptr;
	}

	void MemoryAllocator::addToFreeList(Block* block)
	{
		int classIdx = getSizeClass(block->size);

		block->prev = nullptr;
		block->next = freeLists[classIdx];

		if (freeLists[classIdx])
			freeLists[classIdx]->prev = block;

		freeLists[classIdx] = block;
	}

	Block* MemoryAllocator::coalesce(Block* block)
	{
		Block* prev = getPrevBlock(block);
		Block* next = getNextBlock(block);

		bool prevFree = prev && prev->isFree && (prev->poolIdx == block->poolIdx);
		bool nextFree = next && next->isFree && (next->poolIdx == block->poolIdx);

		if (!prevFree && !nextFree)
			return block;

		if (!prevFree && nextFree)
		{
			removeFromFreeList(next);

			block->size += next->size;
			BlockFooter* footer = getFooter(block, block->size);
			footer->size = block->size;
			footer->isFree = true;

			return block;
		}

		if (prevFree && !nextFree)
		{
			removeFromFreeList(prev);

			prev->size += block->size;
			BlockFooter* footer = getFooter(prev, prev->size);
			footer->size = prev->size;
			footer->isFree = true;

			return prev;
		}

		// both free
		removeFromFreeList(prev);
		removeFromFreeList(next);

		prev->size += block->size + next->size;
		BlockFooter* footer = getFooter(prev, prev->size);
		footer->size = prev->size;
		footer->isFree = true;

		return prev;
	}

	void MemoryAllocator::defrag(void)
	{
		for (int poolIdx = 0; poolIdx < poolCnt; ++poolIdx)
		{
			if (!pools[poolIdx].isActive)
				continue;

			Block* current = (Block*)pools[poolIdx].start;
			void* poolEnd = pools[poolIdx].end;

			while ((void*)current < poolEnd)
			{
				if (current->isFree)
				{
					bool merged = false;
					while (true)
					{
						Block* next = getNextBlock(current);

						// if same pool only
						if (!next || !next->isFree || next->poolIdx != current->poolIdx)
							break;

						if (!merged)
						{
							removeFromFreeList(current);
							merged = true;
						}
						removeFromFreeList(next);

						current->size += next->size;
						BlockFooter* footer = getFooter(current, current->size);
						footer->size = current->size;
						footer->isFree = true;
					}
					if (merged)
						addToFreeList(current);
				}
				current = (Block*)((char*)current + current->size);
			}
		}


	}

	BlockFooter* MemoryAllocator::getFooter(void* blockStart, size_t blockSize)
	{
		return (BlockFooter*)((char*)blockStart + blockSize - FOOTER_SIZE);
	}

	Block* MemoryAllocator::getPrevBlock(Block* block)
	{
		int poolIdx = getPoolIndex(block);
		if (poolIdx < 0 || !pools[poolIdx].isActive)
			return nullptr;

		void* poolStart = pools[poolIdx].start;

		// this for first block in pool to not trigger error msg as it will always exist before pool
		if ((void*)block == poolStart)
			return nullptr;
		if ((void*)block <= poolStart)
		{
#ifdef _DEBUG
			std::cerr << "MemoryAllocator::getPrevBlock error > block exist before memory pool start address\n";
#endif
			return nullptr;
		}

		BlockFooter* prevFooter = (BlockFooter*)((char*)block - FOOTER_SIZE);
		if (!isValidBlock(prevFooter))
		{
#ifdef _DEBUG
			std::cerr << "MemoryAllocator::getPrevBlock error > previous memory footer is invalid\n";
#endif
			return nullptr;
		}

		Block* prevBlock = (Block*)((char*)block - prevFooter->size);
		if (!isValidBlock(prevBlock))
		{
#ifdef _DEBUG
			std::cerr << "MemoryAllocator::getPrevBlock error > previous memory block is invalid\n";
#endif
			return nullptr;
		}
		return prevBlock;
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
		for (int i = 0; i < poolCnt; ++i)
			if (pools[i].isActive && ptr >= pools[i].start && ptr < pools[i].end)
				return true;
		return false;
	}

	int mem::MemoryAllocator::getPoolIndex(void* ptr)
	{
		if (!ptr)
		{
#ifdef _DEBUG
			std::cerr << "MemoryAllocator::getPoolIndex error > Invalid Pool Index\n";
#endif
			return -1;
		}
		for (int i = 0; i < poolCnt; ++i)
			if (pools[i].isActive && ptr >= pools[i].start && ptr < pools[i].end)
				return i;
		return -1;
	}
}

