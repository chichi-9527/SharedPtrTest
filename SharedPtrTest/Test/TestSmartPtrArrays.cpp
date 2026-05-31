#include "TestSmartPtrArrays.h"

#include "../ISharedPtr/ISharedPtr.h"
#include "../IMemPool.h"
#include "TestHelpers.h"
#include <vector>
#include <thread>
#include <cstring>

// ============================================================================
// 测试 1：单对象基本分配与释放 (ISharedPtr)
// ============================================================================
bool Test_SharedPtr_SingleObject_Basic()
{
    IMemPool* pool = IMemPool::CreatePool();
    IMemPoolAllocator<TestObject> alloc(pool);

    {
        auto sp = ISharedPtr<TestObject>::Allocate(alloc, 42);
        TEST_ASSERT(sp.IsValid());
        TEST_ASSERT(sp->value == 42);
        TEST_ASSERT(sp->valid == true);
        TEST_ASSERT_EQ(LifeTracker::aliveCount.load(), 1);
    }
    // sp 析构，对象应被销毁并归还内存
    TEST_ASSERT_EQ(LifeTracker::aliveCount.load(), 0);
    TEST_ASSERT_EQ(LifeTracker::constructCount.load(), 1);
    TEST_ASSERT_EQ(LifeTracker::destructCount.load(), 1);

    IMemPool::DestroyPool(pool);
    return true;
}

// ============================================================================
// 测试 2：单对象唯一指针 (IUniquePtr)
// ============================================================================
bool Test_UniquePtr_SingleObject_Basic()
{
    IMemPool* pool = IMemPool::CreatePool();
    IMemPoolAllocator<TestObject> alloc(pool);

    {
        auto up = IUniquePtr<TestObject>::Allocate(alloc, 99);
        TEST_ASSERT(up.IsValid());
        TEST_ASSERT(up->value == 99);

        // 移动构造
        auto up2 = std::move(up);
        TEST_ASSERT(!up.IsValid());
        TEST_ASSERT(up2.IsValid());
        TEST_ASSERT(up2->value == 99);
    }
    TEST_ASSERT_EQ(LifeTracker::aliveCount.load(), 0);

    IMemPool::DestroyPool(pool);
    return true;
}

// ============================================================================
// 测试 3：数组默认构造
// ============================================================================
bool Test_SharedPtr_Array_DefaultConstruct()
{
    IMemPool* pool = IMemPool::CreatePool();
    IMemPoolAllocator<TestObject> alloc(pool);

    constexpr uint32_t N = 5;
    {
        auto sp = ISharedPtr<TestObject>::AllocateArray(alloc, N);
        TEST_ASSERT(sp.IsValid());

        // 检查每个元素都被默认构造
        for (uint32_t i = 0; i < N; ++i)
        {
            TEST_ASSERT(sp[i].valid == true);
            TEST_ASSERT(sp[i].value == 0);  // 默认值
        }
        TEST_ASSERT_EQ(LifeTracker::aliveCount.load(), (int)N);
    }
    TEST_ASSERT_EQ(LifeTracker::aliveCount.load(), 0);
    TEST_ASSERT_EQ(LifeTracker::constructCount.load(), (int)N);
    TEST_ASSERT_EQ(LifeTracker::destructCount.load(), (int)N);

    IMemPool::DestroyPool(pool);
    return true;
}

// ============================================================================
// 测试 4：数组 initializer_list 构造
// ============================================================================
bool Test_SharedPtr_Array_InitList()
{
    IMemPool* pool = IMemPool::CreatePool();
    IMemPoolAllocator<TestObject> alloc(pool);

    {
        auto sp = ISharedPtr<TestObject>::AllocateArray(alloc,
            { TestObject(1), TestObject(2), TestObject(3), TestObject(4) });
        TEST_ASSERT(sp.IsValid());
        TEST_ASSERT(sp[0].value == 1);
        TEST_ASSERT(sp[1].value == 2);
        TEST_ASSERT(sp[2].value == 3);
        TEST_ASSERT(sp[3].value == 4);
    }
    // initializer_list 中的临时对象也应被正确析构
    // 注意：initializer_list 的临时对象在表达式结束后析构
    TEST_ASSERT_EQ(LifeTracker::aliveCount.load(), 0);

    IMemPool::DestroyPool(pool);
    return true;
}

