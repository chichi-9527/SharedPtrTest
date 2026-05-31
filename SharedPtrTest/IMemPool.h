#pragma once

#include <cstdint>
#include <span>
#include <vector>
#include <functional>
#include <algorithm>
#include <utility>
#include <initializer_list>
#include <array>
#include <mutex>
#include <new>
#include <limits>
#include <concepts>
#include <type_traits>
#include <cassert>

#ifndef ALIGN_4K
#define ALIGN_4K(size) (((size) + 4095) & ~4095)
#endif // !ALIGN_4K

static constexpr uint32_t GENERAL_POOL_BLOCK_SIZE = 4096;

#define MSVC_STR2(x) #x
#define MSVC_STR1(x) MSVC_STR2(x)
#define TODO_REMINDER(msg) __pragma(message(__FILE__ "(" MSVC_STR1(__LINE__) ") : warning: " msg))

class IMemPool;

template<typename T>
class IUserPtr 
{
public:
	using ElementType = T;

	IUserPtr() = default;
	IUserPtr(std::nullptr_t)
		: _ptr(nullptr), _used_node_index(UINT32_MAX), _pool(nullptr)
	{}
	// 固定大小池的构造函数 (直接保存原生指针)
	explicit IUserPtr(T* raw_ptr)
		: _ptr(raw_ptr), _used_node_index(UINT32_MAX), _pool(nullptr)
	{}
	// 通用池的构造函数 (保存 Node 索引与池指针)
	IUserPtr(uint32_t used_node_index, IMemPool* pool)
		: _ptr(nullptr), _used_node_index(used_node_index), _pool(pool)
	{}

	T* get() const
	{
		if (_used_node_index == UINT32_MAX)
		{
			return _ptr;
		}
		return reinterpret_cast<T*>(_pool->ResolveGeneralPtr(_used_node_index));
	}

	// 解引用与箭头操作符重载
	T& operator*() const { return *get(); }
	T* operator->() const { return get(); }

	explicit operator bool() const { return get() != nullptr; }

	uint32_t GetUsedNodeIndex() const { return _used_node_index; }

	// 比较操作符
	bool operator==(const IUserPtr& other) const { return get() == other.get(); }
	bool operator!=(const IUserPtr& other) const { return get() != other.get(); }
	bool operator==(std::nullptr_t) const { return get() == nullptr; }
	bool operator!=(std::nullptr_t) const { return get() != nullptr; }
	
private:
	T* _ptr = nullptr;
	uint32_t _used_node_index = UINT32_MAX;
	IMemPool* _pool = nullptr;

};


class IMemPool
{
	struct BlockInfo
	{
		// UINT32_MAX 表示空闲，非 UINT32_MAX 表示已用起始块，值为 UsedNode 索引
		uint32_t UsedNodeIndex = 0;

	};

	struct FreeNode
	{
		uint32_t FreeBlockCount = 0;
		uint32_t PreNodeIndex = UINT32_MAX;
		uint32_t NextNodeIndex = UINT32_MAX;
	};

	struct UsedNode
	{
		void* UserPtr = nullptr;
		void (*RelocCallBack)(void* dst, void* src, uint32_t count) = nullptr;

		uint32_t UserCount = 0;
		uint32_t StartIndex = 0;
		uint32_t UsedCount = 0;
		uint32_t PreNode = 0;
		uint32_t NextNode = 0;

	};

	struct AllocOut
	{
		void* UserPtr = nullptr;
		uint32_t StartIndex = 0;
	};

public:

	// default count all 16384
	// count must >= 3 because first is free list head and last is next pool ptr
	// can use count is count - 2
	struct MemClassCounts
	{
		uint32_t BlockCountByte8 = 16384;
		uint32_t BlockCountByte16 = 16384;
		uint32_t BlockCountByte32 = 16384;
		uint32_t BlockCountByte64 = 16384;
		uint32_t BlockCountByte128 = 16384;
		uint32_t BlockCountByte256 = 16384;
		uint32_t BlockCountByte512 = 16384;
		uint32_t BlockCountByte1024 = 16384;

		// default general pool block size = 4kb
		uint32_t BlockCountGeneral = 16384;

		MemClassCounts() = default;
		MemClassCounts(uint32_t blockCountByte8,
		uint32_t blockCountByte16,
		uint32_t blockCountByte32,
		uint32_t blockCountByte64,
		uint32_t blockCountByte128,
		uint32_t blockCountByte256,
		uint32_t blockCountByte512,
		uint32_t blockCountByte1024,
		uint32_t blockCountGeneral)
			: BlockCountByte8(blockCountByte8)
			, BlockCountByte16(blockCountByte16)
			, BlockCountByte32(blockCountByte32)
			, BlockCountByte64(blockCountByte64)
			, BlockCountByte128(blockCountByte128)
			, BlockCountByte256(blockCountByte256)
			, BlockCountByte512(blockCountByte512)
			, BlockCountByte1024(blockCountByte1024)
			, BlockCountGeneral(blockCountGeneral)
		{}

	}; // MemClassCounts

private:
	IMemPool(MemClassCounts _counts)
		: _mem_class_counts(_counts)
		, _block_byte8_ptr(nullptr)
		, _block_byte16_ptr(nullptr)
		, _block_byte32_ptr(nullptr)
		, _block_byte64_ptr(nullptr)
		, _block_byte128_ptr(nullptr)
		, _block_byte256_ptr(nullptr)
		, _block_byte512_ptr(nullptr)
		, _block_byte1024_ptr(nullptr)
		, _block_general_ptr(nullptr)
		, _general_pool_ptr(nullptr)
		, _free_list_head_general(0)
		, _free_used_node_list(0)
		, _used_list_head(UINT32_MAX)
	{}

public:
	~IMemPool() = default;

