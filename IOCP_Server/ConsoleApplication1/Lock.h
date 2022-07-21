#pragma once

#include <Windows.h>

/// <summary>
/// Critical Section의 Enter, Leave를 담당, 예전에 했던 것 참고
/// 2021. 05. 10
/// 정종영
/// </summary>
class Lock
{
public:
	Lock(CRITICAL_SECTION* cs) : m_CS(nullptr) { if (cs == nullptr) return; m_CS = cs; EnterCriticalSection(m_CS); };
	~Lock() { LeaveCriticalSection(m_CS); };

private:
	CRITICAL_SECTION* m_CS;

public:
	void Unlock() { LeaveCriticalSection(m_CS); }	// 소멸자에서 LeaveCS를 안하는 경우도 있다.
};