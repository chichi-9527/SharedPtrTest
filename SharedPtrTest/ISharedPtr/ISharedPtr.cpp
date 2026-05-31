#include "ISharedPtr.h"

#include <vector>
#include <queue>
#include <mutex>
#include <atomic>

#ifndef UNUSE_SAFE_SIZE_T
#define SafeSizeT std::atomic<size_t>
constexpr const char* SafeSizeTTypeName = "std::atomic<size_t>";
#else
#define SafeSizeT size_t
constexpr const char* SafeSizeTTypeName = "size_t";
#endif

struct IPtr
{
	using ResolverFunc = void* (*)(void* state);
	using DeleterFunc = void(*)(void* state);

	SafeSizeT SharedCount = 0;
	SafeSizeT WeakCount = 0;
	ResolverFunc Resolver = nullptr;
	void* ResolverState = nullptr;
	DeleterFunc Deleter = nullptr;
	void* DeleterState = nullptr;

	IPtr() = default;
	IPtr(ResolverFunc resolver, void* resolverState,
		DeleterFunc  deleter, void* deleterState)
		: SharedCount(1)
		, Resolver(resolver), ResolverState(resolverState)
		, Deleter(deleter), DeleterState(deleterState)
	{}
	IPtr(const IPtr& other) = delete;
	IPtr& operator=(const IPtr& other) = delete;
#ifndef UNUSE_SAFE_SIZE_T
	IPtr(IPtr&& other) noexcept
		: SharedCount(other.SharedCount.load())
		, WeakCount(other.WeakCount.load())
		, Resolver(other.Resolver), ResolverState(other.ResolverState)
		, Deleter(other.Deleter), DeleterState(other.DeleterState)
	{
		other.Resolver = nullptr;
		other.ResolverState = nullptr;
		other.Deleter = nullptr;
		other.DeleterState = nullptr;
	}
#else
	IPtr(IPtr&& other) noexcept
		: SharedCount(other.SharedCount)
		, WeakCount(other.WeakCount)
		, Resolver(other.Resolver), ResolverState(other.ResolverState)
		, Deleter(other.Deleter), DeleterState(other.DeleterState)
	{
		other.Resolver = nullptr;
		other.ResolverState = nullptr;
		other.Deleter = nullptr;
		other.DeleterState = nullptr;
	}
#endif

	void* GetPtr() const
	{
		return Resolver ? Resolver(ResolverState) : nullptr;
	}

	void operator++()
	{
		SharedCount++;
	}
	void operator++(int)
	{
		SharedCount++;
	}

	void operator--()
	{
		if (SharedCount > 0)
		{
			SharedCount--;
		}
	}
	void operator--(int)
	{
		if (SharedCount > 0)
		{
			SharedCount--;
			
		}
	}

	void WeakInc()
	{
		WeakCount++;
	}

	void WeakDec()
	{
		if (WeakCount > 0)
		{
			WeakCount--;
		}
	}

	bool IsValid() const
	{
		return Resolver != nullptr && GetPtr() != nullptr && SharedCount > 0;
	}

	bool CanBeFreeID() const
	{
		return Resolver == nullptr && Deleter == nullptr
			&& ResolverState == nullptr && DeleterState == nullptr
			&& SharedCount == 0 && WeakCount == 0;
	}

};

static std::vector<IPtr> IPtrs;
static std::queue<size_t> FreePtrIds;
static std::mutex IPtrMutex;

IPtrManager::IPtrManager()
{
	IPtrs.reserve(1 << 7);
	IPtrs.emplace_back();
}

IPtrManager& IPtrManager::GetInstance()
{
	static IPtrManager instance;
	return instance;
}

bool IPtrManager::PtrIsValid(size_t ptr_id)
{
	if (ptr_id < IPtrs.size())
	{
		std::lock_guard<std::mutex> lock(IPtrMutex);
		return IPtrs[ptr_id].IsValid();
	}
	return false;
}