	static IMemPool* CreatePool(MemClassCounts _counts = MemClassCounts())
	{
		IMemPool* pool = new IMemPool(_counts);

		auto init_fixed_pool = [](void*& ptr, size_t blockNum, size_t blockSize) -> bool {
			try { ptr = ::operator new(blockNum * blockSize); }
			catch (...) { return false; }
			*reinterpret_cast<uint32_t*>(ptr) = 1;
			for (size_t i = 1; i < blockNum - 2; ++i)
			{
				*reinterpret_cast<uint32_t*>((std::byte*)ptr + i * blockSize) = (uint32_t)i + 1;
			}
			*reinterpret_cast<uint32_t*>((std::byte*)ptr + (blockNum - 2) * blockSize) = 0;
			*reinterpret_cast<void**>((std::byte*)ptr + (blockNum - 1) * blockSize) = nullptr;
			return true;
			};

		if (!init_fixed_pool(pool->_block_byte8_ptr, _counts.BlockCountByte8, 8) ||
			!init_fixed_pool(pool->_block_byte16_ptr, _counts.BlockCountByte16, 16) ||
			!init_fixed_pool(pool->_block_byte32_ptr, _counts.BlockCountByte32, 32) ||
			!init_fixed_pool(pool->_block_byte64_ptr, _counts.BlockCountByte64, 64) ||
			!init_fixed_pool(pool->_block_byte128_ptr, _counts.BlockCountByte128, 128) ||
			!init_fixed_pool(pool->_block_byte256_ptr, _counts.BlockCountByte256, 256) ||
			!init_fixed_pool(pool->_block_byte512_ptr, _counts.BlockCountByte512, 512) ||
			!init_fixed_pool(pool->_block_byte1024_ptr, _counts.BlockCountByte1024, 1024))
		{
			DestroyPool(pool);
			return nullptr;
		}

		// general pool / byte4096
		size_t blockNum4096 = (size_t)_counts.BlockCountGeneral;

		size_t blockInfoSize = ALIGN_4K(blockNum4096 * sizeof(BlockInfo));
		size_t usedNodeSize = ALIGN_4K(blockNum4096 * sizeof(UsedNode));
		size_t dataBufferSize = blockNum4096 * GENERAL_POOL_BLOCK_SIZE;

		size_t totalSize = blockInfoSize + usedNodeSize + dataBufferSize;

		void* byte4096Ptr = nullptr;
		try
		{
			byte4096Ptr = ::operator new(totalSize, std::align_val_t{ 4096 });
		}
		catch (const std::exception&)
		{
			DestroyPool(pool);
			return nullptr;
		}

		pool->_general_pool_ptr = byte4096Ptr;

		size_t offset = 0;
		pool->_block_info = { reinterpret_cast<BlockInfo*>(byte4096Ptr), blockNum4096 };
		std::memset(pool->_block_info.data(), 0xFF, pool->_block_info.size_bytes());
		offset += blockInfoSize;

		pool->_used_nodes = { reinterpret_cast<UsedNode*>((std::byte*)byte4096Ptr + offset), blockNum4096 };
		std::memset(pool->_used_nodes.data(), 0, pool->_used_nodes.size_bytes());
		// init used nodes
		size_t i = 0;
		for (; i < pool->_used_nodes.size() - 1; ++i)
		{
			pool->_used_nodes[i].NextNode = (uint32_t)i + 1;
		}
		pool->_used_nodes[i].NextNode = UINT32_MAX;
		offset += usedNodeSize;

		pool->_block_general_ptr = (void*)((std::byte*)byte4096Ptr + offset);
		// update free node
		FreeNode* header = reinterpret_cast<FreeNode*>(pool->_block_general_ptr);
		header->FreeBlockCount = (uint32_t)blockNum4096;
		header->NextNodeIndex = UINT32_MAX;
		header->PreNodeIndex = UINT32_MAX;

		return pool;
	}

	static void DestroyPool(IMemPool* pool)
	{
		// byte4096
		if (pool->_general_pool_ptr)
		{
			::operator delete(pool->_general_pool_ptr, std::align_val_t{ 4096 });
			pool->_general_pool_ptr = nullptr;
		}

		auto free_fixed_chain = [](void* ptr, uint32_t count, size_t size) {
			void* nextPtr = nullptr;
			while (ptr)
			{
				nextPtr = *reinterpret_cast<void**>((std::byte*)ptr + (size_t)(count - 1) * size);
				::operator delete(ptr);
				ptr = nextPtr;
			}
			};
		free_fixed_chain(pool->_block_byte8_ptr, pool->_mem_class_counts.BlockCountByte8, 8);
		free_fixed_chain(pool->_block_byte16_ptr, pool->_mem_class_counts.BlockCountByte16, 16);
		free_fixed_chain(pool->_block_byte32_ptr, pool->_mem_class_counts.BlockCountByte32, 32);
		free_fixed_chain(pool->_block_byte64_ptr, pool->_mem_class_counts.BlockCountByte64, 64);
		free_fixed_chain(pool->_block_byte128_ptr, pool->_mem_class_counts.BlockCountByte128, 128);
		free_fixed_chain(pool->_block_byte256_ptr, pool->_mem_class_counts.BlockCountByte256, 256);
		free_fixed_chain(pool->_block_byte512_ptr, pool->_mem_class_counts.BlockCountByte512, 512);
		free_fixed_chain(pool->_block_byte1024_ptr, pool->_mem_class_counts.BlockCountByte1024, 1024);
		delete pool;

	}

