#pragma once

/// <summary>
/// 에러 메시지를 출력해주는 클래스
/// 2021. 05. 10
/// 정종영
/// </summary>
class ErrManager
{
public:
	ErrManager() {};
	~ErrManager() {};

	// 소켓 함수 오류 출력
	static void ErrDisplay(const char* const cpcMSG);
};
