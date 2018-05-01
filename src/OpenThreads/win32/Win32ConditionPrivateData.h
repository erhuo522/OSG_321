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

//ԭ�Ӽӷ������������0���൱��ԭ�ӷ���x
#define InterlockedGet(x) InterlockedExchangeAdd(x,0)

namespace OpenThreads {

class Condition;

class Win32ConditionPrivateData {
public:
    friend class Condition;
    /// number of waiters.
    long waiters_;

    /*  
    *  LPSECURITY_ATTRIBUTES lpSemaphoreAttributes  //��ȫ���ԣ����ΪNULL����Ĭ�ϰ�ȫ���� 
    *         LONG lInitialCount,                  //��ʼ�ź�����������Ҫ>=0��<=�ź���������ʽ
    *         LONG lMaximumCount,                  //�ź��������������� 
    *         LPCTSTR lpName                       //�ź���������,������Ѵ��ڵ����������ش��ڵ��ź���
	*
	*
	* HANDLE WINAPI CreateEvent(
    *    LPSECURITY_ATTRIBUTES lpEventAttributes, //��ȫ���ԣ����ΪNULL����Ĭ�ϰ�ȫ���� 
    *    BOOL                  bManualReset,      //�Ƿ��ֹ�����״̬��true��ʾ�ֹ����ã�false��ʾ�Զ�����
    *    BOOL                  bInitialState,     //��ʼ״̬��true��ʾ���źţ�false��ʾ���źš�
    *    LPCTSTR               lpName             //�¼�������,������Ѵ��ڵ����������ش��ڵ��¼���
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

	//�������ź����������ź�
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
     *	    HANDLE hSemaphore,     //�ź������
     *	    LONG   lReleaseCount,  //�ͷŵ��źŶ����������ͷź��µ�ǰ�ź��������������ֵʱ��������FALSE;
     *	    LPLONG lpPreviousCount //������һ�ε��źŶ�������
     * );
     */
	// �����ź�
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
	//�ȴ���ʱ
    inline int wait (Mutex& external_mutex, long timeout_ms)
    {
    
        // Prevent race conditions on the <waiters_> count.
		//�ȴ���ԭ�Ӽ�һ
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



