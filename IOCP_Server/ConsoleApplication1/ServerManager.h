#pragma once

#include "ServerDefine.h"

class IOCPThread;

/// <summary>
/// 서버의 총 관리자
/// 2021. 05. 10
/// 정종영
/// </summary>
class ServerManager
{
public:
	ServerManager();
	~ServerManager();

private:
	static SOCKET			m_ListenSock;		// listen 소켓
	CRITICAL_SECTION        m_CS_SocketList;	// 아래 리스트에서 사용될 크리티컬 섹션
	std::list< SSocket* >	m_SocketList;		// 연결된 클라이언트 정보들

	HANDLE                  m_hIOCP;			// Input/Output Completion Port( 입출력 완료 포트, 내부적으로 Queue 를 생성 ) 핸들
	static BOOL             m_bExit;			// 주 쓰레드 종료 플래그
	
	IOCPThread*				m_IOCPThread;		// 워커 쓰레드

private:
	static void SignalFunction(int iSignalNumber);
	static int CALLBACK AcceptCondition(LPWSABUF lpCallerId, LPWSABUF lpCallerData, LPQOS lpSQOS, LPQOS lpGQOS, LPWSABUF lpCalleeId, LPWSABUF lpCalleeData, GROUP FAR* g, DWORD_PTR dwCallbackData);

	BOOL AddSocket(SSocket* psSocket);
	BOOL SendPacket(SSocket* psSocket, SHeader* psPacket);

public:
	bool Initialize();
	void Release();
	int Run();

	BOOL RemoveSocket(SOCKET socket);
	BOOL Receive(SSocket* psSocket, SOverlapped* psOverlapped = nullptr);	// 클라이언트로부터 패킷 수신 오버랩드 걸기
	void BroadcastPacket(SHeader* psPacket);
};
