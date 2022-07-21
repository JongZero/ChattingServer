#include "IOCPThread.h"
#include "ServerManager.h"

IOCPThread::IOCPThread(ServerManager* server, HANDLE hIOCP)
	: m_ServerManager(server), m_hIOCP(hIOCP)
{

}

IOCPThread::~IOCPThread()
{

}

void IOCPThread::Begin(void)
{
	size_t nCount = Resume();
	assert(m_ThreadVec.size() == nCount);
}

void IOCPThread::End(void)
{
	// 종료 시 필요한 코드 넣기
	// 지금은 아직 없다.
}

unsigned int IOCPThread::Run(int dwIndex)
{
	assert(m_hIOCP != nullptr);

	// 쓰레드 종료 체크
	while (!m_bClose)
	{
		DWORD        dwNumberOfBytesTransferred = 0;
		SSocket*	 psSocket = nullptr;
		SOverlapped* psOverlapped = nullptr;

		// GetQueuedCompletionStatus() - GQCS 라고 부름
		// WSARead(), WSAWrite() 등의 Overlapped I/O 관련 처리 결과를 받아오는 함수
		// PostQueuedCompletionStatus() 를 통해서도 GQCS 를 리턴시킬 수 있다.( 일반적으로 쓰레드 종료 처리 )
		BOOL bSuccessed = GetQueuedCompletionStatus(m_hIOCP,												// IOCP 핸들
			&dwNumberOfBytesTransferred,						    // I/O 에 사용된 데이터의 크기
			reinterpret_cast<PULONG_PTR>(&psSocket),           // 소켓의 IOCP 등록시 넘겨준 키 값
																   // ( WSAAccept() 이 후, CreateIoCompletionPort() 시 )
			reinterpret_cast<LPOVERLAPPED*>(&psOverlapped),   // WSARead(), WSAWrite() 등에 사용된 WSAOVERLAPPED
			INFINITE);										    // 신호가 발생될 때까지 무제한 대기

		// 키가 nullptr 일 경우 쓰레드 종료를 의미
		if (nullptr == psSocket)
		{
			// 다른 WorkerThread() 들의 종료를 위해서
			PostQueuedCompletionStatus(m_hIOCP, 0, 0, nullptr);
			m_bClose = true;
			break;
		}

		assert(nullptr != psOverlapped);

		// 오버랩드 결과 체크
		if (!bSuccessed)
		{
			// 클라이언트 종료
			m_ServerManager->RemoveSocket(psOverlapped->socket);
			delete psOverlapped;
			continue;
		}

		// 연결 종료
		if (0 == dwNumberOfBytesTransferred)
		{
			// 클라이언트 종료
			m_ServerManager->RemoveSocket(psOverlapped->socket);
			delete psOverlapped;
			continue;
		}
		
		// Overlapped I/O 처리
		switch (psOverlapped->eIOType)
		{
		case SOverlapped::EIOType::EIOType_Recv: ProcessRecv(dwNumberOfBytesTransferred, psOverlapped, psSocket); break; // WSARecv() 의 Overlapped I/O 완료에 대한 처리
		case SOverlapped::EIOType::EIOType_Send: ProcessSend(dwNumberOfBytesTransferred, psOverlapped, psSocket); break; // WSASend() 의 Overlapped I/O 완료에 대한 처리
		}
	}
	return 0;
}

void IOCPThread::ProcessRecv(DWORD dwNumberOfBytesTransferred, SOverlapped* psOverlapped, SSocket* psSocket)
{
	printf_s("[TCP 서버] [%15s:%5d] 패킷 수신 완료 <- %d 바이트\n", psSocket->strIP.c_str(), psSocket->usPort, dwNumberOfBytesTransferred);

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
		// 클라이언트로부터 수신한 패킷
		const SHeader* const cpcHeader = reinterpret_cast<const SHeader* const>(psOverlapped->szBuffer);

		// 잘못된 패킷
		if (EC2S_TYPE_MAX <= cpcHeader->usType)
		{
			// 클라이언트 종료
			m_ServerManager->RemoveSocket(psOverlapped->socket);
			delete psOverlapped;
			return;
		}

		switch (cpcHeader->usType)
		{
		case EC2S_TYPE_MESSAGE:         // 채팅 메세지
		{
			const C2S_Message* const cpcPacket = static_cast<const C2S_Message* const>(cpcHeader);

			// S2C_Message 패킷 작성
			S2C_Message sPacket;
			GetLocalTime(&sPacket.sDate);                         // 시  각
			strcpy_s(sPacket.szIP, psSocket->strIP.c_str());      // 아이피
			sPacket.usPort = psSocket->usPort;                      // 포  트
			strcpy_s(sPacket.szMessage, cpcPacket->szMessage);    // 메시지

			// 브로드 캐스팅
			m_ServerManager->BroadcastPacket(&sPacket);
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

	{   // 임계 영역
		Lock _lock(&psSocket->CS_useCount);

		// 처리되었다면 카운트 감소
		psSocket->useCount--;
	}

	// 종료해야하는 소켓인지 확인
	if (psSocket->useCount == 0 && psSocket->checkClose)
	{
		delete psSocket;
		delete psOverlapped;
		return;
	}

	// 수신 걸기( 이번에 사용한 오버랩드를 다시 사용 )
	if (!m_ServerManager->Receive(psSocket, psOverlapped))
	{
		// 클라이언트 종료
		m_ServerManager->RemoveSocket(psOverlapped->socket);
		delete psOverlapped;
		return;
	}
}

void IOCPThread::ProcessSend(DWORD dwNumberOfBytesTransferred, SOverlapped* psOverlapped, SSocket* psSocket)
{
	printf_s("[TCP 서버] [%15s:%5d] 패킷 송신 완료 -> %d 바이트\n", psSocket->strIP.c_str(), psSocket->usPort, dwNumberOfBytesTransferred);

	{   // 임계 영역
		Lock _lock(&psSocket->CS_useCount);

		// 처리되었다면 카운트 감소
		psSocket->useCount--;
	}

	// 종료해야하는 소켓인지 확인
	if (psSocket->useCount == 0 && psSocket->checkClose)
	{
		delete psSocket;
	}

	delete psOverlapped;
}