// ============================================================================
// 测试 5：共享所有权 — 拷贝
// ============================================================================
bool Test_SharedPtr_CopySemantics()
{
    IMemPool* pool = IMemPool::CreatePool();
    IMemPoolAllocator<TestObject> alloc(pool);

    {
        auto sp1 = ISharedPtr<TestObject>::AllocateArray(alloc, 3);
        sp1[0].value = 10;
        sp1[1].value = 20;
        sp1[2].value = 30;

        auto sp2 = sp1;                     // 拷贝构造
        ISharedPtr<TestObject> sp3;
        sp3 = sp2;                          // 拷贝赋值

        TEST_ASSERT(sp1.IsValid());
        TEST_ASSERT(sp2.IsValid());
        TEST_ASSERT(sp3.IsValid());

        // 所有指针指向同一数组
        TEST_ASSERT(sp1.Get() == sp2.Get());
        TEST_ASSERT(sp2.Get() == sp3.Get());

        // 通过任一指针修改都可见
        sp2[0].value = 100;
        TEST_ASSERT(sp1[0].value == 100);
        TEST_ASSERT(sp3[0].value == 100);

        TEST_ASSERT_EQ(LifeTracker::aliveCount.load(), 3);
    }
    // 所有拷贝析构后，最后一个释放对象
    TEST_ASSERT_EQ(LifeTracker::aliveCount.load(), 0);

    IMemPool::DestroyPool(pool);
    return true;
}

// ============================================================================
// 测试 6：移动语义
// ============================================================================
bool Test_SharedPtr_MoveSemantics()
{
    IMemPool* pool = IMemPool::CreatePool();
    IMemPoolAllocator<TestObject> alloc(pool);

    {
        auto sp1 = ISharedPtr<TestObject>::Allocate(alloc, 42);

        auto sp2 = std::move(sp1);          // 移动构造
        TEST_ASSERT(!sp1.IsValid());        // sp1 被置空
        TEST_ASSERT(sp2.IsValid());
        TEST_ASSERT(sp2->value == 42);

        ISharedPtr<TestObject> sp3;
        sp3 = std::move(sp2);               // 移动赋值
        TEST_ASSERT(!sp2.IsValid());
        TEST_ASSERT(sp3.IsValid());
        TEST_ASSERT(sp3->value == 42);

        TEST_ASSERT_EQ(LifeTracker::aliveCount.load(), 1);
    }
    TEST_ASSERT_EQ(LifeTracker::aliveCount.load(), 0);

    IMemPool::DestroyPool(pool);
    return true;
}

// ============================================================================
// 测试 7：空指针与边界
// ============================================================================
bool Test_NullAndEdgeCases()
{
    IMemPool* pool = IMemPool::CreatePool();
    IMemPoolAllocator<TestObject> alloc(pool);
    {
        // 默认构造
        ISharedPtr<TestObject> sp0;
        TEST_ASSERT(!sp0.IsValid());
        TEST_ASSERT(sp0.Get() == nullptr);

        // 分配 0 元素
        auto spZero = ISharedPtr<TestObject>::AllocateArray(alloc, 0);
        TEST_ASSERT(!spZero.IsValid());

        // 赋值 nullptr 原始指针（new/delete 路径）
        ISharedPtr<TestObject> spNull;
        spNull = static_cast<TestObject*>(nullptr);
        TEST_ASSERT(!spNull.IsValid());

        // 自赋值
        auto sp1 = ISharedPtr<TestObject>::Allocate(alloc, 1);
        sp1 = sp1;                              // 不应崩溃
        TEST_ASSERT(sp1.IsValid());

        // 移动自赋值
        // sp1 = std::move(sp1);               // 未定义行为，不测试
    }

    IMemPool::DestroyPool(pool);
    return true;
}

// ============================================================================
// 测试 8：弱指针
// ============================================================================
bool Test_WeakPtr()
{
    IMemPool* pool = IMemPool::CreatePool();
    IMemPoolAllocator<TestObject> alloc(pool);

    IWeakPtr<TestObject> weak;
    {
        auto sp = ISharedPtr<TestObject>::Allocate(alloc, 55);
        weak = sp;                          // 从 SharedPtr 构造 WeakPtr

        TEST_ASSERT(weak.IsValid());
        TEST_ASSERT(weak.Get()->value == 55);

        auto sp2 = weak.ToSharedPtr();      // 升级为 SharedPtr
        TEST_ASSERT(sp2.IsValid());
        TEST_ASSERT(sp2->value == 55);
        TEST_ASSERT_EQ(LifeTracker::aliveCount.load(), 1);
    }
    // sp 和 sp2 都析构，对象被销毁
    TEST_ASSERT(!weak.IsValid());           // 弱指针失效
    TEST_ASSERT(weak.Get() == nullptr);

    auto spDead = weak.ToSharedPtr();       // 从失效弱指针升级
    TEST_ASSERT(!spDead.IsValid());

    TEST_ASSERT_EQ(LifeTracker::aliveCount.load(), 0);

    IMemPool::DestroyPool(pool);
    return true;
}

