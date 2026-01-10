// my own attempt at creating segregated free list memory allocator

#include "MemoryManager.h"

namespace mem {

	MemoryManager::MemoryManager(MMConfig const& con, size_t size)
		: config{ con }, stats{}
	{
		stats.poolSize = size;

		memset(freeLists, 0, sizeof(freeLists));

		poolStart = malloc(stats.poolSize);
		if (!poolStart)
			throw std::bad_alloc();
		poolEnd = (char*)poolStart + stats.poolSize;

		// init entire pool as 1 large free block
		Block* initialBlock = (Block*)poolStart;
		initialBlock->size = stats.poolSize;
		initialBlock->isFree = true;
		initialBlock->next = nullptr;
		initialBlock->prev = nullptr;

		BlockFooter* footer = getFooter(initialBlock, stats.poolSize);
		footer->size = stats.poolSize;
		footer->isFree = true;

		int classIdx = getSizeClass(stats.poolSize);
		freeLists[classIdx] = initialBlock;
	}

	MemoryManager::~MemoryManager(void)
	{
		free(poolStart);
	}

	void* MemoryManager::allocate(size_t size)
	{
		if (!size)
			return nullptr;

		size_t totalSize = size + OVERHEAD;
		totalSize = (totalSize + 7) & ~7; // 8byte alignment

		if (totalSize < MIN_BLOCK_SIZE)
			totalSize = MIN_BLOCK_SIZE;

		Block* block = findBlock(totalSize);
		if (!block)
			return nullptr;

		removeFromFreeList(block);

		if (block->size >= totalSize + MIN_BLOCK_SIZE)
			splitBlock(block, totalSize);

		block->isFree = false;
		BlockFooter* footer = getFooter(block, block->size);
		footer->isFree = false;

		stats.allocated += block->size;

		return (char*)block + HEADER_SIZE;
	}

	void MemoryManager::deallocate(void* obj)
	{
		if (!obj)
		{
			std::cerr << "MemoryManger::deallocate error > invalid memory block ptr\n";
			return;
		}

		Block* block = (Block*)((char*)obj - HEADER_SIZE);

		if (!isValidBlock(block))
		{
			std::cerr << "MemoryManager::deallocate error > Invalid block\n";
			return;
		}

		if (block->isFree)
		{
			std::cerr << "MemoryManager::deallocate error > Double free detected\n";
			return;
		}

		block->isFree = true;
		block->next = nullptr;
		block->prev = nullptr;
		BlockFooter* footer = getFooter(block, block->size);
		footer->isFree = true;

		stats.allocated -= block->size;
		block = coalesce(block);
		addToFreeList(block);
	}

	void MemoryManager::printStats(void)
	{
		std::cout << "Memory Pool Statistics:\n";
		std::cout << "  Total pool size: " << stats.poolSize << " bytes\n";
		std::cout << "  Allocated: " << stats.allocated << " bytes\n";
		std::cout << "  Free: " << (stats.poolSize - stats.allocated) << " bytes\n";
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
	}

	int MemoryManager::getSizeClass(size_t size)
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

	size_t MemoryManager::getClassSize(int classIdx)
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

	Block* MemoryManager::findBlock(size_t totalSize)
	{
		int classIdx = getSizeClass(totalSize);

		for (int i = classIdx; i < NUM_CLASSES; ++i)
		{
			Block* block = freeLists[i];

			while (block)
			{
				if (block->size >= totalSize)
					return block;
				block = block->next;
			}
		}
		return nullptr;
	}

	void MemoryManager::splitBlock(Block* block, size_t size)
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

			BlockFooter* newFooter = getFooter(newBlock, newBlock->size);
			newFooter->size = remainingSize;
			newFooter->isFree = true;

			addToFreeList(newBlock);
		}
	}

	void MemoryManager::removeFromFreeList(Block* block)
	{
		if(!block || !isValidBlock(block))
		{
			std::cerr << "MemoryManager::removeFromFreeList: Invalid block pointer!\n";
			return;
		}

		if (!block->isFree)
		{
			std::cerr << "MemoryManager::removeFromFreeList: Block is not free!\n";
			return;
		}

		int classIdx = getSizeClass(block->size);

		if (block->prev)
		{
			if (!isValidBlock(block->prev) || !block->prev->isFree)
			{
				std::cerr << "ERROR: Corrupted prev pointer!\n";
				block->prev = nullptr;
			}
			else
				block->prev->next = block->next;
		}
		else
			freeLists[classIdx] = block->next;

		if (block->next)
		{
			if (!isValidBlock(block->next) || !block->next->isFree)
			{
				std::cerr << "ERROR: Corrupted next pointer!\n";
				block->next = nullptr;
			}
			else
				block->next->prev = block->prev;
		}

		block->next = nullptr;
		block->prev = nullptr;
	}

	void MemoryManager::addToFreeList(Block* block)
	{
		int classIdx = getSizeClass(block->size);

		block->prev = nullptr;
		block->next = freeLists[classIdx];

		if (freeLists[classIdx])
			freeLists[classIdx]->prev = block;

		freeLists[classIdx] = block;
	}

	Block* MemoryManager::coalesce(Block* block)
	{
		Block* prev = getPrevBlock(block);
		Block* next = getNextBlock(block);

		bool prevFree = prev && prev->isFree;
		bool nextFree = next && next->isFree;

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

	BlockFooter* MemoryManager::getFooter(void* blockStart, size_t blockSize)
	{
		return (BlockFooter*)((char*)blockStart + blockSize - FOOTER_SIZE);
	}

	Block* MemoryManager::getPrevBlock(Block* block)
	{
		if ((void*)block <= poolStart)
			return nullptr;

		BlockFooter* prevFooter = (BlockFooter*)((char*)block - FOOTER_SIZE);

		if (!isValidBlock(prevFooter))
			return nullptr;

		Block* prevBlock = (Block*)((char*)block - prevFooter->size);

		if (!isValidBlock(prevBlock))
			return nullptr;

		return prevBlock;
	}

	Block* MemoryManager::getNextBlock(Block* block)
	{
		Block* nextBlock = (Block*)((char*)block + block->size);

		if ((void*)nextBlock >= poolEnd)
			return nullptr;

		if (!isValidBlock(nextBlock))
			return nullptr;

		return nextBlock;
	}

	bool MemoryManager::isValidBlock(void* ptr)
	{
		return ptr >= poolStart && ptr < poolEnd;
	}
}

