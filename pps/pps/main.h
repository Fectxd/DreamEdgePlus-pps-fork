#pragma once
#include "string"
#include "vector"
#include <windows.h>

std::string GetRunPath(); //��ȡ��������Ŀ¼

DWORD RunProcess(std::string proPath, int argc, char* argv[]); //����ָ�����򣬷��ؾ��

std::vector<DWORD> GetChildProcessIds(DWORD parentProcessId); //根据父进程ID获取子进程ID列表

HWND FindWindowByProcessIdAndClassName(DWORD processId, const std::wstring& className); //根据进程id和窗口类名获取窗口id

HWND FindWindowByProcessIdAndClassNames(DWORD processId, const std::vector<std::wstring>& classNames); //根据进程id和多个类名查找窗口

void DisableWindow(HWND hwnd); //禁止窗口停止状态

void HideWindow(HWND hwnd); //����ָ���Ĵ���

std::string GetWindowTitle(HWND hwnd); //���ݴ���hwnd��ȡ���ڱ���

std::string GetProcessName(DWORD processId); //��ȡ��������

bool FileExists(const std::string& filename); //�ж��ļ��Ƿ����