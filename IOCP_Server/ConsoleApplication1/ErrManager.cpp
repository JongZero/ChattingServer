#include "ErrManager.h"
#include <WinSock2.h>
#include <stdio.h>

void ErrManager::ErrDisplay(const char* const cpcMSG)
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
