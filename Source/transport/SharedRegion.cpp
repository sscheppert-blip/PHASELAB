#include "transport/SharedRegion.h"

#if defined (_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#else
 #include <fcntl.h>
 #include <sys/mman.h>
 #include <sys/stat.h>
 #include <unistd.h>
#endif

namespace phaselab::link
{

SharedRegion::~SharedRegion()
{
    close();
}

#if defined (_WIN32)

bool SharedRegion::open (const std::string& name, std::size_t size)
{
    close();
    // "Local\" is the namespace of the user's session: every process of this user sees it.
    const std::string fullName = "Local\\" + name;
    const auto high = static_cast<DWORD> ((static_cast<unsigned long long> (size) >> 32) & 0xffffffffu);
    const auto low = static_cast<DWORD> (size & 0xffffffffu);

    HANDLE handle = CreateFileMappingA (INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, high, low, fullName.c_str());
    if (handle == nullptr)
        return false;

    void* view = MapViewOfFile (handle, FILE_MAP_ALL_ACCESS, 0, 0, size);
    if (view == nullptr)
    {
        CloseHandle (handle);
        return false;
    }

    mapping = handle;
    memory = view;
    bytes = size;
    return true;
}

void SharedRegion::close()
{
    if (memory != nullptr)
        UnmapViewOfFile (memory);
    if (mapping != nullptr)
        CloseHandle (static_cast<HANDLE> (mapping));
    memory = nullptr;
    mapping = nullptr;
    bytes = 0;
}

#else

bool SharedRegion::open (const std::string& name, std::size_t size)
{
    close();
    const std::string fullName = "/" + name;   // macOS allows at most 31 characters
    const int fd = shm_open (fullName.c_str(), O_RDWR | O_CREAT, 0600);
    if (fd < 0)
        return false;

    struct stat info {};
    if (fstat (fd, &info) != 0 || (static_cast<std::size_t> (info.st_size) < size && ftruncate (fd, static_cast<off_t> (size)) != 0))
    {
        ::close (fd);
        return false;
    }

    void* view = mmap (nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (view == MAP_FAILED)
    {
        ::close (fd);
        return false;
    }

    descriptor = fd;
    memory = view;
    bytes = size;
    return true;
}

void SharedRegion::close()
{
    if (memory != nullptr)
        munmap (memory, bytes);
    if (descriptor >= 0)
        ::close (descriptor);
    memory = nullptr;
    descriptor = -1;
    bytes = 0;
}

#endif

} // namespace phaselab::link