	template<typename ClassName, typename... Args>
	IUserPtr<ClassName> New(std::uint32_t n, Args&&... args)
	{
		if (n == 0)
		{
			return IUserPtr<ClassName>();
		}

		size_t needByte = (size_t)n * sizeof(ClassName);
		ClassName* resPtr = nullptr;
		size_t usedNodeIndex = UINT32_MAX;

		if (needByte > 1024)
		{
			uint32_t needBlock = (uint32_t)((needByte + GENERAL_POOL_BLOCK_SIZE - 1) / GENERAL_POOL_BLOCK_SIZE);
			AllocOut out = _allocate_from_general_pool(needBlock);
			if (out.UserPtr == nullptr)
			{
				return IUserPtr<ClassName>();
			}
			// add used node
			assert(_free_used_node_list != UINT32_MAX && "More _used_nodes space is needed");

			usedNodeIndex = _free_used_node_list;
			_free_used_node_list = _used_nodes[usedNodeIndex].NextNode;

			UsedNode& node = _used_nodes[usedNodeIndex];
			node.StartIndex = out.StartIndex;
			node.UsedCount = needBlock;
			node.UserPtr = out.UserPtr;
			node.UserCount = n;
			node.PreNode = UINT32_MAX;
			node.NextNode = _used_list_head;

			node.RelocCallBack = [](void* dst, void* src, uint32_t count) {
				if constexpr (std::is_trivially_copyable_v<ClassName>)
				{
					std::memmove(dst, src, (size_t)count * sizeof(ClassName));
				}
				else
				{
					// 非平铺复制对象：使用严格的移动构造 + 析构
					ClassName* srcArray = static_cast<ClassName*>(src);
					ClassName* dstArray = static_cast<ClassName*>(dst);
					for (uint32_t i = 0; i < count; ++i)
					{
						if constexpr (std::is_nothrow_move_constructible_v<ClassName>)
						{
							::new (&dstArray[i]) ClassName(std::move(srcArray[i]));
						}
						else
						{
							::new (&dstArray[i]) ClassName(srcArray[i]);
						}
						srcArray[i].~ClassName();
					}
				}
				};

			if (_used_list_head != UINT32_MAX)
			{
				_used_nodes[_used_list_head].PreNode = (uint32_t)usedNodeIndex;
			}
			_used_list_head = (uint32_t)usedNodeIndex;

			resPtr = (ClassName*)out.UserPtr;

		}
		else
		{
			size_t idx = _get_bucket_index(needByte);
			resPtr = (ClassName*)_take_block_fixed_size_pool(idx, _get_fixed_pool_head(idx), _get_fixed_block_size(idx), _get_fixed_block_count(idx));
		}

		if (!resPtr) return IUserPtr<ClassName>();

		uint32_t constructed = 0;
		ClassName* ptr = nullptr;
		try
		{
			ptr = new(resPtr)ClassName(std::forward<Args>(args)...);
			++constructed;
			for (uint32_t i = 1; i < n; ++i)
			{
				ClassName* _ = new((void*)(ptr + i))ClassName(std::forward<Args>(args)...);
				++constructed;
			}
		}
		catch (...)
		{
			for (uint32_t i = 0; i < constructed; ++i)
				ptr[i].~ClassName();

			return IUserPtr<ClassName>();
		}

		// 返回封装好的 UserPtr
		if (needByte > 1024)
		{
			return IUserPtr<ClassName>((uint32_t)usedNodeIndex, this);
		}
		return IUserPtr<ClassName>(resPtr);

	}

	template<typename ClassName, typename TInit>
	IUserPtr<ClassName> New(std::uint32_t n, std::initializer_list<TInit> list)
	{
		if (n == 0 || n != list.size())
		{
			return IUserPtr<ClassName>();
		}

		size_t needByte = (size_t)n * sizeof(ClassName);
		ClassName* resPtr = nullptr;
		size_t usedNodeIndex = UINT32_MAX;

		if (needByte > 1024)
		{
			uint32_t needBlock = (uint32_t)((needByte + GENERAL_POOL_BLOCK_SIZE - 1) / GENERAL_POOL_BLOCK_SIZE);
			AllocOut out = _allocate_from_general_pool(needBlock);
			if (out.UserPtr == nullptr)
			{
				return IUserPtr<ClassName>();
			}
			// add used node
			assert(_free_used_node_list != UINT32_MAX && "More _used_nodes space is needed");

			usedNodeIndex = _free_used_node_list;
			_free_used_node_list = _used_nodes[usedNodeIndex].NextNode;

			UsedNode& node = _used_nodes[usedNodeIndex];
			node.StartIndex = out.StartIndex;
			node.UsedCount = needBlock;
			node.UserPtr = out.UserPtr;
			node.UserCount = n;
			node.PreNode = UINT32_MAX;
			node.NextNode = _used_list_head;

			node.RelocCallBack = [](void* dst, void* src, uint32_t count) {
				if constexpr (std::is_trivially_copyable_v<ClassName>)
				{
					std::memmove(dst, src, (size_t)count * sizeof(ClassName));
				}
				else
				{
					// 非平铺复制对象：使用严格的移动构造 + 析构
					ClassName* srcArray = static_cast<ClassName*>(src);
					ClassName* dstArray = static_cast<ClassName*>(dst);
					for (uint32_t i = 0; i < count; ++i)
					{
						if constexpr (std::is_nothrow_move_constructible_v<ClassName>)
						{
							::new (&dstArray[i]) ClassName(std::move(srcArray[i]));
						}
						else
						{
							::new (&dstArray[i]) ClassName(srcArray[i]);
						}
						srcArray[i].~ClassName();
					}
				}
				};
			

			if (_used_list_head != UINT32_MAX)
			{
				_used_nodes[_used_list_head].NextNode = usedNodeIndex;
			}
			_used_list_head = usedNodeIndex;

			resPtr = (ClassName*)out.UserPtr;

		}
		else
		{
			size_t idx = _get_bucket_index(needByte);
			resPtr = (ClassName*)_take_block_fixed_size_pool(idx, _get_fixed_pool_head(idx), _get_fixed_block_size(idx), _get_fixed_block_count(idx));
		}

		if (!resPtr) return IUserPtr<ClassName>();

		uint32_t constructed = 0;
		ClassName* ptr = nullptr;
		try
		{
			const auto& first = list.begin();
			ptr = new(resPtr)ClassName(*first);
			++constructed;
			uint32_t i = 1;
			for (auto it = list.begin() + 1; it != list.end(); ++it)
			{
				ClassName* _ = new((void*)(ptr + i))ClassName(*it);
				++i;
				++constructed;
			}

		}
		catch (...)
		{
			for (uint32_t i = 0; i < constructed; ++i)
				ptr[i].~ClassName();

			return IUserPtr<ClassName>();
		}

		// 返回封装好的 UserPtr
		if (needByte > 1024)
		{
			return IUserPtr<ClassName>(usedNodeIndex, this);
		}
		return IUserPtr<ClassName>(resPtr);
	}

