#pragma once

#include <cstddef>
#include <string>

namespace phaselab::link
{

// A named block of shared memory that every plugin binary and every process of the same user can
// open (D-21, open point O-04). A plain static singleton is not enough: each plugin binary has its
// own statics, and sandboxing hosts run plugins in separate processes. New memory is zero-filled.
// Never call open() on the audio thread.
class SharedRegion
{
public:
    SharedRegion() = default;
    ~SharedRegion();
    SharedRegion (const SharedRegion&) = delete;
    SharedRegion& operator= (const SharedRegion&) = delete;

    bool open (const std::string& name, std::size_t size);
    void close();

    void* data() const noexcept { return memory; }
    bool isOpen() const noexcept { return memory != nullptr; }

private:
    void* memory = nullptr;
    std::size_t bytes = 0;
#if defined (_WIN32)
    void* mapping = nullptr;
#else
    int descriptor = -1;
#endif
};

} // namespace phaselab::link
