#pragma once

#include <cstdint>
#include <utility>
#include <string_view>
#include <type_traits>
#include <stdexcept>

namespace ITools
{
	enum class PtrType  : uint32_t
	{
		UNDEFINED = 0,
		SHARED = 1,
		UNIQUE = 2
	};
}

class IPtrBase 
{
public:
	IPtrBase() = default;
	IPtrBase(const IPtrBase& other) = default;
	IPtrBase(IPtrBase&& other) noexcept = default;
	IPtrBase& operator=(const IPtrBase& other) = default;
	IPtrBase& operator=(IPtrBase&& other) noexcept = default;
	IPtrBase(ITools::PtrType type)
		: _type(type)
	{
	}

	std::uint32_t GetCount() const
	{
		return _count;
	}
protected:
	ITools::PtrType _type = ITools::PtrType::UNDEFINED;
	std::uint32_t _count = 1;
};

struct IPtr;
class IPtrManager
{
public:
	using ResolverFunc = void* (*)(void* state);
	using DeleterFunc = void  (*)(void* state);

	static IPtrManager& GetInstance();

	bool PtrIsValid(size_t ptr_id);

	size_t AddPtr(ResolverFunc resolver, void* resolverState, DeleterFunc  deleter, void* deleterState);
	size_t AddPtr(void* ptr);

	void SharedPtrInc(size_t ptr_id);
	void SharedPtrDec(size_t ptr_id);

	void WeakPtrInc(size_t ptr_id);
	void WeakPtrDec(size_t ptr_id);

	void* GetPtr(size_t ptr_id);
	bool GetPtrAndInc(size_t ptr_id, void*& ptr);
	bool GetPtrAndWeakInc(size_t ptr_id, void*& ptr);

	std::string_view GetSafeSizeTTypeName() const;
private:
	IPtrManager();
};

namespace detail
{
	static void* DefaultResolver(void* state) { return state; }
	template<typename T>
	static void DefaultDeleter(void* state)
	{
		T* ptr = static_cast<T*>(state);
		delete ptr;
	}
	
	template<typename T, typename PtrType>
	T* GetRawPtr(PtrType p)
	{
		if constexpr (std::is_pointer_v<PtrType>)
			return p;
		else
			return p.get();
	}
	
	template<typename T, typename Alloc, typename PtrType>
	struct AllocatorState
	{
		Alloc   allocator;
		PtrType ptrHandle;
		uint32_t count = 1;
	};
	template<typename T, typename Alloc, typename PtrType>
	void* AllocatorResolver(void* state)
	{
		auto* s = static_cast<AllocatorState<T, Alloc, PtrType>*>(state);
		return GetRawPtr<T>(s->ptrHandle);
	}
	
	template<typename T, typename Alloc, typename PtrType>
	void AllocatorDeleter(void* state)
	{
		auto* s = static_cast<AllocatorState<T, Alloc, PtrType>*>(state);
		
		T* ptr = static_cast<T*>(GetRawPtr<T>(s->ptrHandle));
		if (ptr)
		{
			if constexpr (!std::is_trivially_destructible_v<T>)
			{
				for (uint32_t i = 0; i < s->count; ++i)
					ptr[i].~T();
			}
		}
		
		s->allocator.deallocate(s->ptrHandle, s->count);
		
		delete s;
	}
} // namespace detail

template<typename CType>
class IWeakPtr;

template<typename CType>
class ISharedPtr : public IPtrBase
{
	friend class IWeakPtr<CType>;
public:
	virtual ~ISharedPtr()
	{
		if (_entity)
		{
			IPtrManager::GetInstance().SharedPtrDec(_entity);
		}
		
	}

	constexpr ISharedPtr() noexcept
		: IPtrBase(ITools::PtrType::SHARED)
	{
	}

	ISharedPtr(CType* ptr)
		: IPtrBase(ITools::PtrType::SHARED)
	{
		if (ptr)
		{
			_entity = IPtrManager::GetInstance().AddPtr(&detail::DefaultResolver, ptr, &detail::DefaultDeleter<CType>, ptr);
		}

	}

