// SharedPtrTest.cpp : 此文件包含 "main" 函数。程序执行将在此处开始并结束。
//

#include "ISharedPtr/ISharedPtr.h"

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

	return 0;
}