	template<typename ClassName>
	void Delete(IUserPtr<ClassName>& p, std::uint32_t n)
	{
		if (!p || n == 0) return;
		ClassName* userPtr = p.get();

		if constexpr (!std::is_trivially_destructible_v<ClassName>)
		{
			size_t usedByte = (size_t)n * sizeof(ClassName);
			if (IsPtrInPool(userPtr, usedByte))
			{
				for (uint32_t i = n; i > 0; --i) { userPtr[i - 1].~ClassName(); }
			}
			else
			{
				return;
			}
		}
		Deallocate<ClassName>(p, n);

	}

	bool IsPtrInPool(void* ptr, size_t byte_count)
	{
		if (byte_count > 1024)
		{
			std::lock_guard<std::mutex> lock(_general_mutex);
			return (uint32_t)(((uintptr_t)ptr - (uintptr_t)_block_general_ptr) / GENERAL_POOL_BLOCK_SIZE) < _mem_class_counts.BlockCountGeneral;
		}
		size_t idx = _get_bucket_index(byte_count);
		return _is_ptr_in_fixed_size_pool(idx, ptr, *_get_fixed_pool_head(idx), _get_fixed_block_size(idx), _get_fixed_block_count(idx));
	}

	// 不会调用构造函数
	// 因此碎片整理时无法移动构造，对于 包含虚函数表、内部指针、自引用、或者在构造/析构中有特殊逻辑等的对象时无法编译
	template<typename T>
	IUserPtr<T> Allocate(std::uint32_t n)
	{
		if (n == 0) return nullptr;

		size_t needByte = (size_t)n * sizeof(T);

		if (needByte > 1024)
		{
			uint32_t needBlocks = (uint32_t)((needByte + GENERAL_POOL_BLOCK_SIZE - 1) / GENERAL_POOL_BLOCK_SIZE);

			AllocOut out = _allocate_from_general_pool(needBlocks);
			if (out.UserPtr == nullptr)
			{
				return IUserPtr<T>();
			}

			// add used node

			assert(_free_used_node_list != UINT32_MAX && "More _used_nodes space is needed");

			uint32_t newIndex = _free_used_node_list;
			_free_used_node_list = _used_nodes[newIndex].NextNode;

			UsedNode& node = _used_nodes[newIndex];
			node.StartIndex = out.StartIndex;
			node.UsedCount = needBlocks;
			node.UserPtr = out.UserPtr;
			node.UserCount = n;
			node.PreNode = UINT32_MAX;
			node.NextNode = _used_list_head;

			/*if constexpr (std::is_trivially_copyable_v<T>)
			{
				TriggerWarning<T> warn;
			}*/

#if defined(__GNUC__) || defined(__clang__) || (__cplusplus >= 202302L)
#warning "the generic pool may throw an undefined error when dealing with types that are not easily copyable due to defragmentation/move."
#elif defined(_MSC_VER)
			TODO_REMINDER("the generic pool may throw an undefined error when dealing with types that are not easily copyable due to defragmentation/move.")
#endif

			node.RelocCallBack = [](void* dst, void* src, uint32_t count) {
				std::memmove(dst, src, (size_t)count * sizeof(T));
				};

			if (_used_list_head != UINT32_MAX)
			{
				_used_nodes[_used_list_head].PreNode = newIndex;
			}
			_used_list_head = newIndex;

			// update block info
			_block_info[out.StartIndex].UsedNodeIndex = newIndex;

			return IUserPtr<T>(newIndex, this);
			
		}
		
		size_t idx = _get_bucket_index(needByte);
		T* ptr = (T*)_take_block_fixed_size_pool(idx, _get_fixed_pool_head(idx), _get_fixed_block_size(idx), _get_fixed_block_count(idx));
		return IUserPtr<T>(ptr);

	}

	template<typename T>
	void Deallocate(IUserPtr<T>& p, std::uint32_t n)
	{
		if (n == 0 || !p) return;

		size_t usedByte = (size_t)n * sizeof(T);

		if (usedByte > 1024)
		{
			std::lock_guard<std::mutex> lock(_general_mutex);

			uint32_t usedBlocks = (uint32_t)((usedByte + GENERAL_POOL_BLOCK_SIZE - 1) / GENERAL_POOL_BLOCK_SIZE);

			uint32_t index = p.GetUsedNodeIndex();
			uint32_t startBlock = _used_nodes[index].StartIndex;

			_deallocate_blocks_general_pool(startBlock, usedBlocks);

			_remove_used_node(index);

			// update block info
			_block_info[startBlock].UsedNodeIndex = UINT32_MAX;

		}
		else
		{
			size_t idx = _get_bucket_index(usedByte);
			_put_back_block_fixed_size_pool(idx, (void*)p.get(), *_get_fixed_pool_head(idx), _get_fixed_block_size(idx), _get_fixed_block_count(idx));
		}

		p = nullptr;

	}



