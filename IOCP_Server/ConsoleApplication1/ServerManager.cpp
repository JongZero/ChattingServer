#include "ServerManager.h"

#include "ErrManager.h"
#include <signal.h>
#include <process.h>
#include <vector>
#include <assert.h>

#include "IOCPThread.h"

BOOL ServerManager::m_bExit = FALSE;
SOCKET ServerManager::m_ListenSock = INVALID_SOCKET;

ServerManager::ServerManager()
	: m_hIOCP(nullptr), m_IOCPThread(nullptr), m_CS_SocketList()
{

}

ServerManager::~ServerManager()
{

}

void ServerManager::SignalFunction(int iSignalNumber)
{
	if (SIGINT == iSignalNumber)
	{
		// 종료 플래그 켬
		m_bExit = TRUE;

		// for main()
		if (INVALID_SOCKET != m_ListenSock)
		{
			closesocket(m_ListenSock);
			m_ListenSock = INVALID_SOCKET;
		}
	}
}

int CALLBACK ServerManager::AcceptCondition(LPWSABUF lpCallerId, LPWSABUF lpCallerData, LPQOS lpSQOS, LPQOS lpGQOS, LPWSABUF lpCalleeId, LPWSABUF lpCalleeData, GROUP FAR* g, DWORD_PTR dwCallbackData)
{
	// 최대 접속 유저수 제한하지 않음
	if (MAX_USER_COUNT == 0)
	{
		return CF_ACCEPT;   // 연결 수락
	}

	// WSAAccept() 의 마지막 인자에 넣었던 값
	// 이 예제에서는 최대 접속 유저수를 제한하기 위해서 g_listSockets 변수의 주소를 넘겼다.
	const std::list< SSocket* >* const cpclistClients = reinterpret_cast<std::list< SSocket* > *>(dwCallbackData);

	// 최대 접속 유저수 제한
	if (cpclistClients->size() >= MAX_USER_COUNT)
	{
		return CF_REJECT;   // 연결 거절
	}

	return CF_ACCEPT;       // 연결 수락
}

BOOL ServerManager::AddSocket(SSocket* psSocket)
{
	assert(nullptr != psSocket);

	// 클라이언트 정보를 list 에 추가

	{	// 임계 영역
		Lock _lock(&m_CS_SocketList);

		// 클라이언트 중복 검사
		for (auto it : m_SocketList)
		{
			if (it == psSocket)
			{
				// Leave Critical Section
				_lock.Unlock();
				return FALSE;
			}
		}

		// 클라이언트 등록
		m_SocketList.push_back(psSocket);
	}

	return TRUE;
}

BOOL ServerManager::RemoveSocket(SOCKET socket)
{
	assert(INVALID_SOCKET != socket);

	// 임계 영역
	{
		Lock _lock(&m_CS_SocketList);

		// 클라이언트 종료 및 삭제
		for (auto it = m_SocketList.begin(); it != m_SocketList.end(); ++it)
		{
			SSocket* psSocket = *it;

			if (psSocket->socket == socket)
			{
				m_SocketList.erase(it);

				// Leave Critical Section
				_lock.Unlock();

				printf_s("[TCP 서버] [%15s:%5d] 클라이언트와 종료\n", psSocket->strIP.c_str(), psSocket->usPort);

				// shutdown()을 호출해 소켓 종료 절차를 진행시킨다.
				shutdown(psSocket->socket, SD_BOTH);

				// 소켓의 카운트가 0이라면 delete (close socket)을 한다.
				if (psSocket->useCount == 0)
				{
					delete psSocket;
				}
				// 0이 아니라면 아직 오버랩드 요청에 대한 완료 처리가 안된 것이므로 다음 번 완료 처리 때 닫기 위해 확인용 변수만 true로 바꿔준다.
				else
				{
					psSocket->checkClose = true;
				}

				return TRUE;
			}
		}
	}

	return FALSE;
}

