#include "BaseThread.h"
#include "ServerDefine.h"

BaseThread::BaseThread()
	: m_bClose(FALSE)
{

}

BaseThread::~BaseThread()
{

}

unsigned int __stdcall BaseThread::ThreadFunction(void* pARG)
{
	assert(nullptr != pARG);
	SThread* psThread = static_cast<SThread*>(pARG);

	psThread->pThread->Run(psThread->iIndex);
	
	return 0;
}

size_t BaseThread::Create(unsigned int uiThreadStat /*= CREATE_SUSPENDED*/, size_t nCount /*= 1*/)
{
	assert(m_ThreadVec.empty());

	m_bClose = FALSE;

	for (size_t i = 0; i < nCount; ++i)
	{
		SThread* psThread = new SThread;

		psThread->pThread = this;
		psThread->iIndex = i;
		psThread->hHandle = reinterpret_cast<HANDLE>(_beginthreadex(nullptr, 0, &BaseThread::ThreadFunction, psThread, uiThreadStat, &psThread->uiThreadID));

		m_ThreadVec.push_back(psThread);
	}

	// 쓰레드를 동작시킴
	Begin();

	return m_ThreadVec.size();
}

size_t BaseThread::Destroy(void)
{
	assert(!m_ThreadVec.empty());

	m_bClose = TRUE;

	// 핸들을 닫기 전에 해야될 것 처리
	End();

	size_t nCount = m_ThreadVec.size();
	for (size_t i = 0; i < nCount; ++i)
	{
		WaitForSingleObject(m_ThreadVec[i]->hHandle, INFINITE);
		CloseHandle(m_ThreadVec[i]->hHandle);
		delete m_ThreadVec[i];
	}

	m_ThreadVec.clear();

	return nCount;
}

size_t BaseThread::Resume(void)
{
	assert(!m_ThreadVec.empty());

	size_t nCount = 0;

	for (size_t i = 0; i < m_ThreadVec.size(); ++i)
	{
		if (1 == ResumeThread(m_ThreadVec[i]->hHandle))
		{
			++nCount;
		}
	}

	return nCount;
}

size_t BaseThread::Suspend(void)
{
	assert(!m_ThreadVec.empty());

	size_t nCount = 0;

	for (size_t i = 0; i < m_ThreadVec.size(); ++i)
	{
		if (1 == SuspendThread(m_ThreadVec[i]->hHandle))
		{
			++nCount;
		}
	}

	return nCount;
}

