// 최신 VC++ 컴파일러에서 경고 및 오류 방지
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <WinSock2.h>
#include <process.h>
#include <assert.h>
#include <signal.h>
#include <string>
#include <vector>
#include <list>
#include <iostream>

// 패킷 헤더
#include "../Packet.h"

// 윈속2 라이브러리
#pragma comment( lib, "ws2_32" )

const unsigned short    BUFSIZE = 512;  // 송/수신 버퍼 크기
const unsigned short    PORT = 9000;	// connect 에 사용할 포트
const std::string       IP = "127.0.0.1";

// 소켓 함수 오류 출력
void err_display(const char* const cpcMSG)
{
	LPVOID lpMsgBuf = nullptr;

	FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
		nullptr,
		WSAGetLastError(),
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		reinterpret_cast<LPSTR>(&lpMsgBuf),
		0,
		nullptr);

	printf_s("%s %s", cpcMSG, reinterpret_cast<LPSTR>(lpMsgBuf));

	LocalFree(lpMsgBuf);
}

struct SServer
{
	SServer(void)
	{
		socket = INVALID_SOCKET;
		bConnected = FALSE;
	}

	~SServer(void)
	{
		// 소켓 정리
		if (INVALID_SOCKET != socket)
		{
			shutdown(socket, SD_BOTH);
			closesocket(socket);
		}
	}

	SOCKET          socket;                 // 서버와 연결된 소켓
	BOOL            bConnected;            // 서버와 연결이 되어있는지 여부
};

struct SOverlapped
{
	SOverlapped(void)
	{
		ZeroMemory(&wsaOverlapped, sizeof(wsaOverlapped));

		socket = INVALID_SOCKET;
		ZeroMemory(szBuffer, sizeof(szBuffer));
		iDataSize = 0;
	}

	// Overlapped I/O 작업의 종류
	enum class EIOType
	{
		EIOType_Recv,
		EIOType_Send
	};

	WSAOVERLAPPED   wsaOverlapped;      // Overlapped I/O 에 사용될 구조체
	EIOType         eIOType;            // 나중에 처리 결과를 통보 받았을때, WSARecv() 에 대한 처리였는지, WSASend() 에 대한 처리였는지 구분하기 위한 용도