BOOL ServerManager::Receive(SSocket* psSocket, SOverlapped* psOverlapped /*= nullptr*/)
{
	assert(nullptr != socket);

	// 사용할 오버랩드를 받지 않았으면 생성
	if (nullptr == psOverlapped)
	{
		psOverlapped = new SOverlapped;
	}

	// 오버랩드 셋팅
	psOverlapped->eIOType = SOverlapped::EIOType::EIOType_Recv;
	psOverlapped->socket = psSocket->socket;

	// WSABUF 셋팅
	WSABUF wsaBuffer;
	wsaBuffer.buf = psOverlapped->szBuffer + psOverlapped->iDataSize;
	wsaBuffer.len = sizeof(psOverlapped->szBuffer) - psOverlapped->iDataSize;

	// WSARecv() 오버랩드 걸기
	DWORD dwNumberOfBytesRecvd = 0, dwFlag = 0;

	int iResult = WSARecv(psOverlapped->socket,
		&wsaBuffer,
		1,
		&dwNumberOfBytesRecvd,
		&dwFlag,
		&psOverlapped->wsaOverlapped,
		nullptr);

	if ((SOCKET_ERROR == iResult) && (WSAGetLastError() != WSA_IO_PENDING))
	{
		char szBuffer[BUFSIZE] = { 0, };
		sprintf_s(szBuffer, "[TCP 서버] 에러 발생 -- WSARecv() :");
		ErrManager::ErrDisplay(szBuffer);

		delete psOverlapped;
		return FALSE;
	}

	{   // 임계 영역
		Lock _lock(&psSocket->CS_useCount);

		// 처리되었다면 카운트 증가
		psSocket->useCount++;
	}

	return TRUE;
}

BOOL ServerManager::SendPacket(SSocket* psSocket, SHeader* psPacket)
{
	assert(nullptr != psSocket);
	assert(nullptr != psPacket);

	// 등록되지 않은 패킷은 전송할 수 없다.
	// [서버] -> [클라]
	if (ES2C_TYPE_MAX <= psPacket->usType)
	{
		return FALSE;
	}

	// 오버랩드 셋팅
	SOverlapped* psOverlapped = new SOverlapped;
	psOverlapped->eIOType = SOverlapped::EIOType::EIOType_Send;
	psOverlapped->socket = psSocket->socket;

	// 패킷 복사
	psOverlapped->iDataSize = 2 + psPacket->usSize;

	if (sizeof(psOverlapped->szBuffer) < psOverlapped->iDataSize)
	{
		delete psOverlapped;
		return FALSE;
	}

	memcpy_s(psOverlapped->szBuffer, sizeof(psOverlapped->szBuffer), psPacket, psOverlapped->iDataSize);

	// WSABUF 셋팅
	WSABUF wsaBuffer;
	wsaBuffer.buf = psOverlapped->szBuffer;
	wsaBuffer.len = psOverlapped->iDataSize;

	// WSASend() 오버랩드 걸기
	DWORD dwNumberOfBytesSent = 0;

	int iResult = WSASend(psOverlapped->socket,
		&wsaBuffer,
		1,
		&dwNumberOfBytesSent,
		0,
		&psOverlapped->wsaOverlapped,
		nullptr);

	if ((SOCKET_ERROR == iResult) && (WSAGetLastError() != WSA_IO_PENDING))
	{
		char szBuffer[BUFSIZE] = { 0, };
		sprintf_s(szBuffer, "[TCP 서버] 에러 발생 -- WSASend() :");
		ErrManager::ErrDisplay(szBuffer);

		delete psOverlapped;
		return FALSE;
	}

	{   // 임계 영역
		Lock _lock(&psSocket->CS_useCount);

		// 처리되었다면 카운트 증가
		psSocket->useCount++;
	}

	return TRUE;
}

void ServerManager::BroadcastPacket(SHeader* psPacket)
{
	assert(nullptr != psPacket);

	// 임계 영역
	{
		Lock _lock(&m_CS_SocketList);

		// 전체 클라이언트 소켓에 패킷 송신
		for (auto it : m_SocketList)
		{
			// 패킷 송신
			SendPacket(it, psPacket);
		}
	}
}