	void* ResolveGeneralPtr(uint32_t index) const
	{
		if (index == UINT32_MAX || index >= _used_nodes.size()) [[unlikely]]
		{
			return nullptr;
		}
		
		return _used_nodes[index].UserPtr;
	}

	// test
	void DefragmentGeneralPool()
	{
		std::lock_guard<std::mutex> lock(_general_mutex);
		_defragment_general_pool();
	}

	bool RecreateGeneralPool(uint32_t new_block_count)
	{
		std::lock_guard<std::mutex> lock(_general_mutex);
		return _recreate_general_pool(new_block_count);
	}


private:

	/// <summary>
	/// 
	/// </summary>
	/// <param name="block_ptr"></param>
	/// <param name="block_size"></param>
	/// <param name="block_count"></param>
	/// <returns> 返回 nullptr 时说明 所有此 block_size 池已满，且分配新池时外部堆内存不足</returns>
	void* _take_block_fixed_size_pool(size_t idx, void** block_ptr, uint32_t block_size, uint32_t block_count)
	{
		std::lock_guard<std::mutex> lock(_fixed_mutexes[idx]);

		void* currentPool = *block_ptr;
		void* lastPool = nullptr;
		while (currentPool)
		{
			uint32_t& freeHead = *reinterpret_cast<uint32_t*>(currentPool);
			if (freeHead != 0)
			{
				void* freeBlock = (void*)((std::byte*)currentPool + (size_t)freeHead * block_size);
				freeHead = *reinterpret_cast<uint32_t*>(freeBlock);
				return freeBlock;
			}

			lastPool = currentPool;
			// next pool
			currentPool = *reinterpret_cast<void**>((std::byte*)currentPool + (size_t)(block_count - 1) * block_size);
		}

		// 所有池已满，分配新池
		void* newPool = nullptr;
		try
		{
			newPool = ::operator new((size_t)block_count * block_size);
		}
		catch (const std::exception&)
		{
			return nullptr;
		}

		// init
		*reinterpret_cast<uint32_t*>(newPool) = 1;

		for (uint32_t i = 1; i < block_count - 2; ++i)
		{
			*reinterpret_cast<uint32_t*>((std::byte*)newPool + i * block_size) = i + 1;
		}
		*reinterpret_cast<uint32_t*>((std::byte*)newPool + (size_t)(block_count - 2) * block_size) = 0;
		// linkPtr
		*reinterpret_cast<void**>((std::byte*)newPool + (size_t)(block_count - 1) * block_size) = nullptr;
		if (lastPool)
			*reinterpret_cast<void**>((std::byte*)lastPool + (size_t)(block_count - 1) * block_size) = newPool;
		else
		{
			*block_ptr = newPool;
		}

		// take from newPool
		void* result = (void*)((std::byte*)newPool + block_size);
		*reinterpret_cast<uint32_t*>(newPool) = *reinterpret_cast<uint32_t*>(result);

		return result;
	}

	void _put_back_block_fixed_size_pool(size_t idx, void* user_ptr, void* block_ptr, uint32_t block_size, uint32_t block_count)
	{
		std::lock_guard<std::mutex> lock(_fixed_mutexes[idx]);

		void* userPtr = user_ptr;
		void* pool = block_ptr;
		while (pool)
		{
			// start is block 2
			std::byte* poolStart = (std::byte*)pool + block_size;
			// end is block block_count-1
			std::byte* poolEnd = (std::byte*)pool + (size_t)(block_count - 1) * block_size;

			if (userPtr >= poolStart && userPtr < poolEnd)
			{
				uint32_t index = static_cast<uint32_t>((std::byte*)userPtr - (std::byte*)pool) / block_size;

				uint32_t oldHead = *reinterpret_cast<uint32_t*>(pool);
				*reinterpret_cast<uint32_t*>(userPtr) = oldHead;
				*reinterpret_cast<uint32_t*>(pool) = index;

			}

			// go to next pool
			pool = *reinterpret_cast<void**>(poolEnd);

		}

		// not found
		// do nothing

	}

	bool _is_ptr_in_fixed_size_pool(size_t idx, void* ptr, void* block_ptr, uint32_t block_size, uint32_t block_count) const
	{
		std::lock_guard<std::mutex> lock(_fixed_mutexes[idx]);
		void* pool = block_ptr;
		while (pool)
		{
			std::byte* poolStart = (std::byte*)pool + block_size;
			std::byte* poolEnd = (std::byte*)pool + (size_t)(block_count - 1) * block_size;
			if (ptr >= poolStart && ptr < poolEnd) return true;
			pool = *reinterpret_cast<void**>(poolEnd);
		}
		return false;
	}

