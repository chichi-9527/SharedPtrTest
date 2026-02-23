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
	void* Ptr = nullptr;
	SafeSizeT SharedCount = 0;
	SafeSizeT WeakCount = 0;

	IPtr() = default;
	IPtr(void* ptr)
	{
		Ptr = ptr;
		SharedCount = 1;
	}
	IPtr(const IPtr& other) = delete;
	IPtr& operator=(const IPtr& other) = delete;
	IPtr(IPtr&& other) noexcept
		: Ptr(other.Ptr), SharedCount(other.SharedCount.load()), WeakCount(other.WeakCount.load())
	{
		other.Ptr = nullptr;
		other.SharedCount = 0;
		other.WeakCount = 0;
	}
	

	void operator=(void* ptr)
	{
		Ptr = ptr;
		SharedCount = 1;
		WeakCount = 0;
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

	bool operator==(const void* ptr) const
	{
		return Ptr == ptr;
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
		return (Ptr != nullptr) && (SharedCount != 0);
	}

	bool CanBeFreeID() const
	{
		return (Ptr == nullptr) && (SharedCount == 0) && (WeakCount == 0);
	}

};

static std::vector<IPtr> IPtrs;
static std::queue<size_t> FreePtrIds;
static std::mutex IPtrMutex;

IPtrManager::IPtrManager()
{
	IPtrs.reserve(1 << 7);
	IPtrs.emplace_back(nullptr);
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

size_t IPtrManager::AddPtr(void* ptr)
{
	if (FreePtrIds.empty())
	{
		std::lock_guard<std::mutex> lock(IPtrMutex);
		IPtrs.emplace_back(ptr);
		return IPtrs.size() - 1;
	}
	else
	{
		std::lock_guard<std::mutex> lock(IPtrMutex);
		size_t id = FreePtrIds.front();
		FreePtrIds.pop();
		IPtrs[id] = ptr;
		return id;
	}
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
	void* ptrToDelete = nullptr;
	{
		std::lock_guard<std::mutex> lock(IPtrMutex);
		if (ptr_id < IPtrs.size())
		{
			IPtrs[ptr_id]--;
			if (!IPtrs[ptr_id].IsValid())
			{
				ptrToDelete = IPtrs[ptr_id].Ptr;
				IPtrs[ptr_id].Ptr = nullptr;
				if (IPtrs[ptr_id].CanBeFreeID())
				{
					FreePtrIds.push(ptr_id);
				}
			}
		}
	}
	if (ptrToDelete)
		delete ptrToDelete;
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
		if (IPtrs[ptr_id].IsValid())
		{
			return IPtrs[ptr_id].Ptr;
		}
	}
	return nullptr;
}

bool IPtrManager::GetPtrAndInc(size_t ptr_id, void*& ptr)
{
	std::lock_guard<std::mutex> lock(IPtrMutex);
	if (ptr_id < IPtrs.size() && IPtrs[ptr_id].IsValid())
	{
		IPtrs[ptr_id]++;
		ptr = IPtrs[ptr_id].Ptr;
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
		ptr = IPtrs[ptr_id].Ptr;
		return true;
	}
	return false;
}

std::string_view IPtrManager::GetSafeSizeTTypeName() const
{
	return SafeSizeTTypeName;
}
