#pragma once
#include "BaseThread.h"

class ServerManager;

/// <summary>
/// IOCP 쓰레드, Send, Recv 처리 담당
/// 2021. 05. 10
/// 정종영
/// </summary>
class IOCPThread : public BaseThread
{
public:
	IOCPThread(ServerManager* server, HANDLE hIOCP);
	virtual ~IOCPThread();

private:
	ServerManager*			m_ServerManager;
	HANDLE                  m_hIOCP;

private:
	virtual void			Begin();
	virtual void			End();
	virtual unsigned int	Run(int dwIndex);

private:
	void ProcessRecv(DWORD dwNumberOfBytesTransferred, SOverlapped* psOverlapped, SSocket* psSocket);
	void ProcessSend(DWORD dwNumberOfBytesTransferred, SOverlapped* psOverlapped, SSocket* psSocket);
};