	ISharedPtr(IPtrManager::ResolverFunc resolver, void* resolverState, IPtrManager::DeleterFunc  deleter, void* deleterState, std::uint32_t count = 1)
		: IPtrBase(ITools::PtrType::SHARED)
	{
		if (resolver && resolverState)
		{
			_entity = IPtrManager::GetInstance().AddPtr(resolver, resolverState, deleter, deleterState);
			_count = count;
		}
	}

	ISharedPtr(const ISharedPtr& other)
		: IPtrBase(ITools::PtrType::SHARED)
	{
		if (this != &other)
		{
			if (_entity)
			{
				IPtrManager::GetInstance().SharedPtrDec(_entity);
			}
			_entity = other._entity;
			_count = other._count;
			IPtrManager::GetInstance().SharedPtrInc(_entity);
		}
		
	}

	ISharedPtr(ISharedPtr&& other) noexcept
		: IPtrBase(ITools::PtrType::SHARED)
	{
		if(_entity)
		{
			IPtrManager::GetInstance().SharedPtrDec(_entity);
		}
		_entity = other._entity;
		_count = other._count;
		other._entity = 0;
		other._count = 1;
	}

	explicit ISharedPtr(size_t entity, std::uint32_t count)
		: IPtrBase(ITools::PtrType::SHARED)
	{
		_entity = entity;
		_count = count;

		void* dummy = nullptr;
		if (!IPtrManager::GetInstance().GetPtrAndInc(entity, dummy))
		{
			_entity = 0;
			_count = 1;
		}
	}

	// 核心工厂：使用任意分配器（包括 IMemPoolAllocator）
	//
	// 分配器必须满足：
	//   - alloc.allocate(n) → PtrType     （PtrType 可为 T* 或 IUserPtr<T>）
	//   - alloc.deallocate(p, n)
	//
	// 对于 IMemPoolAllocator<T>：
	//   - allocate(1) 返回 IUserPtr<T>
	//   - GetRawPtr 调用 IUserPtr<T>::get() 每次获取最新地址
	//   - deallocate 将 IUserPtr<T> 归还池
	//
	// 注意：IMemPool 的 Allocate 对 >1024 字节的非平凡类型有 static_assert，
	//       这是 IMemPool 本身的限制，非本智能指针的问题。
	template<typename Alloc, typename... Args>
	static ISharedPtr<CType> Allocate(Alloc& alloc, Args&&... args)
	{
		auto ptrHandle = alloc.allocate(1);
		CType* raw = detail::GetRawPtr<CType>(ptrHandle);

		try
		{
			::new (raw) CType(std::forward<Args>(args)...);
		}
		catch (...)
		{
			alloc.deallocate(ptrHandle, 1);
			throw;
		}

		using State = detail::AllocatorState<CType, Alloc, decltype(ptrHandle)>;
		auto* state = new State{ alloc, ptrHandle, 1 };

		return ISharedPtr<CType>(
			&detail::AllocatorResolver<CType, Alloc, decltype(ptrHandle)>,
			state,
			&detail::AllocatorDeleter<CType, Alloc, decltype(ptrHandle)>,
			state);
	}
	template<typename Alloc>
	static ISharedPtr<CType> AllocateArray(Alloc& alloc, std::uint32_t n)
	{
		if (n == 0) return ISharedPtr<CType>();
		auto ptrHandle = alloc.allocate(n);
		CType* raw = detail::GetRawPtr<CType>(ptrHandle);
		std::uint32_t constructed = 0;
		try
		{
			for (std::uint32_t i = 0; i < n; ++i)
			{
				::new (&raw[i]) CType();
				++constructed;
			}
		}
		catch (...)
		{
			for (std::uint32_t i = 0; i < constructed; ++i)
				raw[i].~CType();
			alloc.deallocate(ptrHandle, n);
			throw;
		}
		using State = detail::AllocatorState<CType, Alloc, decltype(ptrHandle)>;
		auto* state = new State{ alloc, ptrHandle, n };
		return ISharedPtr<CType>(
			&detail::AllocatorResolver<CType, Alloc, decltype(ptrHandle)>,
			state,
			&detail::AllocatorDeleter<CType, Alloc, decltype(ptrHandle)>,
			state,
		    n);
	}
	template<typename Alloc, typename TInit>
	static ISharedPtr<CType> AllocateArray(Alloc& alloc,
		std::initializer_list<TInit> init)
	{
		std::uint32_t n = static_cast<std::uint32_t>(init.size());
		if (n == 0) return ISharedPtr<CType>();
		auto ptrHandle = alloc.allocate(n);
		CType* raw = detail::GetRawPtr<CType>(ptrHandle);
		std::uint32_t constructed = 0;
		try
		{
			auto it = init.begin();
			for (std::uint32_t i = 0; i < n; ++i, ++it)
			{
				::new (&raw[i]) CType(*it);
				++constructed;
			}
		}
		catch (...)
		{
			for (std::uint32_t i = 0; i < constructed; ++i)
				raw[i].~CType();
			alloc.deallocate(ptrHandle, n);
			throw;
		}
		using State = detail::AllocatorState<CType, Alloc, decltype(ptrHandle)>;
		auto* state = new State{ alloc, ptrHandle, n };
		return ISharedPtr<CType>(
			&detail::AllocatorResolver<CType, Alloc, decltype(ptrHandle)>,
			state,
			&detail::AllocatorDeleter<CType, Alloc, decltype(ptrHandle)>,
			state,
			n);
	}

