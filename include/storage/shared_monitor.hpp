#ifndef SHARED_MONITOR_HPP
#define SHARED_MONITOR_HPP

#include "storage/shared_datatype.hpp"

#include <boost/format.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/sync/interprocess_condition.hpp>
#include <boost/interprocess/sync/interprocess_mutex.hpp>

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

    template <typename Lock> void wait(Lock &lock) { internal().condition.wait(lock); }

    void notify_all() { internal().condition.notify_all(); }

    static void remove() { bi::shared_memory_object::remove(Data::name); }

  private:
    static constexpr int internal_size = 128;

    struct InternalData
    {
        mutex_type mutex;
        bi::interprocess_condition condition;
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