bool ServerManager::Initialize()
{
	// Ctrl - C 를 누르면,
   // 주 쓰레드를 포함한 모든 쓰레드들이 정상 종료를 할 수 있도록 함
	signal(SIGINT, ServerManager::SignalFunction);

	char szBuffer[BUFSIZE] = { 0, };

	// 윈속 초기화
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
	{
		sprintf_s(szBuffer, "[TCP 서버] 에러 발생 -- WSAStartup() :");
		ErrManager::ErrDisplay(szBuffer);

		return false;
	}

	// 1. WSASocket()
	m_ListenSock = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);

	if (INVALID_SOCKET == m_ListenSock)
	{
		sprintf_s(szBuffer, "[TCP 서버] 에러 발생 -- WSASocket() :");
		ErrManager::ErrDisplay(szBuffer);

		WSACleanup();
		return false;
	}

	// 2. bind()
	SOCKADDR_IN serveraddr;
	ZeroMemory(&serveraddr, sizeof(serveraddr));
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
	serveraddr.sin_port = htons(PORT);
	if (SOCKET_ERROR == bind(m_ListenSock, reinterpret_cast<SOCKADDR*>(&serveraddr), sizeof(serveraddr)))
	{
		sprintf_s(szBuffer, "[TCP 서버] 에러 발생 -- bind() :");
		ErrManager::ErrDisplay(szBuffer);

		closesocket(m_ListenSock);
		m_ListenSock = INVALID_SOCKET;

		WSACleanup();
		return false;
	}

	// 3. listen()
	if (SOCKET_ERROR == listen(m_ListenSock, 5))
	{
		sprintf_s(szBuffer, "[TCP 서버] 에러 발생 -- listen() :");
		ErrManager::ErrDisplay(szBuffer);

		closesocket(m_ListenSock);
		m_ListenSock = INVALID_SOCKET;

		WSACleanup();
		return false;
	}

	// 크리티컬 섹션 초기화
	InitializeCriticalSection(&m_CS_SocketList);

	// 4. IOCP 생성
	m_hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
	assert(nullptr != m_hIOCP);

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// 5. WorkerThread() 들 동작
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// IOCP 의 쓰레드 풀은 서버를 기준으로 [코어수] ~ [코어수 * 2] 정도
	SYSTEM_INFO SystemInfo;
	GetSystemInfo(&SystemInfo);
	int iThreadCount = SystemInfo.dwNumberOfProcessors * 2;

	// IOCP 쓰레드를 iThreadCount 만큼 생성
	m_IOCPThread = new IOCPThread(this, m_hIOCP);
	m_IOCPThread->Create(CREATE_SUSPENDED, iThreadCount);

	printf_s("[TCP 서버] 시작\n");
	return true;
}

void ServerManager::Release()
{
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// WorkerThread() 종료 처리 및 대기
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// WorkerThread() 들의 종료를 위한 최초 신호
	PostQueuedCompletionStatus(m_hIOCP, 0, 0, nullptr);

	// 쓰레드 종료
	m_IOCPThread->Destroy();

	// IOCP 종료
	CloseHandle(m_hIOCP);
	m_hIOCP = nullptr;
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	for (auto it : m_SocketList)
	{
		delete it;
	}
	m_SocketList.clear();

	// 윈속 종료
	WSACleanup();

	// 크리티컬 섹션 종료
	DeleteCriticalSection(&m_CS_SocketList);

	printf_s("[TCP 서버] 종료\n");
}

int ServerManager::Run()
{
	while (!m_bExit)
	{
		// 6. WSAAccept()
		SOCKADDR_IN clientaddr;
		int addrlen = sizeof(clientaddr);
		ZeroMemory(&clientaddr, addrlen);
		SOCKET socket = WSAAccept(m_ListenSock, reinterpret_cast<SOCKADDR*>(&clientaddr), &addrlen, ServerManager::AcceptCondition, reinterpret_cast<DWORD_PTR>(&m_SocketList));

		if (INVALID_SOCKET == socket)
		{
			// 서버에서 연결 거부( AcceptCondition() )를 한 경우는 오류 출력을 안하게 함
			if (WSAGetLastError() != WSAECONNREFUSED)
			{
				char szBuffer[BUFSIZE] = { 0, };
				sprintf_s(szBuffer, "[TCP 서버] 에러 발생 -- WSAAccept() :");
				ErrManager::ErrDisplay(szBuffer);
			}

			continue;
		}

		// 클라이언트 구조체 생성 및 등록
		SSocket* psSocket = new SSocket;
		psSocket->socket = socket;
		psSocket->strIP = inet_ntoa(clientaddr.sin_addr);
		psSocket->usPort = ntohs(clientaddr.sin_port);

		// 7. 소켓을 IOCP 에 키 값과 함께 등록
		if (CreateIoCompletionPort(reinterpret_cast<HANDLE>(psSocket->socket), m_hIOCP, reinterpret_cast<ULONG_PTR>(psSocket), 0) != m_hIOCP)
		{
			delete psSocket;
			continue;
		}

		// 연결된 클라이언트 정보를 등록
		if (!AddSocket(psSocket))
		{
			delete psSocket;
			continue;
		}

		printf_s("[TCP 서버] [%15s:%5d] 클라이언트가 접속\n", psSocket->strIP.c_str(), psSocket->usPort);

		// 8. WSARecv() 걸기
		if (!Receive(psSocket))
		{
			// 클라이언트 종료
			RemoveSocket(psSocket->socket);
			continue;
		}
	}

	return 0;
}