	SOCKET          socket;             // 이 오버랩드의 대상 소켓
	char            szBuffer[BUFSIZE];  // 버퍼( EIOType_Recv 시 수신 버퍼, EIOType_Send 시 송신 버퍼 )
	int             iDataSize;          // 데이터량( EIOType_Recv 시 누적된 처리해야할 데이터량, EIOType_Send 시 송신 데이터량 )
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// 전역 변수들
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
SServer*				g_Server = nullptr;

HANDLE                  g_hIOCP = nullptr;          // Input/Output Completion Port( 입출력 완료 포트, 내부적으로 Queue 를 생성 ) 핸들
BOOL                    g_bExit = FALSE;            // 주 쓰레드 종료 플래그
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// 전역 함수들
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
BOOL Receive(SOCKET socket, SOverlapped* psOverlapped = nullptr);                                 // 클라이언트로부터 패킷 수신 오버랩드 걸기
BOOL SendPacket(SOCKET socket, SHeader* psPacket);                                                // 클라이언트에게 패킷 전송 오버랩드 걸기

void ProcessRecv(DWORD dwNumberOfBytesTransferred, SOverlapped* psOverlapped); // 수신 결과 처리
void ProcessSend(DWORD dwNumberOfBytesTransferred, SOverlapped* psOverlapped); // 송신 결과 처리
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// 작업 쓰레드
unsigned int __stdcall WorkerThread(void* pARG)
{
	assert(nullptr == pARG);
	assert(nullptr != g_hIOCP);

	while (TRUE)
	{
		DWORD        dwNumberOfBytesTransferred = 0;
		SServer*	 psServer = nullptr;
		SOverlapped* psOverlapped = nullptr;

		// GetQueuedCompletionStatus() - GQCS 라고 부름
		// WSARead(), WSAWrite() 등의 Overlapped I/O 관련 처리 결과를 받아오는 함수
		// PostQueuedCompletionStatus() 를 통해서도 GQCS 를 리턴시킬 수 있다.( 일반적으로 쓰레드 종료 처리 )
		BOOL bSuccessed = GetQueuedCompletionStatus(g_hIOCP,												// IOCP 핸들
			&dwNumberOfBytesTransferred,						    // I/O 에 사용된 데이터의 크기
			reinterpret_cast<PULONG_PTR>(&psServer),           // 소켓의 IOCP 등록시 넘겨준 키 값
																   // ( WSAAccept() 이 후, CreateIoCompletionPort() 시 )
			reinterpret_cast<LPOVERLAPPED*>(&psOverlapped),   // WSARead(), WSAWrite() 등에 사용된 WSAOVERLAPPED
			INFINITE);										    // 신호가 발생될 때까지 무제한 대기

		// 키가 nullptr 일 경우 쓰레드 종료를 의미
		if (nullptr == psServer)
		{
			// 다른 WorkerThread() 들의 종료를 위해서
			PostQueuedCompletionStatus(g_hIOCP, 0, 0, nullptr);
			break;
		}

		assert(nullptr != psOverlapped);

		// 오버랩드 결과 체크
		if (!bSuccessed)
		{
			// 오버랩드 제거
			delete psOverlapped;
			continue;
		}

		// 연결 종료
		if (0 == dwNumberOfBytesTransferred)
		{
			// 오버랩드 제거
			delete psOverlapped;
			continue;
		}

		// Overlapped I/O 처리
		switch (psOverlapped->eIOType)
		{
		case SOverlapped::EIOType::EIOType_Recv: ProcessRecv(dwNumberOfBytesTransferred, psOverlapped); break; // WSARecv() 의 Overlapped I/O 완료에 대한 처리
		case SOverlapped::EIOType::EIOType_Send: ProcessSend(dwNumberOfBytesTransferred, psOverlapped); break; // WSASend() 의 Overlapped I/O 완료에 대한 처리
		}
	}

	return 0;
}

void ProcessRecv(DWORD dwNumberOfBytesTransferred, SOverlapped* psOverlapped)
{
	printf_s("[TCP 클라이언트] 패킷 수신 완료 <- %d 바이트\n", dwNumberOfBytesTransferred);

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// 패킷 처리
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// 수신한 데이터들 크기를 누적 시켜준다.
	psOverlapped->iDataSize += dwNumberOfBytesTransferred;

	// 처리할 데이터가 있으면 처리
	while (psOverlapped->iDataSize > 0)
	{
		// header 크기는 2 바이트( 고정 )
		static const unsigned short cusHeaderSize = 2;

		// header 를 다 받지 못했다. 이어서 recv()
		if (cusHeaderSize > psOverlapped->iDataSize)
		{
			break;
		}

		// body 의 크기는 N 바이트( 가변 ), 패킷에 담겨있음
		unsigned short usBodySize = *reinterpret_cast<unsigned short*>(psOverlapped->szBuffer);
		unsigned short usPacketSize = cusHeaderSize + usBodySize;

		// 하나의 패킷을 다 받지 못했다. 이어서 recv()
		if (usPacketSize > psOverlapped->iDataSize)
		{
			break;
		}

		/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// 완성된 패킷을 처리
		/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
		// 서버로부터 수신한 패킷
		const SHeader* const cpcHeader = reinterpret_cast<const SHeader* const>(psOverlapped->szBuffer);

		// 잘못된 패킷
		if (ES2C_TYPE_MAX <= cpcHeader->usType)
		{
			// 클라이언트 종료
			delete psOverlapped;
			return;
		}
		
		switch (cpcHeader->usType)
		{
		case ES2C_TYPE_MESSAGE:         // 채팅 메세지
		{
			const S2C_Message* const packet = static_cast<const S2C_Message* const>(cpcHeader);

			// 서버로부터 받은 메시지 출력
			printf_s("[TCP 클라이언트] [%04d-%02d-%02d %02d:%02d:%02d] [%15s:%5d] 님의 말 : %s\n", packet->sDate.wYear, packet->sDate.wMonth, packet->sDate.wDay,
				packet->sDate.wHour, packet->sDate.wMinute, packet->sDate.wSecond,
				packet->szIP, packet->usPort,
				packet->szMessage);
		}
		break;
		}
		/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

		// 데이터들을 이번에 처리한만큼 당긴다.
		memcpy_s(psOverlapped->szBuffer, psOverlapped->iDataSize,
			psOverlapped->szBuffer + usPacketSize, psOverlapped->iDataSize - usPacketSize);

		// 처리한 패킷 크기만큼 처리할량 감소
		psOverlapped->iDataSize -= usPacketSize;
	}
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	// 수신 걸기( 이번에 사용한 오버랩드를 다시 사용 )
	if (!Receive(psOverlapped->socket, psOverlapped))
	{
		shutdown(g_Server->socket, SD_BOTH);
		closesocket(g_Server->socket);
		g_Server->bConnected = FALSE;
		
		printf_s("[TCP 클라이언트] 서버와 연결이 끊겼습니다. 엔터를 누르시면 연결을 시도합니다.\n");

		// 오버랩드 제거
		delete psOverlapped;
		return;
	}
}

void ProcessSend(DWORD dwNumberOfBytesTransferred, SOverlapped* psOverlapped)
{
	printf_s("[TCP 클라이언트] 패킷 송신 완료 -> %d 바이트\n", dwNumberOfBytesTransferred);

	delete psOverlapped;
}

void SignalFunction(int iSignalNumber)
{
	if (SIGINT == iSignalNumber)
	{
		// 종료 플래그 켬
		g_bExit = TRUE;
	}
}

int main(void)
{
	// Ctrl - C 를 누르면,
	// 주 쓰레드를 포함한 모든 쓰레드들이 정상 종료를 할 수 있도록 함
	signal(SIGINT, SignalFunction);

	char szBuffer[BUFSIZE] = { 0, };

	// 윈속 초기화
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
	{
		sprintf_s(szBuffer, "[TCP 클라이언트] 에러 발생 -- WSAStartup() :");
		err_display(szBuffer);

		return -1;
	}
	
	// 0. IOCP 생성
	g_hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
	assert(nullptr != g_hIOCP);

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// 1. WorkerThread() 들 동작
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// IOCP 의 쓰레드 풀은 서버를 기준으로 [코어수] ~ [코어수 * 2] 정도
	// 클라이언트도 이 정도 숫자의 쓰레드가 필요한지?
	SYSTEM_INFO SystemInfo;
	GetSystemInfo(&SystemInfo);
	int iThreadCount = SystemInfo.dwNumberOfProcessors * 2;
	
	// WorkerThread() 를 iThreadCount 만큼 생성
	std::vector< HANDLE > vecWorkerThreads;
	for (int i = 0; i < iThreadCount; ++i)
	{
		unsigned int uiThreadID;
		HANDLE hHandle = reinterpret_cast<HANDLE>(_beginthreadex(nullptr, 0, &WorkerThread, nullptr, 0, &uiThreadID));
		assert(nullptr != hHandle);
		vecWorkerThreads.push_back(hHandle);
	}
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	printf_s("[TCP 클라이언트] 시작\n");

	g_Server = new SServer;

	while (!g_bExit)
	{
		// 2. WSASocket()
		g_Server->socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);

		if (INVALID_SOCKET == g_Server->socket)
		{
			sprintf_s(szBuffer, "[TCP 클라이언트] 에러 발생 -- WSASocket() :");
			err_display(szBuffer);

			WSACleanup();
			return -1;
		}

		// 3. connect()
		while (!g_bExit)
		{
			SOCKADDR_IN serveraddr;
			ZeroMemory(&serveraddr, sizeof(serveraddr));
			serveraddr.sin_family = AF_INET;
			serveraddr.sin_addr.s_addr = inet_addr(IP.c_str());
			serveraddr.sin_port = htons(PORT);
			if (SOCKET_ERROR == connect(g_Server->socket, reinterpret_cast<SOCKADDR*>(&serveraddr), sizeof(serveraddr)))
			{
				if (WSAGetLastError() == WSAECONNREFUSED)
				{
					printf_s("[TCP 클라이언트] 서버 연결 재시도\n");
					continue;
				}

				sprintf_s(szBuffer, "[TCP 클라이언트] 에러 발생 -- connect() :");
				err_display(szBuffer);

				closesocket(g_Server->socket);
				g_Server->socket = INVALID_SOCKET;
			}

			break;
		}

		// 통신 진행 체크
		if ((g_bExit) || (INVALID_SOCKET == g_Server->socket))
		{
			continue;
		}

		g_Server->bConnected = TRUE;
		printf_s("[TCP 클라이언트] 서버와 연결되었습니다.\n");

		// 4. 소켓을 IOCP 에 키 값과 함께 등록
		if (CreateIoCompletionPort(reinterpret_cast<HANDLE>(g_Server->socket), g_hIOCP, reinterpret_cast<ULONG_PTR>(g_Server), 0) != g_hIOCP)
		{
			shutdown(g_Server->socket, SD_BOTH);
			closesocket(g_Server->socket);
			g_Server->socket = INVALID_SOCKET;
			g_Server->bConnected = FALSE;
			continue;
		}

		// 5. WSARecv() 걸기
		if (!Receive(g_Server->socket))
		{
			// 클라이언트 종료
			continue;
		}

		// 6. 패킷 송신 로직 시작
		while (!g_bExit)
		{
			std::string strBuffer;
			std::getline(std::cin, strBuffer);

			// Ctrl - C 종료
			if (std::cin.eof())
			{
				Sleep(1000);
				break;
			}

			// 소켓이 연결되지 않았으면, 연결 시도
			if (!g_Server->bConnected)
			{
				g_Server->socket = INVALID_SOCKET;
				break;
			}

			// 입력된 메시지가 없음
			if (strBuffer.empty())
			{
				printf_s("입력된 메시지가 없습니다.\n");
				continue;
			}
			// 입력된 메시지가 너무 김
			else if (strBuffer.size() > 127)
			{
				printf_s("메시지는 127 바이트 이하로만 입력 가능합니다.\n");
				continue;
			}

			C2S_Message sPacket;
			// C2S_Message 패킷 작성 및 전송
			strcpy_s(sPacket.szMessage, strBuffer.c_str());   // 메시지

			if (!SendPacket(g_Server->socket, &sPacket))
			{
				shutdown(g_Server->socket, SD_BOTH);
				closesocket(g_Server->socket);
				g_Server->socket = INVALID_SOCKET;
				g_Server->bConnected = FALSE;

				printf_s("[TCP 클라이언트] 서버와 연결 종료\n");
				break;
			}
		}
	}

	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// WorkerThread() 종료 처리 및 대기
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	// WorkerThread() 들의 종료를 위한 최초 신호
	PostQueuedCompletionStatus(g_hIOCP, 0, 0, nullptr);

