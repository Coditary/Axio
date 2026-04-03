#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace {

struct ArcHeader {
    std::atomic<std::uint64_t> strongCount;
    std::atomic<std::uint64_t> weakCount;
    std::uint64_t size;
    std::uint8_t alive;
    std::uint8_t reserved[7];
};

ArcHeader* headerFromObject(void* object) {
    if (object == nullptr) {
        return nullptr;
    }
    return reinterpret_cast<ArcHeader*>(static_cast<std::byte*>(object) - sizeof(ArcHeader));
}

void destroyStorageIfNeeded(ArcHeader* header) {
    if (header == nullptr) {
        return;
    }
    if (header->strongCount.load(std::memory_order_acquire) == 0 && header->weakCount.load(std::memory_order_acquire) == 0) {
        std::free(header);
    }
}

}  // namespace

extern "C" void* axio_arc_alloc(std::uint64_t size) {
    const std::size_t totalSize = sizeof(ArcHeader) + static_cast<std::size_t>(size);
    auto* header = static_cast<ArcHeader*>(std::malloc(totalSize));
    if (header == nullptr) {
        return nullptr;
    }
    header->strongCount.store(1, std::memory_order_release);
    header->weakCount.store(0, std::memory_order_release);
    header->size = size;
    header->alive = 1;
    void* object = reinterpret_cast<std::byte*>(header) + sizeof(ArcHeader);
    std::memset(object, 0, static_cast<std::size_t>(size));
    return object;
}

extern "C" void* axio_arc_retain(void* object) {
    if (auto* header = headerFromObject(object); header != nullptr && header->alive != 0) {
        header->strongCount.fetch_add(1, std::memory_order_acq_rel);
    }
    return object;
}

extern "C" void axio_arc_release(void* object) {
    ArcHeader* header = headerFromObject(object);
    if (header == nullptr) {
        return;
    }
    if (header->strongCount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        header->alive = 0;
        destroyStorageIfNeeded(header);
    }
}

extern "C" void* axio_weak_init(void* object) {
    if (auto* header = headerFromObject(object); header != nullptr && header->alive != 0) {
        header->weakCount.fetch_add(1, std::memory_order_acq_rel);
        return object;
    }
    return nullptr;
}

extern "C" void axio_weak_release(void* object) {
    ArcHeader* header = headerFromObject(object);
    if (header == nullptr) {
        return;
    }
    if (header->weakCount.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        destroyStorageIfNeeded(header);
    }
}

extern "C" void* axio_weak_load(void* object) {
    ArcHeader* header = headerFromObject(object);
    if (header == nullptr || header->alive == 0) {
        return nullptr;
    }
    header->strongCount.fetch_add(1, std::memory_order_acq_rel);
    return object;
}

extern "C" std::uint64_t axio_arc_strong_count(void* object) {
    ArcHeader* header = headerFromObject(object);
    if (header == nullptr) {
        return 0;
    }
    return header->strongCount.load(std::memory_order_acquire);
}
