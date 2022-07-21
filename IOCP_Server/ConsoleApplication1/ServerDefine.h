#pragma once

// 최신 VC++ 컴파일러에서 경고 및 오류 방지
#define _WINSOCK_DEPRECATED_NO_WARNINGS

#include <WinSock2.h>
#include <string>
#include <vector>
#include <list>
#include <assert.h>
#include <process.h>
#include "Lock.h"
#include "../Packet.h"

// 윈속2 라이브러리
#pragma comment( lib, "ws2_32" )

const unsigned short    BUFSIZE = 512;			// 송/수신 버퍼 크기
const unsigned short    PORT = 9000;			// listen 에 사용할 포트
const unsigned short    MAX_USER_COUNT = 0;		// 서버측에서 수용할 최대 소켓수( 0 으로 셋팅하면 접속 제한을 하지 않게 함 )

struct SSocket
{
	SSocket(void)
	{
		socket = INVALID_SOCKET;
		usPort = 0;
		useCount = 0;
		checkClose = false;

		// 크리티컬 섹션 초기화
		InitializeCriticalSection(&CS_useCount);
	}

	~SSocket(void)
	{
		// 소켓 정리
		if (INVALID_SOCKET != socket)
		{
			closesocket(socket);
		}

		// 크리티컬 섹션 종료
		DeleteCriticalSection(&CS_useCount);
	}

	SOCKET              socket;         // 연결된 소  켓
	std::string         strIP;          // 원격지 아이피
	unsigned short      usPort;         // 원격지 포  트
	int                 useCount;       // 소켓 사용 카운트 ( Send, Recv 정상 호출 시 증가. 완료 처리시 감소 )
	CRITICAL_SECTION    CS_useCount;    // 카운트를 증감할 때 사용할 크리티컬 섹션
	bool                checkClose;     // 소켓을 닫아야하는지 여부 ( 완료 처리 시점에서 검사한다. )
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