	CType& operator[](std::size_t index) const
	{
		if (index < _count)
		{
			return Get()[index];
		}
		else
		{
			throw std::out_of_range("");
		}
	}

	CType* Get() const
	{
		return _entity
			? static_cast<CType*>(IPtrManager::GetInstance().GetPtr(_entity))
			: nullptr;
	}

	CType* operator->() const
	{
		return Get();
	}

	CType& operator*()  const { return *Get(); }

	void operator=(CType* ptr)
	{
		if (_entity) IPtrManager::GetInstance().SharedPtrDec(_entity);
		if (ptr)
		{
			_entity = IPtrManager::GetInstance().AddPtr(
				&detail::DefaultResolver, ptr,
				&detail::DefaultDeleter<CType>, ptr);
		}
		else
		{
			_entity = 0;
		}
	}

	void operator=(const ISharedPtr& other)
	{
		if (this != &other)
		{
			if (_entity)
			{
				IPtrManager::GetInstance().SharedPtrDec(_entity);
			}
			_entity = other._entity;
			_count = other._count;
			IPtrManager::GetInstance().SharedPtrInc(_entity);
		}
	}

	void operator=(ISharedPtr&& other) noexcept
	{
		if (this != &other)
		{
			if (_entity)
			{
				IPtrManager::GetInstance().SharedPtrDec(_entity);
				
			}
			_entity = other._entity;
			_count = other._count;
			other._entity = 0;
			other._count = 1;
		}

	}

	bool operator==(const ISharedPtr& other) const
	{
		return _entity == other._entity;
	}

	bool operator!=(const ISharedPtr& other) const
	{
		return _entity != other._entity;
	}

	bool operator==(const CType* ptr) const
	{
		return Get() == ptr;
	}

	bool operator!=(const CType* ptr) const
	{
		return Get() != ptr;
	}

	bool IsValid() const
	{
		if (_entity)
		{
			return IPtrManager::GetInstance().PtrIsValid(_entity);
		}
		return false;
	}
	
	// type safe cast, only works for SharedPtr, if the cast fails, it will return nullptr
	template<typename CastType>
	ISharedPtr<CastType> CastTo() const
	{
		if (IsValid())
		{
			if (auto _ = dynamic_cast<CastType*>(Get()))
			{
				return ISharedPtr<CastType>(_entity, _count);
			}
			
		}
		return ISharedPtr<CastType>();
	}

private:
	size_t _entity = 0;
};

template<typename CType>
class IUniquePtr;

template<typename CType>
class IWeakPtr : public IPtrBase
{
	friend class IUniquePtr<CType>;
	friend class ISharedPtr<CType>;
public:
	~IWeakPtr()
	{
		if (_entity)
		{
			IPtrManager::GetInstance().WeakPtrDec(_entity);
		}
	}

	constexpr IWeakPtr() noexcept
		: IPtrBase(ITools::PtrType::UNDEFINED)
	{
	}

