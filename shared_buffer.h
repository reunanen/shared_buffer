// A generic buffer implementation for pushing data from one thread to another
//
// Copyright (c) 2017, 2020, 2026 Juha Reunanen

#ifndef SHARED_BUFFER_H
#define SHARED_BUFFER_H

#include <chrono>
#include <deque>
#include <mutex>
#include <optional>
#include <condition_variable>

template <class T> class shared_buffer {
public:
    shared_buffer() {}

    bool push_back(const T& value) {
        {
            const auto push_lock = acquire_push_lock();
            if (!push_lock.has_value()) {
                return false;
            }
            values.push_back(value);
            ready = true;
        }
        not_empty.notify_one();
        return true;
    }

    bool push_back(T&& value) {
        {
            const auto push_lock = acquire_push_lock();
            if (!push_lock.has_value()) {
                return false;
            }
            values.push_back(std::move(value));
            ready = true;
        }
        not_empty.notify_one();
        return true;
    }

    bool pop_front(T& value) {
        // No waiting.
        std::lock_guard<std::mutex> lock(mutex);
        return pop_front_when_already_locked(value);
    }

    template <class Duration>
    bool pop_front(T& value, const Duration& max_duration) {

        // See if we already have something we can readily pop.
        if (pop_front(value)) {
            return true;
        }

        // We don't have anything right now, so let's just wait.
        std::unique_lock<std::mutex> lock(mutex);
        if (!not_empty.wait_for(lock, max_duration, [this]{ return this->ready; })) {
            return false;
        }

        // After a successful wait, we own the lock.
        return pop_front_when_already_locked(value);
	}

	size_t size() const {
		std::lock_guard<std::mutex> lock(mutex);
		return values.size();
	}

    bool empty() const {
        return size() == 0;
    }

    // Force threads waiting in pop_front() or push_back() to return.
    void halt() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            ready = true;
            enabled = false;
        }
        not_empty.notify_all();
        not_full.notify_all();
    }

    void set_max_size(size_t max_size) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            this->max_size = max_size;
        }
        not_full.notify_all();
    }

    bool is_enabled() const {
        return enabled;
    }

private:
    shared_buffer(const shared_buffer&) = delete; // not construction-copyable
    shared_buffer& operator=(const shared_buffer&) = delete; // not copyable

    std::optional<std::unique_lock<std::mutex>> acquire_push_lock() {
        std::unique_lock<std::mutex> lock(mutex);
        if (max_size > 0) {
            not_full.wait(lock, [this] { return values.size() < max_size || !enabled; });
            if (!enabled) {
                return std::nullopt;
            }
        }
        return lock;
    }

    bool pop_front_when_already_locked(T& value) {
        if (!values.empty()) {
            value = std::move(this->values.front());
            this->values.pop_front();
            ready = false;
            not_full.notify_one();
            return true;
        }
        else {
            return false;
        }
    }

	std::deque<T> values;
    size_t max_size = 0;

	mutable std::mutex mutex;
    std::condition_variable not_empty;
    std::condition_variable not_full;
	bool ready = false;
    bool enabled = true;
};

#endif // SHARED_BUFFER_H
