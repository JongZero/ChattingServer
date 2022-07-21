#pragma once
#include "ServerDefine.h"

/// <summary>
/// 쓰레드의 부모 클래스, 교수님 예제
/// 2021. 05. 10
/// 정종영
/// </summary>
class BaseThread
{
public:
	BaseThread();
	virtual ~BaseThread();

private:
	struct SThread
	{
		BaseThread*		pThread;
		int				iIndex;
		unsigned int	uiThreadID;
		HANDLE			hHandle;
	};

protected:
	BOOL					m_bClose;			// 종료 플래그
	std::vector< SThread* >	m_ThreadVec;		// 쓰레드 관련 정보들

private:
	static	unsigned int __stdcall ThreadFunction(void* pARG);

private:
	virtual void			Begin(void) {}
	virtual void			End(void) {}
	virtual unsigned int	Run(int dwIndex) = 0;	// 쓰레드가 매 프레임 동작할 로직

public:
	size_t	Create(unsigned int uiThreadStat = CREATE_SUSPENDED, size_t nCount = 1);	// 쓰레드 생성 및 대기( 생성된 쓰레드 수 리턴 )
	size_t	Destroy(void);																// 쓰레드 종료( 종료된 쓰레드 수 리턴 )
	size_t	Resume(void);																// 쓰레드 재개( 이번 호출로 인해 재개된 쓰레드 수 리턴 )
	size_t	Suspend(void);
};