	IWeakPtr(const IWeakPtr& other)
		: IPtrBase(other._type)
	{
		if (this != &other)
		{
			if (_entity)
			{
				IPtrManager::GetInstance().WeakPtrDec(_entity);
			}
			_entity = other._entity;
			_type = other._type;
			_count = other._count;
			IPtrManager::GetInstance().WeakPtrInc(_entity);
		}
	}
	IWeakPtr(IWeakPtr&& other) noexcept
	{
		if(_entity)
		{
			IPtrManager::GetInstance().WeakPtrDec(_entity);
		}
		_entity = other._entity;
		_type = other._type;
		_count = other._count;
		other._entity = 0;
		other._type = ITools::PtrType::UNDEFINED;
		other._count = 1;
	}
	IWeakPtr(const ISharedPtr<CType>& other)
		: IPtrBase(other._type)
	{
		if(_entity)
		{
			IPtrManager::GetInstance().WeakPtrDec(_entity);
		}
		_entity = other._entity;
		_type = other._type;
		_count = other._count;
		IPtrManager::GetInstance().WeakPtrInc(_entity);
	}
	IWeakPtr(size_t entity, ITools::PtrType type, std::uint32_t count)
	{
		void* dummy = nullptr;
		if (IPtrManager::GetInstance().GetPtrAndWeakInc(entity, dummy))
		{
			_entity = entity;
			_type = type;
			_count = count;
		}
		
	}

	void operator=(const IWeakPtr& other)
	{
		if (this != &other)
		{
			if (_entity)
			{
				IPtrManager::GetInstance().WeakPtrDec(_entity);
			}
			_entity = other._entity;
			_type = other._type;
			_count = other._count;
			IPtrManager::GetInstance().WeakPtrInc(_entity);
		}
	}
	void operator=(IWeakPtr&& other) noexcept
	{
		if(_entity)
		{
			IPtrManager::GetInstance().WeakPtrDec(_entity);
		}
		_entity = other._entity;
		_type = other._type;
		_count = other._count;
		other._entity = 0;
		other._type = ITools::PtrType::UNDEFINED;
		other._count = 1;
	}

	void operator=(const ISharedPtr<CType>& other)
	{
		if (_entity)
		{
			IPtrManager::GetInstance().WeakPtrDec(_entity);
		}
		_entity = other._entity;
		_type = other._type;
		_count = other._count;
		IPtrManager::GetInstance().WeakPtrInc(_entity);
	}

	void operator=(const IUniquePtr<CType>& other)
	{
		if (_entity)
		{
			IPtrManager::GetInstance().WeakPtrDec(_entity);
		}
		_entity = other._entity;
		_type = other._type;
		_count = other._count;
		IPtrManager::GetInstance().WeakPtrInc(_entity);
	}

	bool operator==(const IWeakPtr& other) const
	{
		return _entity == other._entity;
	}

	bool operator!=(const IWeakPtr& other) const
	{
		return _entity != other._entity;
	}

	bool operator==(const CType* ptr) const
	{
		return Get() == ptr;
	}

	bool operator!=(const CType* ptr) const
	{
		return Get() != ptr;
	}

	bool IsValid() const
	{
		if (_entity)
		{
			return IPtrManager::GetInstance().PtrIsValid(_entity);
		}
		return false;
	}

	template<typename CastType>
	IWeakPtr<CastType> CastTo() const
	{
		if (IsValid())
		{
			if (auto _ = dynamic_cast<CastType*>(Get()))
			{
				return IWeakPtr<CastType>(_entity, _type, _count);
			}
		}
		return IWeakPtr<CastType>();
	}

	// 从 UniquePtr 转换而来的 WeakPtr 无法直接转换为 SharedPtr，因为 UniquePtr 的独占性质意味着它不支持共享所有权。
	// 因此，ToSharedPtr 函数仅在指针类型为 Shared 时才返回有效的 SharedPtr，否则返回 nullptr。
	// 这是为了避免在 UniquePtr 的上下文中误用 SharedPtr，从而导致潜在的资源管理问题。
	ISharedPtr<CType> ToSharedPtr() const
	{
		if (_type == ITools::PtrType::SHARED && IsValid())
		{
			return ISharedPtr<CType>(_entity, _count);
		}
		return nullptr;
	}

	// 仅当弱指针仍然有效时才返回原始指针，否则返回nullptr
	// 旨在从 UniquePtr 获取原始指针，前提是 UniquePtr 仍然存在
	// 此函数在指针类型为 Unique 时依然存在悬空指针风险，因此使用时需谨慎
	CType* Get() const
	{
		if (IsValid())
		{
			return static_cast<CType*>(IPtrManager::GetInstance().GetPtr(_entity));
		}
		return nullptr;
	}



private:
	size_t _entity = 0;
};

