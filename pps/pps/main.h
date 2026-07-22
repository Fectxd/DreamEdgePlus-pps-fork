#pragma once
#include "string"
#include "vector"
#include <windows.h>

std::string GetRunPath(); //获取程序运行目录

DWORD RunProcess(std::string proPath, int argc, char* argv[]); //启动指定程序，返回句柄

std::vector<DWORD> GetChildProcessIds(DWORD parentProcessId); //根据父进程ID获取子进程ID列表

DWORD FindProcessByName(const std::string& processName); //按进程名查找进程ID（全系统搜索）

std::vector<DWORD> FindAllProcessesByName(const std::string& processName); //按进程名查找所有匹配的进程ID

HWND FindWindowByProcessIdAndClassName(DWORD processId, const std::wstring& className); //根据进程id和窗口类名获取窗口id

HWND FindWindowByProcessIdAndClassNames(DWORD processId, const std::vector<std::wstring>& classNames); //根据进程id和多个类名查找窗口（仅顶层窗口）

HWND FindWindowByProcessIdAndClassNamesDeep(DWORD processId, const std::vector<std::wstring>& classNames); //深度搜索：先搜顶层窗口，再递归搜子窗口

void HideWindowTree(HWND hwnd, const std::vector<std::wstring>& cefClasses); //递归隐藏窗口及其CEF子窗口

void DisableWindow(HWND hwnd); //解除窗口禁用状态

void HideWindow(HWND hwnd); //隐藏指定窗口

std::string GetWindowTitle(HWND hwnd); //根据窗口hwnd获取窗口标题

std::string GetProcessName(DWORD processId); //获取进程名称

bool FileExists(const std::string& filename); //判断文件是否存在