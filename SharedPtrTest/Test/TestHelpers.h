// TestHelpers.h
#pragma once

#include <cassert>
#include <iostream>
#include <atomic>
#include <sstream>

// ============================================================================
// 对象生命周期追踪器
// ============================================================================
struct LifeTracker {
    static std::atomic<int> aliveCount;
    static std::atomic<int> constructCount;
    static std::atomic<int> destructCount;
    static std::atomic<int> moveConstructCount;
    static std::atomic<int> moveAssignCount;

    static void Reset()
    {
        aliveCount = 0;
        constructCount = 0;
        destructCount = 0;
        moveConstructCount = 0;
        moveAssignCount = 0;
    }

    static bool Verify(int expectedAlive, int expectedConstructed, int expectedDestructed)
    {
        return aliveCount == expectedAlive
            && constructCount == expectedConstructed
            && destructCount == expectedDestructed;
    }

    static void PrintStats(const char* label)
    {
        std::cout << "[" << label << "] "
            << "alive=" << aliveCount
            << " constructed=" << constructCount
            << " destructed=" << destructCount
            << " moved_ctor=" << moveConstructCount
            << " moved_assign=" << moveAssignCount
            << std::endl;
    }
};

std::atomic<int> LifeTracker::aliveCount{ 0 };
std::atomic<int> LifeTracker::constructCount{ 0 };
std::atomic<int> LifeTracker::destructCount{ 0 };
std::atomic<int> LifeTracker::moveConstructCount{ 0 };
std::atomic<int> LifeTracker::moveAssignCount{ 0 };

// ============================================================================
// 测试用对象
// ============================================================================
struct TestObject {
    int value;
    bool valid;

    TestObject() : value(0), valid(true)
    {
        LifeTracker::aliveCount++;
        LifeTracker::constructCount++;
    }

    explicit TestObject(int v) : value(v), valid(true)
    {
        LifeTracker::aliveCount++;
        LifeTracker::constructCount++;
    }

    TestObject(const TestObject& other) : value(other.value), valid(true)
    {
        LifeTracker::aliveCount++;
        LifeTracker::constructCount++;
    }

    TestObject(TestObject&& other) noexcept : value(other.value), valid(true)
    {
        other.valid = false;
        LifeTracker::aliveCount++;
        LifeTracker::moveConstructCount++;
    }

    TestObject& operator=(const TestObject& other)
    {
        if (this != &other) { value = other.value; valid = true; }
        return *this;
    }

    TestObject& operator=(TestObject&& other) noexcept
    {
        if (this != &other)
        {
            value = other.value;
            valid = true;
            other.valid = false;
            LifeTracker::moveAssignCount++;
        }
        return *this;
    }

    ~TestObject()
    {
        if (valid)
        {
            LifeTracker::aliveCount--;
            LifeTracker::destructCount++;
        }
    }
};

// ============================================================================
// 轻量级断言宏
// ============================================================================
#define TEST_ASSERT(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAIL: " << #cond << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while(0)

#define TEST_ASSERT_EQ(a, b) \
    do { \
        if ((a) != (b)) { \
            std::cerr << "FAIL: " << #a << " == " << #b \
                      << "  (" << (a) << " != " << (b) << ")" \
                      << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            return false; \
        } \
    } while(0)

#define RUN_TEST(testFunc) \
    do { \
        std::cout << "Running " << #testFunc << "..." << std::endl; \
        LifeTracker::Reset(); \
        if (testFunc()) { \
            std::cout << "  PASSED" << std::endl; \
        } else { \
            std::cout << "  FAILED" << std::endl; \
            allPassed = false; \
        } \
    } while(0)