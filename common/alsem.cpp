/**
 * OpenAL cross platform audio library
 * Copyright (C) 1999-2007 by authors.
 * This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 *  License along with this library; if not, write to the
 *  Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 * Or go to http://www.gnu.org/copyleft/lgpl.html
 */

#include "config.h"

#include "alsem.h"

#include <system_error>

#include "opthelpers.h"


#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <limits>

namespace al {

semaphore::semaphore(unsigned int initial)
{
    if(initial > static_cast<unsigned int>(std::numeric_limits<int>::max()))
        throw std::system_error(std::make_error_code(std::errc::value_too_large));
    mSem = CreateSemaphore(nullptr, initial, std::numeric_limits<int>::max(), nullptr);
    if(mSem == nullptr)
        throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again));
}

semaphore::~semaphore()
{ CloseHandle(mSem); }

void semaphore::post()
{
    if(!ReleaseSemaphore(static_cast<HANDLE>(mSem), 1, nullptr))
        throw std::system_error(std::make_error_code(std::errc::value_too_large));
}

void semaphore::wait() noexcept
{ WaitForSingleObject(static_cast<HANDLE>(mSem), INFINITE); }

bool semaphore::try_wait() noexcept
{ return WaitForSingleObject(static_cast<HANDLE>(mSem), 0) == WAIT_OBJECT_0; }

} // namespace al

#else

/* Do not try using libdispatch on systems where it is absent. */
#if defined(AL_APPLE_HAVE_DISPATCH)

namespace al {

semaphore::semaphore(unsigned int initial)
{
    mSem = dispatch_semaphore_create(initial);
    if(!mSem)
        throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again));
}

semaphore::~semaphore()
{ dispatch_release(mSem); }

void semaphore::post()
{ dispatch_semaphore_signal(mSem); }

void semaphore::wait() noexcept
{ dispatch_semaphore_wait(mSem, DISPATCH_TIME_FOREVER); }

bool semaphore::try_wait() noexcept
{ return dispatch_semaphore_wait(mSem, DISPATCH_TIME_NOW) == 0; }

} // namespace al

#elif defined(__APPLE__)

namespace al {

semaphore::semaphore(unsigned int initial)
{
    mSem.sem = MACH_PORT_NULL;
    mSem.value = initial;

    kern_return_t ret = semaphore_create(mach_task_self(), &mSem.sem, SYNC_POLICY_FIFO, 0);
    if(ret != KERN_SUCCESS)
        throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again));
}

semaphore::~semaphore()
{ semaphore_destroy(mach_task_self(), mSem.sem); }

void semaphore::post()
{
    intptr_t val = mSem.value.fetch_add(1, std::memory_order_release);
    if (val <= 0) {
        semaphore_signal(mSem.sem);
    }
}

void semaphore::wait() noexcept
{
    intptr_t val = mSem.value.fetch_sub(1, std::memory_order_acquire);
    if (val < 0) {
        semaphore_wait(mSem.sem);
    }
}

bool semaphore::try_wait() noexcept
{
    intptr_t orig = mSem.value.load();
    while (orig < 0) {
        if (mSem.value.compare_exchange_weak(orig, orig+1, std::memory_order_relaxed)) {
            return false;
        }
    }
    return semaphore_wait(mSem.sem) == KERN_SUCCESS;
}

} // namespace al

#else /* !__APPLE__ */

#include <cerrno>

namespace al {

semaphore::semaphore(unsigned int initial)
{
    if(sem_init(&mSem, 0, initial) != 0)
        throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again));
}

semaphore::~semaphore()
{ sem_destroy(&mSem); }

void semaphore::post()
{
    if(sem_post(&mSem) != 0)
        throw std::system_error(std::make_error_code(std::errc::value_too_large));
}

void semaphore::wait() noexcept
{
    while(sem_wait(&mSem) == -1 && errno == EINTR) {
    }
}

bool semaphore::try_wait() noexcept
{ return sem_trywait(&mSem) == 0; }

} // namespace al

#endif /* __APPLE__ */

#endif /* _WIN32 */
