#pragma once

#include <cstdint>
#include <utility>
#include <string_view>

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
protected:
	ITools::PtrType _type = ITools::PtrType::UNDEFINED;
};

class IPtrManager
{
public:

	static IPtrManager& GetInstance();

	bool PtrIsValid(size_t ptr_id);

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



template<typename CType>
class ISharedPtr : public IPtrBase
{
public:
	virtual ~ISharedPtr()
	{
		if (_ptr && _entity)
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
			_ptr = ptr;
			_entity = IPtrManager::GetInstance().AddPtr(ptr);
		}
		else
		{
			_ptr = nullptr;
			_entity = 0;
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
			_ptr = other._ptr;
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
		_ptr = other._ptr;
		other._entity = 0;
		other._ptr = nullptr;
	}

	ISharedPtr(size_t entity)
		: IPtrBase(ITools::PtrType::SHARED)
	{
		_entity = entity;
		if (!IPtrManager::GetInstance().GetPtrAndInc(entity, _ptr))
		{
			_entity = 0;
			_ptr = nullptr;
		}
	}

	CType* Get() const
	{
		return _ptr;
	}

	CType* operator->() const
	{
		return _ptr;
	}

	void operator=(CType* ptr)
	{
		if (_ptr != ptr)
		{
			if(_ptr && _entity)
			{
				IPtrManager::GetInstance().SharedPtrDec(_entity);
			}
			if (ptr)
			{
				_ptr = ptr;
				_entity = IPtrManager::GetInstance().AddPtr(ptr);
			}
			else
			{
				_ptr = nullptr;
				_entity = 0;
			}
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
			_ptr = other._ptr;
			IPtrManager::GetInstance().SharedPtrInc(_entity);
		}
	}

	void operator=(ISharedPtr&& other) noexcept
	{
		if (_entity)
		{
			IPtrManager::GetInstance().SharedPtrDec(_entity);
		}
		_entity = other._entity;
		_ptr = other._ptr;
		other._entity = 0;
		other._ptr = nullptr;
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
		return _ptr == ptr;
	}

	bool operator!=(const CType* ptr) const
	{
		return _ptr != ptr;
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
			if (auto castedPtr = dynamic_cast<CastType*>(_ptr))
			{
				ISharedPtr<CastType> castedPtr(_entity);
				return castedPtr;
			}
			
		}
		return ISharedPtr<CastType>();
	}


private:
	CType* _ptr = nullptr;
	size_t _entity = 0;
};

template<typename CType>
class IUniquePtr;

template<typename CType>
class IWeakPtr : public IPtrBase
{
	friend class IUniquePtr<CType>;
public:
	~IWeakPtr()
	{
		if (_ptr && _entity)
		{
			IPtrManager::GetInstance().WeakPtrDec(_entity);
		}
	}

	constexpr IWeakPtr() noexcept
		: IPtrBase(ITools::PtrType::UNDEFINED)
	{
	}

	IWeakPtr(const IWeakPtr& other)
	{
		if (this != &other)
		{
			if (_entity)
			{
				IPtrManager::GetInstance().WeakPtrDec(_entity);
			}
			_entity = other._entity;
			_ptr = other._ptr;
			_type = other._type;
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
		_ptr = other._ptr;
		_type = other._type;
		other._entity = 0;
		other._ptr = nullptr;
		other._type = ITools::PtrType::UNDEFINED;
	}
	IWeakPtr(const ISharedPtr<CType>& other)
	{
		if(_entity)
		{
			IPtrManager::GetInstance().WeakPtrDec(_entity);
		}
		_entity = other._entity;
		_ptr = other._ptr;
		_type = other._type;
		IPtrManager::GetInstance().WeakPtrInc(_entity);
	}
	IWeakPtr(size_t entity, ITools::PtrType type)
	{
		if (IPtrManager::GetInstance().GetPtrAndWeakInc(entity, _ptr))
		{
			_entity = entity;
			_type = type;
		}
		else
		{
			_entity = 0;
			_ptr = nullptr;
			_type = ITools::PtrType::UNDEFINED;
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
			_ptr = other._ptr;
			_type = other._type;
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
		_ptr = other._ptr;
		_type = other._type;
		other._entity = 0;
		other._ptr = nullptr;
		other._type = ITools::PtrType::UNDEFINED;
	}

	void operator=(const ISharedPtr<CType>& other)
	{
		if(_entity)
		{
			IPtrManager::GetInstance().WeakPtrDec(_entity);
		}
		_entity = other._entity;
		_ptr = other._ptr;
		_type = other._type;
		IPtrManager::GetInstance().WeakPtrInc(_entity);
	}

	void operator=(const IUniquePtr<CType>& other)
	{
		if(_entity)
		{
			IPtrManager::GetInstance().WeakPtrDec(_entity);
		}
		_entity = other._entity;
		_ptr = other._ptr;
		_type = other._type;
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
		return _ptr == ptr;
	}

	bool operator!=(const CType* ptr) const
	{
		return _ptr != ptr;
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
			if (auto castedPtr = dynamic_cast<CastType*>(_ptr))
			{
				IWeakPtr<CastType> castedPtr(_entity, _type);
				return castedPtr;
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
			ISharedPtr<CType> sharedPtr(_entity);
			return sharedPtr;
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
			return _ptr;
		}
		return nullptr;
	}



private:
	CType* _ptr = nullptr;
	size_t _entity = 0;
};

template<typename CType>
class IUniquePtr : public IPtrBase
{
public:
	~IUniquePtr()
	{
		if (_ptr && _entity)
		{
			IPtrManager::GetInstance().SharedPtrDec(_entity);
		}
	}
	constexpr IUniquePtr() noexcept
		: IPtrBase(ITools::PtrType::UNIQUE)
	{
	}

	IUniquePtr(CType* ptr)
		: IPtrBase(ITools::PtrType::UNIQUE)
	{
		if (ptr)
		{
			_ptr = ptr;
			_entity = IPtrManager::GetInstance().AddPtr(ptr);
		}
		else
		{
			_ptr = nullptr;
			_entity = 0;
		}
	}

	IUniquePtr(const IUniquePtr& other) = delete;
	IUniquePtr(IUniquePtr&& other) noexcept
		: IPtrBase(ITools::PtrType::UNIQUE)
	{
		_entity = other._entity;
		_ptr = other._ptr;
		other._entity = 0;
		other._ptr = nullptr;
	}

	void operator=(const IUniquePtr& other) = delete;
	void operator=(IUniquePtr&& other) noexcept
	{
		_entity = other._entity;
		_ptr = other._ptr;
		_type = ITools::PtrType::UNIQUE;
		other._entity = 0;
		other._ptr = nullptr;
		other._type = ITools::PtrType::UNDEFINED;
	}

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
		return _ptr == ptr;
	}

	bool operator!=(const CType* ptr) const
	{
		return _ptr != ptr;
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
		weakPtr._ptr = _ptr;
		weakPtr._type = _type;
		IPtrManager::GetInstance().WeakPtrInc(_entity);
		return weakPtr;
	}



private:
	CType* _ptr = nullptr;
	size_t _entity = 0;
};


namespace ITools
{
	template<typename CType, typename... Args>
	ISharedPtr<CType> MakeIShared(Args&&... args)
	{
		return ISharedPtr<CType>(new CType(std::forward<Args>(args)...));
	}

	template<typename CType, typename CastType>
	ISharedPtr<CastType> CastIShared(const ISharedPtr<CType>& ptr)
	{
		return ptr.CastTo<CastType>();
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
