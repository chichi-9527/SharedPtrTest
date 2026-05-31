// SharedPtrTest.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include "ISharedPtr/ISharedPtr.h"
#include "IMemPool.h"
#include "Test/TestSmartPtrArrays.h"

#include <iostream>
#include <vector>

class MyClass
{
public:
	MyClass(){}
	~MyClass(){}

	int add(int a, int b)
	{
		return a + b;
	}

private:

};

// 辅助测试类 2：测试大内存分配（触发通用池）
struct BigObject {
	double data[1000]; // 8000 字节 > 1024
};


int main()
{
	auto ptr1 = ISharedPtr<MyClass>(new MyClass());
	auto ptr2 = ITools::MakeIShared<MyClass>();

	IUniquePtr<MyClass> uniquePtr(new MyClass());

	std::vector<IUniquePtr<MyClass>> uniquePtrVec;
	uniquePtrVec.emplace_back(new MyClass());
	auto weakPtrFromUnique = uniquePtr.ToWeakPtr();
	auto weakPtrFromUnique2 = uniquePtrVec[0].ToWeakPtr();

	std::cout << IPtrManager::GetInstance().GetSafeSizeTTypeName() << std::endl;

	std::cout << ptr1->add(1, 2) << std::endl;
	std::cout << ptr2->add(3, 4) << std::endl;
	std::cout << weakPtrFromUnique.Get()->add(5, 6) << std::endl;
	std::cout << weakPtrFromUnique2.Get()->add(7, 8) << std::endl;

	IMemPool* pool = IMemPool::CreatePool();
	// IMemPoolAllocator 就是一个普通分配器，与其他分配器用法完全一致
	IMemPoolAllocator<BigObject> alloc(pool);

	auto sp1 = ITools::AllocateIShared<BigObject>(alloc);
	sp1->data[0] = 1.1;

	auto p1 = pool->New<BigObject>(1);
	p1->data[0] = 3.3;

	auto up1 = ITools::AllocateIUnique<BigObject>(alloc);
	up1->data[0] = 2.2;

	pool->Deallocate(p1, 1);

	pool->DefragmentGeneralPool();

	assert(sp1->data[0] == 1.1);
	assert(up1->data[0] == 2.2);
	std::cout << sp1->data[0] << std::endl;
	std::cout << up1->data[0] << std::endl;

	return TestSmartPtrArrays();

	return 0;
}


