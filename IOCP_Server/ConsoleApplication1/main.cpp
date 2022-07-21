#include "ServerManager.h"

int main()
{
	ServerManager server;
	server.Initialize();
	server.Run();
	server.Release();
}