	size_t nWorkerThreadCount = vecWorkerThreads.size();
	for (size_t i = 0; i < nWorkerThreadCount; ++i)
	{
		WaitForSingleObject(vecWorkerThreads[i], INFINITE);
		CloseHandle(vecWorkerThreads[i]);
	}
	vecWorkerThreads.clear();

	// IOCP 종료
	CloseHandle(g_hIOCP);
	g_hIOCP = nullptr;
	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

	delete g_Server;
	g_Server = nullptr;

	// 윈속 종료
	WSACleanup();

	printf_s("[TCP 클라이언트] 종료\n");

	return 0;
}

BOOL Receive(SOCKET socket, SOverlapped* psOverlapped)
{
	assert(INVALID_SOCKET != socket);

	// 사용할 오버랩드를 받지 않았으면 생성
	if (nullptr == psOverlapped)
	{
		psOverlapped = new SOverlapped;
	}

	// 오버랩드 셋팅
	psOverlapped->eIOType = SOverlapped::EIOType::EIOType_Recv;
	psOverlapped->socket = socket;

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
		sprintf_s(szBuffer, "[TCP 클라이언트] 에러 발생 -- WSARecv() :");
		err_display(szBuffer);

		delete psOverlapped;
		return FALSE;
	}

	return TRUE;
}

BOOL SendPacket(SOCKET socket, SHeader* psPacket)
{
	assert(INVALID_SOCKET != socket);
	assert(nullptr != psPacket);

	// 등록되지 않은 패킷은 전송할 수 없다.
	// [클라] -> [서버]
	if (EC2S_TYPE_MAX <= psPacket->usType)
	{
		return FALSE;
	}

	// 오버랩드 셋팅
	SOverlapped* psOverlapped = new SOverlapped;
	psOverlapped->eIOType = SOverlapped::EIOType::EIOType_Send;
	psOverlapped->socket = socket;

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
		sprintf_s(szBuffer, "[TCP 클라이언트] 에러 발생 -- WSASend() :");
		err_display(szBuffer);

		delete psOverlapped;
		return FALSE;
	}

	return TRUE;
}