size_t IPtrManager::AddPtr(ResolverFunc resolver, void* resolverState, DeleterFunc deleter, void* deleterState)
{
	if (FreePtrIds.empty())
	{
		std::lock_guard<std::mutex> lock(IPtrMutex);
		IPtrs.emplace_back(resolver, resolverState, deleter, deleterState);
		return IPtrs.size() - 1;
	}
	else
	{
		std::lock_guard<std::mutex> lock(IPtrMutex);
		size_t id = FreePtrIds.front();
		FreePtrIds.pop();
		IPtr& ctrl = IPtrs[id];
		ctrl.SharedCount = 1;
		ctrl.WeakCount = 0;
		ctrl.Resolver = resolver;
		ctrl.ResolverState = resolverState;
		ctrl.Deleter = deleter;
		ctrl.DeleterState = deleterState;
		return id;
	}
}

size_t IPtrManager::AddPtr(void* ptr)
{
	return AddPtr(&detail::DefaultResolver, ptr, nullptr, nullptr);
}

void IPtrManager::SharedPtrInc(size_t ptr_id)
{
	if (ptr_id < IPtrs.size())
	{
		std::lock_guard<std::mutex> lock(IPtrMutex);
		if (IPtrs[ptr_id].IsValid())
		{
			IPtrs[ptr_id]++;
		}
		
	}
}

void IPtrManager::SharedPtrDec(size_t ptr_id)
{
	IPtr::DeleterFunc deleter = nullptr;
	void* deleterState = nullptr;

	{
		std::lock_guard<std::mutex> lock(IPtrMutex);
		if (ptr_id < IPtrs.size())
		{
			IPtrs[ptr_id]--;
			if (!IPtrs[ptr_id].IsValid())
			{
				
				deleter = IPtrs[ptr_id].Deleter;
				deleterState = IPtrs[ptr_id].DeleterState;
				IPtrs[ptr_id].Resolver = nullptr;
				IPtrs[ptr_id].ResolverState = nullptr;
				IPtrs[ptr_id].Deleter = nullptr;
				IPtrs[ptr_id].DeleterState = nullptr;
				if (IPtrs[ptr_id].CanBeFreeID())
				{
					FreePtrIds.push(ptr_id);
				}
			}
		}
	}
	if (deleter && deleterState)
		deleter(deleterState);
}

void IPtrManager::WeakPtrInc(size_t ptr_id)
{
	if (ptr_id < IPtrs.size())
	{
		std::lock_guard<std::mutex> lock(IPtrMutex);
		if (IPtrs[ptr_id].IsValid())
		{
			IPtrs[ptr_id].WeakInc();
		}
	}
}

void IPtrManager::WeakPtrDec(size_t ptr_id)
{

	std::lock_guard<std::mutex> lock(IPtrMutex);
	if (ptr_id < IPtrs.size())
	{
		IPtrs[ptr_id].WeakDec();
		if (IPtrs[ptr_id].CanBeFreeID())
		{
			FreePtrIds.push(ptr_id);
		}
	}

}

void* IPtrManager::GetPtr(size_t ptr_id)
{
	if (ptr_id < IPtrs.size())
	{
		std::lock_guard<std::mutex> lock(IPtrMutex);
		return IPtrs[ptr_id].GetPtr();
	}
	return nullptr;
}

bool IPtrManager::GetPtrAndInc(size_t ptr_id, void*& ptr)
{
	std::lock_guard<std::mutex> lock(IPtrMutex);
	if (ptr_id < IPtrs.size() && IPtrs[ptr_id].IsValid())
	{
		IPtrs[ptr_id]++;
		ptr = IPtrs[ptr_id].GetPtr();
		return true;
	}
	return false;
}

bool IPtrManager::GetPtrAndWeakInc(size_t ptr_id, void*& ptr)
{
	std::lock_guard<std::mutex> lock(IPtrMutex);
	if (ptr_id < IPtrs.size() && IPtrs[ptr_id].IsValid())
	{
		IPtrs[ptr_id].WeakInc();
		ptr = IPtrs[ptr_id].GetPtr();
		return true;
	}
	return false;
}

std::string_view IPtrManager::GetSafeSizeTTypeName() const
{
	return SafeSizeTTypeName;
}
