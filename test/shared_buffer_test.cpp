// Test shared_buffer.h
//
// Copyright 2017 Juha Reunanen

#include "../shared_buffer.h"
#include <gtest/gtest.h>
#include <thread>
#include <atomic>

namespace {

    class shared_buffer_test : public ::testing::Test {
    protected:
        shared_buffer_test() {
            // You can do set-up work for each test here.
        }

        virtual ~shared_buffer_test() {
            // You can do clean-up work that doesn't throw exceptions here.
        }

        virtual void SetUp() {
            // Code here will be called immediately after the constructor (right before each test).
        }

        virtual void TearDown() {
            // Code here will be called immediately after each test (right before the destructor).
        }

        shared_buffer<std::string> buffer;
    };

    TEST_F(shared_buffer_test, DoesNotPopIfNothingPushed) {
        std::string retrievedValue;
        EXPECT_FALSE(buffer.pop_front(retrievedValue));
        EXPECT_EQ(retrievedValue, "");
    }

    TEST_F(shared_buffer_test, PopsPushedValuesSingleThread) {
        buffer.push_back("test1");
        buffer.push_back("test2");

        std::string retrievedValue;
        EXPECT_TRUE(buffer.pop_front(retrievedValue));
        EXPECT_EQ(retrievedValue, "test1");
        EXPECT_TRUE(buffer.pop_front(retrievedValue));
        EXPECT_EQ(retrievedValue, "test2");
    }

    TEST_F(shared_buffer_test, PopsPushedValueImmediately) {

        std::thread consumer{ [&] {
            const auto t0 = std::chrono::steady_clock::now();
            std::string value;
            EXPECT_TRUE(buffer.pop_front(value, std::chrono::seconds(1)));
            EXPECT_EQ(value, "test");
            EXPECT_LE(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count(), 10);
        } };

        std::thread producer{ [&] {
            buffer.push_back("test");
        } };

        producer.join();
        consumer.join();
    }

    TEST_F(shared_buffer_test, Halts) {
        const auto t0 = std::chrono::steady_clock::now();

        std::thread consumer{ [&] {
            const auto t1 = std::chrono::steady_clock::now();
            std::string value;
            EXPECT_FALSE(buffer.pop_front(value, std::chrono::seconds(1)));
            EXPECT_LE(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t1).count(), 10);
        } };

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        buffer.halt();
        consumer.join();

        EXPECT_LE(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count(), 20);
    }

    TEST_F(shared_buffer_test, ReturnsIfHaltedBeforehand) {
        buffer.halt();

        const auto t0 = std::chrono::steady_clock::now();
        std::string value;
        EXPECT_FALSE(buffer.pop_front(value, std::chrono::seconds(1)));
        EXPECT_LE(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count(), 10);
    }

    TEST_F(shared_buffer_test, PopsPushedValuesDifferentThreads) {

        const int valuesToPush = 1000;

        // Deliberately relay strings rather than ints, so we easily get some additional debug checks.

        std::thread consumer{ [&] {
            std::string value;
            int expectedNumber = 0;
            while (expectedNumber < valuesToPush && buffer.pop_front(value, std::chrono::seconds(1))) {
                const int number = std::stoi(value);
                EXPECT_EQ(number, expectedNumber);
                expectedNumber = number + 1;
            }
            EXPECT_EQ(expectedNumber, valuesToPush);
        } };

        std::thread producer{ [&] {
            for (int i = 0; i < valuesToPush; ++i) {
                std::ostringstream number;
                number << i;
                buffer.push_back(number.str());
            }
        } };

        producer.join();
        consumer.join();

    }

    TEST_F(shared_buffer_test, HandlesMultipleProducersAndConsumers) {

        const int valuesToPush = 100;
        const int consumerCount = 20;
        const int producerCount = 10;

        std::vector<std::thread> consumers, producers;

        std::mutex mutex;
        std::map<std::string, size_t> consumedValueCounts;

        for (int i = 0; i < consumerCount; ++i) {
            consumers.push_back(std::thread([&] {
                std::string value;
                while (buffer.pop_front(value, std::chrono::seconds(1))) {
                    std::lock_guard<std::mutex> lock(mutex);
                    ++consumedValueCounts[value];
                }
            }));
        }

        for (int i = 0; i < producerCount; ++i) {
            producers.push_back(std::thread([&] {
                for (int i = 0; i < valuesToPush; ++i) {
                    std::ostringstream number;
                    number << i;
                    buffer.push_back(number.str());
                }
            }));
        }

        for (auto& producer : producers) {
            producer.join();
        }

        buffer.halt();

        for (auto& consumer : consumers) {
            consumer.join();
        }

        EXPECT_EQ(consumedValueCounts.size(), valuesToPush);
        for (const auto& i : consumedValueCounts) {
            EXPECT_EQ(i.second, producerCount);
        }
    }

}  // namespace