	AllocOut _allocate_from_general_pool(uint32_t need_blocks)
	{
		
		if (need_blocks == 0) return { nullptr, 0 };

		uint32_t prev = UINT32_MAX;
		uint32_t cur = _free_list_head_general;

		while (cur != UINT32_MAX)
		{
			FreeNode* header = _get_free_header(cur);
			if (header->FreeBlockCount >= need_blocks)
			{
				break;
			}
			prev = cur;
			cur = header->NextNodeIndex;
		}

		// 未找到
		if (cur == UINT32_MAX)
		{
			_defragment_general_pool();

			cur = _free_list_head_general;

			bool needRecreatePool = true;
			if (cur != UINT32_MAX)
			{
				FreeNode* hdr = _get_free_header(cur);
				if (hdr->FreeBlockCount >= need_blocks)
				{
					needRecreatePool = false;
				}
			}

			if (needRecreatePool)
			{
				const uint32_t currentBlocks = _mem_class_counts.BlockCountGeneral;
				size_t minTotalBlocks = need_blocks;

				size_t i = 1;
				size_t newTotal = static_cast<size_t>(currentBlocks) * (1ULL << i);
				while (newTotal < minTotalBlocks && newTotal <= UINT32_MAX)
				{
					++i;
					newTotal = static_cast<size_t>(currentBlocks) * (1ULL << i);
				}
				if (newTotal > UINT32_MAX || newTotal < minTotalBlocks)
				{
					return { nullptr, 0 };
				}

				if (!_recreate_general_pool((uint32_t)newTotal))
				{
					return { nullptr, 0 };
				}

				cur = _free_list_head_general;
				if (cur == UINT32_MAX)
				{
					return { nullptr, 0 };
				}
				FreeNode* hdr = _get_free_header(cur);

				assert(hdr->FreeBlockCount >= need_blocks);
			} // needRecreatePool

		} // (cur == UINT32_MAX)

		FreeNode* curHeader = _get_free_header(cur);
		uint32_t allocStart = cur;
		uint32_t remaining = curHeader->FreeBlockCount - need_blocks;

		if (remaining > 0)
		{
			// cut
			uint32_t newStart = cur + need_blocks;
			FreeNode* newHeader = _get_free_header(newStart);
			newHeader->PreNodeIndex = curHeader->PreNodeIndex;
			newHeader->NextNodeIndex = curHeader->NextNodeIndex;
			newHeader->FreeBlockCount = remaining;

			// update
			if (curHeader->PreNodeIndex != UINT32_MAX)
			{
				_get_free_header(curHeader->PreNodeIndex)->NextNodeIndex = newStart;
			}
			else
			{
				_free_list_head_general = newStart;
			}

			if (curHeader->NextNodeIndex != UINT32_MAX)
			{
				_get_free_header(curHeader->NextNodeIndex)->PreNodeIndex = newStart;
			}
		
		}
		else
		{
			if (curHeader->PreNodeIndex != UINT32_MAX)
			{
				_get_free_header(curHeader->PreNodeIndex)->NextNodeIndex = curHeader->NextNodeIndex;
			}
			else
			{
				_free_list_head_general = curHeader->NextNodeIndex;
			}

			if (curHeader->NextNodeIndex != UINT32_MAX)
			{
				_get_free_header(curHeader->NextNodeIndex)->PreNodeIndex = curHeader->PreNodeIndex;
			}
		}

		return { (void*)(((std::byte*)_block_general_ptr + (size_t)allocStart * GENERAL_POOL_BLOCK_SIZE)), allocStart };

	}

	void _deallocate_blocks_general_pool(uint32_t start_block, uint32_t block_count)
	{
		if (block_count == 0) return;

		uint32_t prev = UINT32_MAX;
		uint32_t cur = _free_list_head_general;
		while (cur != UINT32_MAX && cur < start_block)
		{
			prev = cur;
			cur = _get_free_header(cur)->NextNodeIndex;
		}

		// 合并前驱
		if (prev != UINT32_MAX)
		{
			FreeNode* preHeader = _get_free_header(prev);
			if (prev + preHeader->FreeBlockCount == start_block)
			{
				// 前驱合并
				start_block = prev;
				block_count += preHeader->FreeBlockCount;
				// 摘除 prev
				if (preHeader->PreNodeIndex != UINT32_MAX)
					_get_free_header(preHeader->PreNodeIndex)->NextNodeIndex = preHeader->NextNodeIndex;
				else
					_free_list_head_general = preHeader->NextNodeIndex;
				if (preHeader->NextNodeIndex != UINT32_MAX)
					_get_free_header(preHeader->NextNodeIndex)->PreNodeIndex = preHeader->PreNodeIndex;
				prev = preHeader->PreNodeIndex; // 更新
			}
		}

		// 合并后继
		if (cur != UINT32_MAX)
		{
			FreeNode* curHeader = _get_free_header(cur);
			if (start_block + block_count == cur)
			{
				block_count += curHeader->FreeBlockCount;
				// 摘除 cur
				if (curHeader->PreNodeIndex != UINT32_MAX)
					_get_free_header(curHeader->PreNodeIndex)->NextNodeIndex = curHeader->NextNodeIndex;
				else
					_free_list_head_general = curHeader->NextNodeIndex;
				if (curHeader->NextNodeIndex != UINT32_MAX)
					_get_free_header(curHeader->NextNodeIndex)->PreNodeIndex = curHeader->PreNodeIndex;
				cur = curHeader->NextNodeIndex;
			}
		}

		// 写入最终节点
		FreeNode* newHeader = _get_free_header(start_block);
		newHeader->FreeBlockCount = static_cast<uint32_t>(block_count);
		newHeader->PreNodeIndex = prev;
		newHeader->NextNodeIndex = cur;
		if (prev != UINT32_MAX) _get_free_header(prev)->NextNodeIndex = start_block;
		else _free_list_head_general = start_block;
		if (cur != UINT32_MAX) _get_free_header(cur)->PreNodeIndex = start_block;

	}