// ============================================================================
// 测试 9：碎片整理后指针有效性
// ============================================================================
bool Test_FragmentationResilience()
{
    IMemPool* pool = IMemPool::CreatePool(
        IMemPool::MemClassCounts{
            1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024,
            256   // 通用池较小，容易触发整理
        });
    {
        IMemPoolAllocator<TestObject> alloc(pool);

        constexpr uint32_t N = 100;

        // 分配大量大块对象（>1024字节，走通用池）
        // TestObject 较小，我们用一个大类型或多次分配来制造碎片
        struct LargeObject {
            char data[2048];
            int index;
        };

        // 使用 LargeObject 测试通用池整理
        IMemPoolAllocator<LargeObject> largeAlloc(pool);

        std::vector<ISharedPtr<LargeObject>> ptrs;
        ptrs.reserve(N);

        for (int i = 0; i < (int)N; ++i)
        {
            auto sp = ISharedPtr<LargeObject>::Allocate(largeAlloc);
            sp->index = i;
            ptrs.push_back(std::move(sp));
        }

        // 释放一半，制造碎片
        for (int i = 0; i < (int)N; i += 2)
        {
            ptrs[i].operator=(nullptr);        // 释放
        }

        // 触发碎片整理
        pool->DefragmentGeneralPool();

        // 验证剩余的指针仍然有效，且数据正确
        for (int i = 1; i < (int)N; i += 2)
        {
            TEST_ASSERT(ptrs[i].IsValid());
            TEST_ASSERT(ptrs[i]->index == i);
        }
    }

    IMemPool::DestroyPool(pool);
    return true;
}

// ============================================================================
// 测试 10：池扩容后指针有效性
// ============================================================================
bool Test_PoolExpansionResilience()
{
    IMemPool* pool = IMemPool::CreatePool(
        IMemPool::MemClassCounts{
            1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024,
            4  // 极小的通用池，强制扩容
        });
    {
        IMemPoolAllocator<TestObject> alloc(pool);

        struct LargeObject { char data[4096]; int id; };
        IMemPoolAllocator<LargeObject> largeAlloc(pool);

        auto sp1 = ISharedPtr<LargeObject>::Allocate(largeAlloc);
        sp1->id = 100;
        TEST_ASSERT(sp1.IsValid());

        // 再分配一个，触发扩容
        auto sp2 = ISharedPtr<LargeObject>::Allocate(largeAlloc);
        sp2->id = 200;
        TEST_ASSERT(sp2.IsValid());

        // 两个指针在扩容后都应有效
        TEST_ASSERT(sp1.IsValid());
        TEST_ASSERT(sp1->id == 100);
        TEST_ASSERT(sp2.IsValid());
        TEST_ASSERT(sp2->id == 200);
    }
    

    IMemPool::DestroyPool(pool);
    return true;
}

// ============================================================================
// 测试 11：使用 new/delete 路径（默认分配器）
// ============================================================================
bool Test_DefaultAllocator()
{
    {
        auto sp = ITools::MakeIShared<TestObject>(42);
        TEST_ASSERT(sp.IsValid());
        TEST_ASSERT(sp->value == 42);

        auto sp2 = sp;
        TEST_ASSERT(sp.Get() == sp2.Get());
    }
    TEST_ASSERT_EQ(LifeTracker::aliveCount.load(), 0);
    TEST_ASSERT_EQ(LifeTracker::constructCount.load(), 1);
    TEST_ASSERT_EQ(LifeTracker::destructCount.load(), 1);

    LifeTracker::Reset();

    {
        auto up = ITools::MakeIUnique<TestObject>(99);
        TEST_ASSERT(up.IsValid());
        TEST_ASSERT(up->value == 99);
    }
    TEST_ASSERT_EQ(LifeTracker::constructCount.load(), 1);
    TEST_ASSERT_EQ(LifeTracker::destructCount.load(), 1);

    return true;
}

