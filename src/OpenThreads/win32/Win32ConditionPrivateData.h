/* -*-c++-*- OpenThreads library, Copyright (C) 2002 - 2007  The Open Thread Group
 *
 * This library is open source and may be redistributed and/or modified under  
 * the terms of the OpenSceneGraph Public License (OSGPL) version 0.0 or 
 * (at your option) any later version.  The full license is in LICENSE file
 * included with this distribution, and on the openscenegraph.org website.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the 
 * OpenSceneGraph Public License for more details.
*/

//
//
// WIN32ConditionPrivateData.h - Private data structure for Condition
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
#ifndef _WIN32CONDITIONPRIVATEDATA_H_
#define _WIN32CONDITIONPRIVATEDATA_H_

#include <OpenThreads/ScopedLock>

#include "Win32ThreadPrivateData.h"
#include "HandleHolder.h"

//原子加法操作，这里加0，相当于原子返回x
#define InterlockedGet(x) InterlockedExchangeAdd(x,0)

namespace OpenThreads {

class Condition;

class Win32ConditionPrivateData {
public:
    friend class Condition;
    /// number of waiters.
    long waiters_;

    /*  
    *  LPSECURITY_ATTRIBUTES lpSemaphoreAttributes  //安全属性，如果为NULL则是默认安全属性 
    *         LONG lInitialCount,                  //初始信号量的数量，要>=0且<=信号量的最大格式
    *         LONG lMaximumCount,                  //信号量数量的最大个数 
    *         LPCTSTR lpName                       //信号量的名称,如果与已存在的重名，返回存在的信号量
	*
	*
	* HANDLE WINAPI CreateEvent(
    *    LPSECURITY_ATTRIBUTES lpEventAttributes, //安全属性，如果为NULL则是默认安全属性 
    *    BOOL                  bManualReset,      //是否手工重置状态，true表示手工重置，false表示自动重置
    *    BOOL                  bInitialState,     //初始状态，true表示有信号，false表示无信号。
    *    LPCTSTR               lpName             //事件的名称,如果与已存在的重名，返回存在的事件量
    * );
	*
	*/
    Win32ConditionPrivateData ()
        :waiters_(0), 
         sema_(CreateSemaphore(NULL,0,0x7fffffff,NULL)),
         waiters_done_(CreateEvent(NULL,FALSE,FALSE,NULL)),
         was_broadcast_(0)
    {
    }

    ~Win32ConditionPrivateData ();

	//向所有信号量都发送信号
    inline int broadcast ()
    {
        int have_waiters = 0;
        long w = InterlockedGet(&waiters_);

        if (w > 0)
        {
          // we are broadcasting.  
          was_broadcast_ = 1;
          have_waiters = 1;
        }

        int result = 0;
        if (have_waiters)
        {
            // Wake up all the waiters.
            ReleaseSemaphore(sema_.get(), w, NULL);

            cooperativeWait(waiters_done_.get(), INFINITE);

            //end of broadcasting
            was_broadcast_ = 0;
        }
        return result;
    }

	/*
	 * BOOL WINAPI ReleaseSemaphore(
     *	    HANDLE hSemaphore,     //信号量句柄
     *	    LONG   lReleaseCount,  //释放的信号对象数量，释放后导致当前信号量总数大于最大值时，数返回FALSE;
     *	    LPLONG lpPreviousCount //返回上一次的信号对象数量
     * );
     */
	// 发送信号
    inline int signal()
    {
        long w = InterlockedGet(&waiters_);
        int have_waiters = w > 0;
 
        int result = 0;

        if (have_waiters)
        {
            if( !ReleaseSemaphore(sema_.get(),1,NULL) )
                result = -1;
        }
        return result;
    }
	//等待超时
    inline int wait (Mutex& external_mutex, long timeout_ms)
    {
    
        // Prevent race conditions on the <waiters_> count.
		//等待的原子加一
        InterlockedIncrement(&waiters_);

        int result = 0;

        ReverseScopedLock<Mutex> lock(external_mutex);

        // wait in timeslices, giving testCancel() a change to
        // exit the thread if requested.
        try {
              DWORD dwResult = cooperativeWait(sema_.get(), timeout_ms);
            if(dwResult != WAIT_OBJECT_0)
                result = (int)dwResult;
        }
        catch(...){
            // thread is canceled in cooperative wait , do cleanup
            long w = InterlockedDecrement(&waiters_);
            int last_waiter = was_broadcast_ && w == 0;

            if (last_waiter)  SetEvent(waiters_done_.get());
            // rethrow
            throw;
        }

        
        // We're ready to return, so there's one less waiter.
        long w = InterlockedDecrement(&waiters_);
        int last_waiter = was_broadcast_ && w == 0;

        if (result != -1 && last_waiter)
            SetEvent(waiters_done_.get());

        return result;
    }

protected:

  /// Serialize access to the waiters count.
  /// Mutex waiters_lock_;
  /// Queue up threads waiting for the condition to become signaled.
  HandleHolder sema_;
  /**
   * An auto reset event used by the broadcast/signal thread to wait
   * for the waiting thread(s) to wake up and get a chance at the
   * semaphore.
   */
  HandleHolder waiters_done_;
  /// Keeps track of whether we were broadcasting or just signaling.
  size_t was_broadcast_;
};

#undef InterlockedGet

}







#endif // !_WIN32CONDITIONPRIVATEDATA_H_