template<typename CType>
class IUniquePtr : public IPtrBase
{
public:
	~IUniquePtr()
	{
		if (_entity)
		{
			IPtrManager::GetInstance().SharedPtrDec(_entity);
		}
	}
	constexpr IUniquePtr() noexcept
		: IPtrBase(ITools::PtrType::UNIQUE)
	{}

	IUniquePtr(CType* ptr)
		: IPtrBase(ITools::PtrType::UNIQUE)
	{
		if (ptr)
		{
			_entity = IPtrManager::GetInstance().AddPtr(
				&detail::DefaultResolver, ptr,
				&detail::DefaultDeleter<CType>, ptr);
		}

	}

	IUniquePtr(IPtrManager::ResolverFunc resolver, void* resolverState, IPtrManager::DeleterFunc  deleter, void* deleterState, std::uint32_t count = 1)
		: IPtrBase(ITools::PtrType::UNIQUE)
	{
		if (resolver && resolverState)
		{
			_entity = IPtrManager::GetInstance().AddPtr(resolver, resolverState, deleter, deleterState);
			_count = count;
		}
	}

	IUniquePtr(const IUniquePtr& other) = delete;
	IUniquePtr(IUniquePtr&& other) noexcept
		: IPtrBase(ITools::PtrType::UNIQUE)
	{
		_entity = other._entity;
		_count = other._count;
		other._entity = 0;
		other._count = 1;
	}

	void operator=(const IUniquePtr& other) = delete;
	void operator=(IUniquePtr&& other) noexcept
	{
		if (this != &other)
		{
			if (_entity) IPtrManager::GetInstance().SharedPtrDec(_entity);
			_entity = other._entity;
			_count = other._count;
			other._entity = 0;
			other._count = 1;
		}
		
	}

	template<typename Alloc, typename... Args>
	static IUniquePtr<CType> Allocate(Alloc& alloc, Args&&... args)
	{
		auto ptrHandle = alloc.allocate(1);
		CType* raw = detail::GetRawPtr<CType>(ptrHandle);
		try
		{
			::new (raw) CType(std::forward<Args>(args)...);
		}
		catch (...)
		{
			alloc.deallocate(ptrHandle, 1);
			throw;
		}
		using State = detail::AllocatorState<CType, Alloc, decltype(ptrHandle)>;
		auto* state = new State{ alloc, ptrHandle, 1 };
		return IUniquePtr<CType>(
			&detail::AllocatorResolver<CType, Alloc, decltype(ptrHandle)>,
			state,
			&detail::AllocatorDeleter<CType, Alloc, decltype(ptrHandle)>,
			state);
	}
	template<typename Alloc>
	static IUniquePtr<CType> AllocateArray(Alloc& alloc, std::uint32_t n)
	{
		if (n == 0) return IUniquePtr<CType>();
		auto ptrHandle = alloc.allocate(n);
		CType* raw = detail::GetRawPtr<CType>(ptrHandle);
		std::uint32_t constructed = 0;
		try
		{
			for (std::uint32_t i = 0; i < n; ++i)
			{
				::new (&raw[i]) CType();
				++constructed;
			}
		}
		catch (...)
		{
			for (std::uint32_t i = 0; i < constructed; ++i)
				raw[i].~CType();
			alloc.deallocate(ptrHandle, n);
			throw;
		}
		using State = detail::AllocatorState<CType, Alloc, decltype(ptrHandle)>;
		auto* state = new State{ alloc, ptrHandle, n };
		return IUniquePtr<CType>(
			&detail::AllocatorResolver<CType, Alloc, decltype(ptrHandle)>,
			state,
			&detail::AllocatorDeleter<CType, Alloc, decltype(ptrHandle)>,
			state,
			n);
	}
	template<typename Alloc, typename TInit>
	static IUniquePtr<CType> AllocateArray(Alloc& alloc,
		std::initializer_list<TInit> init)
	{
		std::uint32_t n = static_cast<std::uint32_t>(init.size());
		if (n == 0) return IUniquePtr<CType>();
		auto ptrHandle = alloc.allocate(n);
		CType* raw = detail::GetRawPtr<CType>(ptrHandle);
		std::uint32_t constructed = 0;
		try
		{
			auto it = init.begin();
			for (std::uint32_t i = 0; i < n; ++i, ++it)
			{
				::new (&raw[i]) CType(*it);
				++constructed;
			}
		}
		catch (...)
		{
			for (std::uint32_t i = 0; i < constructed; ++i)
				raw[i].~CType();
			alloc.deallocate(ptrHandle, n);
			throw;
		}
		using State = detail::AllocatorState<CType, Alloc, decltype(ptrHandle)>;
		auto* state = new State{ alloc, ptrHandle, n };
		return IUniquePtr<CType>(
			&detail::AllocatorResolver<CType, Alloc, decltype(ptrHandle)>,
			state,
			&detail::AllocatorDeleter<CType, Alloc, decltype(ptrHandle)>,
			state,
			n);
	}