	// 碎片整理
	void _defragment_general_pool()
	{
		
		std::vector<UsedNode*> usedList;
		usedList.reserve(_mem_class_counts.BlockCountGeneral / 2);

		uint32_t cur = _used_list_head;
		while (cur != UINT32_MAX)
		{
			usedList.push_back(&_used_nodes[cur]);
			cur = _used_nodes[cur].NextNode;
		}

		if (usedList.empty()) return;

		// --- 按 StartIndex 升序排序（正常整理） ---
		std::sort(usedList.begin(), usedList.end(),
			[](const UsedNode* a, const UsedNode* b) {
				return a->StartIndex < b->StartIndex;
			});

		uint32_t newStart = 0;
		for (UsedNode* node : usedList)
		{
			if (node->StartIndex == newStart)
			{
				newStart += node->UsedCount;
				continue;
			}

			void* oldPtr = (void*)((std::byte*)_block_general_ptr + (size_t)node->StartIndex * GENERAL_POOL_BLOCK_SIZE);
			void* newPtr = (void*)((std::byte*)_block_general_ptr + (size_t)newStart * GENERAL_POOL_BLOCK_SIZE);
			
			if (node->RelocCallBack)
				node->RelocCallBack(newPtr, oldPtr, node->UserCount);

			node->UserPtr = newPtr;

			// update block info
			_block_info[node->StartIndex].UsedNodeIndex = UINT32_MAX;
			_block_info[newStart].UsedNodeIndex = static_cast<uint32_t>(node - _used_nodes.data());

			node->StartIndex = newStart;
			newStart += node->UsedCount;

		}

		// --- 重建空闲链表 ---
		uint32_t totalBlocks = _mem_class_counts.BlockCountGeneral;

		if (newStart < totalBlocks)
		{
			FreeNode* newFreeHeader = _get_free_header(newStart);
			newFreeHeader->PreNodeIndex = UINT32_MAX;
			newFreeHeader->NextNodeIndex = UINT32_MAX;
			newFreeHeader->FreeBlockCount = totalBlocks - newStart;
			_free_list_head_general = newStart;
		}
		else
		{
			_free_list_head_general = UINT32_MAX;
		}

	}

	void _remove_used_node(uint32_t index)
	{
		UsedNode& node = _used_nodes[index];
		if (node.PreNode != UINT32_MAX)
			_used_nodes[node.PreNode].NextNode = node.NextNode;
		else
			_used_list_head = node.NextNode;
		if (node.NextNode != UINT32_MAX)
			_used_nodes[node.NextNode].PreNode = node.PreNode;

		// 归还节点到 freelist
		node.NextNode = _free_used_node_list;
		_free_used_node_list = index;
		node.UserPtr = nullptr;
		node.StartIndex = 0;
		node.UsedCount = 0;
		node.RelocCallBack = nullptr;
	}

	void _rebuild_free_list(uint32_t start_from)
	{
		uint32_t total = (uint32_t)_block_info.size();
		uint32_t prev = UINT32_MAX;
		_free_list_head_general = UINT32_MAX;

		uint32_t i = start_from;   // 从上次整理后的位置开始，前面全是已用块
		while (i < total)
		{
			if (_block_info[i].UsedNodeIndex == UINT32_MAX)
			{
				uint32_t runStart = i;
				while (i < total && _block_info[i].UsedNodeIndex == UINT32_MAX) ++i;
				uint32_t count = i - runStart;

				FreeNode* hdr = _get_free_header(runStart);
				hdr->PreNodeIndex = prev;
				hdr->NextNodeIndex = UINT32_MAX;
				hdr->FreeBlockCount = count;

				if (prev != UINT32_MAX)
					_get_free_header(prev)->NextNodeIndex = runStart;
				else
					_free_list_head_general = runStart;
				prev = runStart;
			}
			else
			{
				++i;
			}
		}
	}

	bool _recreate_general_pool(uint32_t new_block_count)
	{
		// used node count = block count
		uint32_t oldBlockCount = _mem_class_counts.BlockCountGeneral;

		assert(new_block_count > oldBlockCount);

		size_t blockInfoSize = ALIGN_4K((size_t)new_block_count * sizeof(BlockInfo));
		size_t usedNodeSize = ALIGN_4K((size_t)new_block_count * sizeof(UsedNode));
		size_t dataBufferSize = (size_t)new_block_count * GENERAL_POOL_BLOCK_SIZE;

		size_t totalSize = blockInfoSize + usedNodeSize + dataBufferSize;

		void* newGeneralPoolPtr = nullptr;
		try
		{
			newGeneralPoolPtr = ::operator new(totalSize, std::align_val_t{ 4096 });
		}
		catch (const std::exception&)
		{
			return false;
		}

		// init new
		size_t offset = 0;
		std::span<BlockInfo> newBlockInfo{ reinterpret_cast<BlockInfo*>(newGeneralPoolPtr), new_block_count };
		std::memset(newBlockInfo.data(), 0xFF, newBlockInfo.size_bytes());
		offset += blockInfoSize;

		std::span<UsedNode> newUsedNodes{ reinterpret_cast<UsedNode*>((std::byte*)newGeneralPoolPtr + offset), new_block_count };
		std::memset(newUsedNodes.data(), 0, newUsedNodes.size_bytes());
		// init used nodes
		newUsedNodes[(size_t)new_block_count - 1].NextNode = UINT32_MAX;
		offset += usedNodeSize;

		void* newDataRegion = (void*)((std::byte*)newGeneralPoolPtr + offset);

		std::ranges::copy(_used_nodes, newUsedNodes.begin());

		uint32_t newFreeUsedNodeList = _free_used_node_list;

		for (uint32_t i = oldBlockCount; i < new_block_count; ++i)
		{
			newUsedNodes[i].NextNode = newFreeUsedNodeList;
			newFreeUsedNodeList = i;
		}

		void* oldDataRegion = _block_general_ptr;

		// move data block
		uint32_t curIndex = _used_list_head;
		while (curIndex != UINT32_MAX)
		{
			UsedNode& node = newUsedNodes[curIndex];

			uint32_t startIndex = node.StartIndex;
			uint32_t count = node.UsedCount;

			void* oldPtr = (void*)((std::byte*)oldDataRegion + (size_t)startIndex * GENERAL_POOL_BLOCK_SIZE);
			void* newPtr = (void*)((std::byte*)newDataRegion + (size_t)startIndex * GENERAL_POOL_BLOCK_SIZE);

			if (oldPtr != newPtr)
			{
				if (node.RelocCallBack)
					node.RelocCallBack(newPtr, oldPtr, node.UserCount);
			}

			node.UserPtr = newPtr;

			newBlockInfo[startIndex].UsedNodeIndex = curIndex;

			curIndex = node.NextNode;

		}

		// 重建侵入式空闲链表
		uint32_t newFreeListHead = UINT32_MAX;
		uint32_t prevFree = UINT32_MAX;
		uint32_t idx = 0;
		while (idx < new_block_count)
		{
			if (newBlockInfo[idx].UsedNodeIndex == UINT32_MAX)
			{
				uint32_t start = idx;
				while (idx < new_block_count && newBlockInfo[idx].UsedNodeIndex == UINT32_MAX)
					++idx;
				uint32_t count = idx - start;

				FreeNode* hdr = reinterpret_cast<FreeNode*>((std::byte*)newDataRegion + (size_t)start * GENERAL_POOL_BLOCK_SIZE);
				hdr->PreNodeIndex = prevFree;
				hdr->NextNodeIndex = UINT32_MAX;
				hdr->FreeBlockCount = count;

				if (prevFree != UINT32_MAX)
				{
					FreeNode* prevHdr = reinterpret_cast<FreeNode*>((std::byte*)newDataRegion + (size_t)prevFree * GENERAL_POOL_BLOCK_SIZE);
					prevHdr->NextNodeIndex = start;
				}
				else
				{
					newFreeListHead = start;
				}
				prevFree = start;
			}
			else
			{
				++idx;
			}
		}

		// update pool 
		_block_info = newBlockInfo;
		_used_nodes = newUsedNodes;
		_free_list_head_general = newFreeListHead;
		_free_used_node_list = newFreeUsedNodeList;
		_block_general_ptr = newDataRegion;

		// delete old pool
		if (_general_pool_ptr)
		{
			::operator delete(_general_pool_ptr, std::align_val_t{ 4096 });
		}
		_general_pool_ptr = newGeneralPoolPtr;
		_mem_class_counts.BlockCountGeneral = new_block_count;

		return true;

	}