// ============================================================================
// 测试 12：线程安全（基础）
// ============================================================================
bool Test_ThreadSafety()
{
    IMemPool* pool = IMemPool::CreatePool();
    IMemPoolAllocator<TestObject> alloc(pool);

    {
        auto sp = ISharedPtr<TestObject>::Allocate(alloc, 0);
        sp->value = 0;

        constexpr int kThreads = 4;
        constexpr int kIterations = 1000;
        std::vector<std::thread> threads;

        for (int t = 0; t < kThreads; ++t)
        {
            threads.emplace_back([&sp, kIterations]() {
                for (int i = 0; i < kIterations; ++i)
                {
                    auto spCopy = sp;                // 增加引用计数
                    spCopy->value++;                 // 原子？这里是测试引用计数安全
                }
                });
        }

        for (auto& th : threads) th.join();

        // 如果引用计数正确，对象仍存活
        TEST_ASSERT(sp.IsValid());
        // value 的最终值取决于竞争，但对象本身应完好
    }

    

    IMemPool::DestroyPool(pool);
    return true;
}

// ============================================================================
// 测试 13：类型安全转换 (CastTo)
// ============================================================================
struct BaseObject {
    int baseValue;
    virtual ~BaseObject() = default;
    virtual int GetType() const { return 0; }
};
struct DerivedObject : BaseObject {
    int derivedValue;
    int GetType() const override { return 1; }
};

bool Test_CastTo()
{
    // 此测试使用 new/delete 路径（因为 IMemPool 分配需要定制）
    auto spBase = ITools::MakeIShared<DerivedObject>();
    spBase->baseValue = 10;
    spBase->derivedValue = 20;

    // 向上转型（通过 ISharedPtr 的派生类构造）
    ISharedPtr<BaseObject> spBase2 = spBase.CastTo<BaseObject>();
    TEST_ASSERT(spBase2.IsValid());
    TEST_ASSERT(spBase2->GetType() == 1);  // 虚函数仍正确

    // 向下转型
    auto spDerived = spBase2.CastTo<DerivedObject>();
    TEST_ASSERT(spDerived.IsValid());
    TEST_ASSERT(spDerived->derivedValue == 20);

    // 非法转型返回空
    struct Unrelated : BaseObject {};
    auto spUnrelated = spBase2.CastTo<Unrelated>();
    TEST_ASSERT(!spUnrelated.IsValid());

    return true;
}

// ============================================================================
// 测试 14：内存池固定池（≤1024 字节）的正确性
// ============================================================================
bool Test_FixedPool()
{
    IMemPool* pool = IMemPool::CreatePool();
    IMemPoolAllocator<TestObject> alloc(pool);  // TestObject ≤ 1024

    {
        // 分配大量小对象，测试固定池的分配和释放
        std::vector<ISharedPtr<TestObject>> ptrs;
        constexpr int N = 500;

        for (int i = 0; i < N; ++i)
        {
            auto sp = ISharedPtr<TestObject>::Allocate(alloc, i);
            TEST_ASSERT(sp.IsValid());
            ptrs.push_back(std::move(sp));
        }

        TEST_ASSERT_EQ(LifeTracker::aliveCount.load(), N);

        // 随机释放一半
        for (int i = 0; i < N; i += 2)
        {
            ptrs[i] = nullptr;
        }

        // 重新分配，测试复用
        for (int i = 0; i < N / 2; ++i)
        {
            auto sp = ISharedPtr<TestObject>::Allocate(alloc, i * 100);
            TEST_ASSERT(sp.IsValid());
            ptrs.push_back(std::move(sp));
        }

        // 全部释放
        ptrs.clear();
        TEST_ASSERT_EQ(LifeTracker::aliveCount.load(), 0);
    }

    IMemPool::DestroyPool(pool);
    return true;
}

// ============================================================================
// TestSmartPtrArrays
// ============================================================================
int TestSmartPtrArrays()
{
    bool allPassed = true;

    RUN_TEST(Test_SharedPtr_SingleObject_Basic);
    RUN_TEST(Test_UniquePtr_SingleObject_Basic);
    RUN_TEST(Test_SharedPtr_Array_DefaultConstruct);
    RUN_TEST(Test_SharedPtr_Array_InitList);
    RUN_TEST(Test_SharedPtr_CopySemantics);
    RUN_TEST(Test_SharedPtr_MoveSemantics);
    RUN_TEST(Test_NullAndEdgeCases);
    RUN_TEST(Test_WeakPtr);
    RUN_TEST(Test_FragmentationResilience);
    RUN_TEST(Test_PoolExpansionResilience);
    RUN_TEST(Test_DefaultAllocator);
    RUN_TEST(Test_ThreadSafety);
    RUN_TEST(Test_CastTo);
    RUN_TEST(Test_FixedPool);

    std::cout << "\n========================================" << std::endl;
    if (allPassed)
    {
        std::cout << "  ALL TESTS PASSED" << std::endl;
    }
    else
    {
        std::cout << "  SOME TESTS FAILED" << std::endl;
    }
    std::cout << "========================================" << std::endl;

    return allPassed ? 0 : 1;
}