	CType& operator[](std::size_t index) const
	{
		if (index < _count)
		{
			return Get()[index];
		}
		else
		{
			throw std::out_of_range();
		}
	}

	CType* Get() const 
	{
		return _entity
			? static_cast<CType*>(IPtrManager::GetInstance().GetPtr(_entity))
			: nullptr;
	}

	CType* operator->() const { return Get(); }
	CType& operator*()  const { return *Get(); }

	template<typename WeakType>
	bool operator==(const IWeakPtr<WeakType>& other) const
	{
		return _entity == other._entity;
	}

	template<typename WeakType>
	bool operator!=(const IWeakPtr<WeakType>& other) const
	{
		return _entity != other._entity;
	}

	bool operator==(const CType* ptr) const
	{
		return Get() == ptr;
	}

	bool operator!=(const CType* ptr) const
	{
		return Get() != ptr;
	}

	bool IsValid() const
	{
		if (_entity)
		{
			return IPtrManager::GetInstance().PtrIsValid(_entity);
		}
		return false;
	}

	// 从 UniquePtr 获取 WeakPtr，与 STL 中的 std::weak_ptr 类似，返回一个弱指针，指向 UniquePtr 管理的对象
	// 由于 UniquePtr 的独占性质，弱指针的存在并不影响 UniquePtr 的生命周期，但使用弱指针时需要注意对象是否仍然存在
	IWeakPtr<CType> ToWeakPtr() const
	{
		IWeakPtr<CType> weakPtr;
		weakPtr._entity = _entity;
		weakPtr._type = _type;
		weakPtr._count = _count;
		IPtrManager::GetInstance().WeakPtrInc(_entity);
		return weakPtr;
	}



private:
	size_t _entity = 0;
};


namespace ITools
{
	template<typename CType, typename... Args>
	ISharedPtr<CType> MakeIShared(Args&&... args)
	{
		return ISharedPtr<CType>(new CType(std::forward<Args>(args)...));
	}

	template<typename CType, typename Alloc, typename... Args>
	ISharedPtr<CType> AllocateIShared(Alloc& alloc, Args&&... args)
	{
		return ISharedPtr<CType>::template Allocate<Alloc>(
			alloc, std::forward<Args>(args)...
		);
	}

	template<typename CType, typename CastType>
	ISharedPtr<CastType> CastIShared(const ISharedPtr<CType>& ptr)
	{
		return ptr.CastTo<CastType>();
	}

	template<typename CType, typename... Args>
	IUniquePtr<CType> MakeIUnique(Args&&... args)
	{
		return IUniquePtr<CType>(new CType(std::forward<Args>(args)...));
	}

	template<typename CType, typename Alloc, typename... Args>
	IUniquePtr<CType> AllocateIUnique(Alloc& alloc, Args&&... args)
	{
		return IUniquePtr<CType>::template Allocate<Alloc>(
			alloc, std::forward<Args>(args)...
		);
	}

	template<typename CType, typename CastType>
	IWeakPtr<CastType> CastIWeak(const ISharedPtr<CType>& ptr)
	{
		return ptr.CastTo<CastType>();
	}

	template<typename CType, typename CastType>
	IWeakPtr<CastType> CastIWeak(const IWeakPtr<CType>& ptr)
	{
		return ptr.CastTo<CastType>();
	}

}
