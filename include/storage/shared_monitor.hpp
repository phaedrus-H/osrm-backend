#ifndef SHARED_MONITOR_HPP
#define SHARED_MONITOR_HPP

#include "storage/shared_datatype.hpp"

#include <boost/format.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/sync/interprocess_condition.hpp>
#include <boost/interprocess/sync/interprocess_mutex.hpp>
#include <boost/interprocess/sync/interprocess_semaphore.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>

namespace osrm
{
namespace storage
{

namespace
{
namespace bi = boost::interprocess;

template <class Lock> class InvertedLock
{
    Lock &lock;

  public:
    InvertedLock(Lock &lock) : lock(lock) { lock.unlock(); }
    ~InvertedLock() { lock.lock(); }
};
}

// The shared monitor implementation based on a semaphore and mutex
template <typename Data> struct SharedMonitor
{
    using mutex_type = bi::interprocess_mutex;

    SharedMonitor(const Data &initial_data)
    {
        shmem = bi::shared_memory_object(bi::open_or_create, Data::name, bi::read_write);

        bi::offset_t size = 0;
        if (shmem.get_size(size) && size == 0)
        {
            shmem.truncate(internal_size + sizeof(Data));
            region = bi::mapped_region(shmem, bi::read_write);
            new (&internal()) InternalData;
            new (&data()) Data(initial_data);
        }
        else
        {
            region = bi::mapped_region(shmem, bi::read_write);
        }
    }

    SharedMonitor()
    {
        try
        {
            shmem = bi::shared_memory_object(bi::open_only, Data::name, bi::read_write);

            bi::offset_t size = 0;
            if (!shmem.get_size(size) || size != internal_size + sizeof(Data))
            {
                auto message =
                    boost::format("Wrong shared memory block '%1%' size %2%, expected %3% bytes") %
                    (const char *)Data::name % size % (internal_size + sizeof(Data));
                throw util::exception(message.str() + SOURCE_REF);
            }

            region = bi::mapped_region(shmem, bi::read_write);
        }
        catch (const bi::interprocess_exception &exception)
        {
            auto message = boost::format("No shared memory block '%1%' found, have you forgotten "
                                         "to run osrm-datastore?") %
                           (const char *)Data::name;
            throw util::exception(message.str() + SOURCE_REF);
        }
    }

    Data &data() const
    {
        return *reinterpret_cast<Data *>(reinterpret_cast<char *>(region.get_address()) +
                                         internal_size);
    }

    mutex_type &get_mutex() const { return internal().mutex; }

    template <typename Lock> void wait(Lock &lock)
    {
#if defined(__linux__)
        internal().condition.wait(lock);
#else
        auto index = internal().head++;
        auto sema = new (internal().buffer + (index & 255) * sizeof(bi::interprocess_semaphore))
            bi::interprocess_semaphore(0);
        {
            InvertedLock<Lock> inverted_lock(lock);
            sema->wait();
        }
        sema->~interprocess_semaphore();
#endif
    }

    void notify_all()
    {
#if defined(__linux__)
        internal().condition.notify_all();
#else
        {
            bi::scoped_lock<mutex_type> lock(internal().mutex);
            while (internal().tail != internal().head)
            {
                reinterpret_cast<bi::interprocess_semaphore *>(
                    internal().buffer +
                    (internal().tail & 255) * sizeof(bi::interprocess_semaphore))
                    ->post();
                ++internal().tail;
            }
        }
#endif
    }

    static void remove() { bi::shared_memory_object::remove(Data::name); }

  private:
    static constexpr int internal_size = 4 * 4096;

    struct InternalData
    {
#if !defined(__linux__)
        InternalData() : head(0), tail(0){};
#endif

        mutex_type mutex;
#if defined(__linux__)
        bi::interprocess_condition condition;
#else
        std::size_t head, tail;
        char buffer[256 * sizeof(bi::interprocess_semaphore)];
#endif
    };
    static_assert(sizeof(InternalData) <= internal_size, "not enough space to place internal data");
    static_assert(alignof(Data) <= internal_size, "incorrect data alignment");

    InternalData &internal() const
    {
        return *reinterpret_cast<InternalData *>(reinterpret_cast<char *>(region.get_address()));
    }

    bi::shared_memory_object shmem;
    bi::mapped_region region;
};
}
}

#endif // SHARED_MONITOR_HPP