	size_t _get_bucket_index(size_t byte_count) const
	{
		if (byte_count <= 8) return 0;
		if (byte_count <= 16) return 1;
		if (byte_count <= 32) return 2;
		if (byte_count <= 64) return 3;
		if (byte_count <= 128) return 4;
		if (byte_count <= 256) return 5;
		if (byte_count <= 512) return 6;
		return 7;
	}

	void** _get_fixed_pool_head(size_t idx)
	{
		std::array<void**, 8> heads = { &_block_byte8_ptr, &_block_byte16_ptr, &_block_byte32_ptr, &_block_byte64_ptr,
										&_block_byte128_ptr, &_block_byte256_ptr, &_block_byte512_ptr, &_block_byte1024_ptr };
		return heads[idx];
	}

	uint32_t _get_fixed_block_size(size_t idx) const { return 8 << idx; }

	uint32_t _get_fixed_block_count(size_t idx) const
	{
		switch (idx)
		{
		case 0: return _mem_class_counts.BlockCountByte8;
		case 1: return _mem_class_counts.BlockCountByte16;
		case 2: return _mem_class_counts.BlockCountByte32;
		case 3: return _mem_class_counts.BlockCountByte64;
		case 4: return _mem_class_counts.BlockCountByte128;
		case 5: return _mem_class_counts.BlockCountByte256;
		case 6: return _mem_class_counts.BlockCountByte512;
		case 7: return _mem_class_counts.BlockCountByte1024;
		default: return 0;
		}
	}

	FreeNode* _get_free_header(uint32_t index)
	{
		return reinterpret_cast<FreeNode*>((std::byte*)_block_general_ptr + (size_t)index * GENERAL_POOL_BLOCK_SIZE);
	}

private:

	void* _block_byte8_ptr;
	void* _block_byte16_ptr;
	void* _block_byte32_ptr;
	void* _block_byte64_ptr;
	void* _block_byte128_ptr;
	void* _block_byte256_ptr;
	void* _block_byte512_ptr;
	void* _block_byte1024_ptr;
	void* _block_general_ptr;

	void* _general_pool_ptr;

	std::span<BlockInfo> _block_info;
	std::span<UsedNode> _used_nodes;
	uint32_t _free_list_head_general;
	uint32_t _used_list_head;
	uint32_t _free_used_node_list;

	MemClassCounts _mem_class_counts;

	// 多线程同步组件（按 Bin 维度细化，极低冲突率）
	mutable std::array<std::mutex, 8> _fixed_mutexes;
	mutable std::mutex _general_mutex;

};

template<typename T>
class IMemPoolAllocator
{
public:
	using value_type = T;
	using pointer = IUserPtr<T>;

	IMemPoolAllocator(IMemPool* pool)
		: _pool(pool)
	{}

	template <typename U>
	IMemPoolAllocator(const IMemPoolAllocator<U>& other) : _pool(other._pool) {}

	/// <summary>
	/// 
	/// </summary>
	/// <param name="n"> : T count </param>
	/// <returns></returns>
	pointer allocate(std::uint32_t n)
	{
		if (n > std::numeric_limits<std::uint32_t>::max() / sizeof(T))
		{
			throw std::bad_alloc();
		}

		if (_pool)
			return _pool->Allocate<T>(n);
		return IUserPtr<T>();
	}
	/// <summary>
	/// 
	/// </summary>
	/// <param name="p"></param>
	/// <param name="n"> : T count </param>
	void deallocate(pointer p, std::uint32_t n)
	{
		if (_pool)
			_pool->Deallocate<T>(p, n);
	}

	bool operator==(const IMemPoolAllocator& other) const { return _pool == other._pool; }
	bool operator!=(const IMemPoolAllocator& other) const { return !(*this == other); }


private:
	IMemPool* _pool;
};


