
#include <windows.h>
#include <iostream>
#include <tlhelp32.h>
#include <fstream>
#include <string.h>
#include "main.h"

int main(int argc, char* argv[])
{
    std::string psPath = GetRunPath() + "Photoshop.exe"; //ps程序所在根目录
    
    if (!FileExists(psPath)) {
        std::cerr << "没有检测到PS，请把程序放到PS根目录，每次使用此程序来启动PS。" << std::endl;
        std::cout << "也可以拖放图片到程序，把图片路径作为参数传给PS启动。" << std::endl;
        system("pause");
        return 0;
    }

    DWORD proId = RunProcess(psPath, argc, argv);

    int num = 4; //授权窗口最多处理次数，超过3次就不管了
    int sleepNum = 0; //累计时间，防止死循环

    // 兼容新旧两种授权窗口类型：
    //   旧版 IE 内嵌：顶层窗口 EmbeddedWB
    //   新版 CEF/Chromium：EmbeddedWB → CefBrowserWindow → Chrome_WidgetWin_0 → Chrome_RenderWidgetHostHWND
    //   CI: windows-2022 runner, v143 toolset, ARM64 cross-compile
    const std::vector<std::wstring> licenseWindowClasses = {
        L"EmbeddedWB",
        L"CefBrowserWindow",
        L"Chrome_WidgetWin_0",
        L"Chrome_RenderWidgetHostHWND"
    };
    // CEF 子窗口类名（在 EmbeddedWB 下递归隐藏用）
    const std::vector<std::wstring> cefChildClasses = {
        L"CefBrowserWindow",
        L"Chrome_WidgetWin_0",
        L"Chrome_WidgetWin_1",
        L"Chrome_WidgetWin_2",
        L"Chrome_RenderWidgetHostHWND"
    };

    while (true) {
        HWND psWindowId = FindWindowByProcessIdAndClassName(proId, L"Photoshop");
        if (psWindowId != nullptr) {
            // 方案1：先尝试子进程方式（兼容旧版）
            std::vector<DWORD> subPros = GetChildProcessIds(proId);
            bool foundLicensing = false;
            for (DWORD proItem : subPros) {
                if (GetProcessName(proItem) == "adobe_licensing_wf.exe") {
                    foundLicensing = true;
                    HWND wfWindowId = FindWindowByProcessIdAndClassNamesDeep(proItem, licenseWindowClasses);
                    if (wfWindowId != nullptr) {
                        if (num <= 0) {
                            goto GOOUT;
                        }
                        num--;
                        // 递归隐藏整个窗口树（兼容 CEF 新版）
                        HWND rootWnd = GetAncestor(wfWindowId, GA_ROOT);
                        HideWindowTree(rootWnd, cefChildClasses);
                        HideWindow(rootWnd);
                        DisableWindow(psWindowId);
                    }
                }
            }
            // 方案2：全局搜索 adobe_licensing_wf.exe（兼容新版进程树变更）
            if (!foundLicensing) {
                std::vector<DWORD> allLicProcs = FindAllProcessesByName("adobe_licensing_wf.exe");
                for (DWORD licPid : allLicProcs) {
                    HWND wfWindowId = FindWindowByProcessIdAndClassNamesDeep(licPid, licenseWindowClasses);
                    if (wfWindowId != nullptr) {
                        if (num <= 0) {
                            goto GOOUT;
                        }
                        num--;
                        HWND rootWnd = GetAncestor(wfWindowId, GA_ROOT);
                        HideWindowTree(rootWnd, cefChildClasses);
                        HideWindow(rootWnd);
                        DisableWindow(psWindowId);
                    }
                }
            }
        }
        Sleep(100); //等待PS启动完成

        if (sleepNum >= 1200) { //120秒还没出现授权窗口就退出
            return 0;
        }
        sleepNum++;
    }
    GOOUT:

    //std::cout << FindWindowByProcessIdAndClassName(proId, L"Photoshop");
    //std::cout << FindWindowByProcessIdAndClassName(15448, L"abc");
    //DisableWindow(FindWindowByProcessIdAndClassName(23312, L"Photoshop"));
    //HideWindow(FindWindowByProcessIdAndClassName(18092, L"EmbeddedWB"));
    return 0;
}


//��ȡ��������Ŀ¼
std::string GetRunPath()
{
    wchar_t buffer[MAX_PATH];
    GetModuleFileNameW(NULL, buffer, MAX_PATH); // ʹ��GetModuleFileNameW()��ȡ��ǰ��ִ���ļ�������·��
    std::wstring fullPath(buffer);
    std::wstring::size_type pos = fullPath.find_last_of(L"\\/");
    if (pos != std::wstring::npos)
    {
        // ��ȡ��·���е�Ŀ¼����
        std::wstring directory = fullPath.substr(0, pos + 1);
        return std::string(directory.begin(), directory.end());
    }
    return "";
}

//����ָ�����򣬷��ؾ��
DWORD RunProcess(std::string proPath, int argc, char* argv[])
{
    // �������̵Ľṹ��
    STARTUPINFOA startupInfo = { 0 };
    startupInfo.cb = sizeof(STARTUPINFOA);
    PROCESS_INFORMATION processInfo = { 0 };

    // ���������в����ַ���
    std::string commandLine;
    for (int i = 0; i < argc; ++i)
    {
        commandLine += argv[i];
        commandLine += " ";
    }


    // ��������
    if (!CreateProcessA(const_cast<LPSTR>(proPath.c_str()), const_cast<LPSTR>(commandLine.c_str()), nullptr,
        nullptr, FALSE, 0, nullptr, nullptr, &startupInfo, &processInfo))
    {
        return 0;
    }

    // ��ȡ���� ID
    DWORD processId = GetProcessId(processInfo.hProcess);

    // �ر����õľ��
    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);

    return processId;
}

//���ݽ���ID��ȡ�ӽ���id��
std::vector<DWORD> GetChildProcessIds(DWORD parentProcessId)
{
    std::vector<DWORD> childProcessIds;
    PROCESSENTRY32 processEntry;
    processEntry.dwSize = sizeof(PROCESSENTRY32);

    HANDLE processSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (processSnapshot == INVALID_HANDLE_VALUE)
    {
        return childProcessIds;
    }

    if (!Process32First(processSnapshot, &processEntry))
    {
        CloseHandle(processSnapshot);
        return childProcessIds;
    }

    do
    {
        if (processEntry.th32ParentProcessID == parentProcessId)
        {
            childProcessIds.push_back(processEntry.th32ProcessID);
        }
    } while (Process32Next(processSnapshot, &processEntry));

    CloseHandle(processSnapshot);
    return childProcessIds;
}


//���ݽ���id�봰��������ȡ����id
HWND FindWindowByProcessIdAndClassName(DWORD processId, const std::wstring& className)
{
    HWND windowHandle = NULL;

    // ö�ٽ��������еĶ�������
    do
    {
        windowHandle = FindWindowExW(NULL, windowHandle, className.c_str(), NULL);

        // ����ҵ����ڣ��������Ľ��� ID �Ƿ���ָ���Ľ��� ID ƥ��
        if (windowHandle != NULL)
        {
            DWORD windowProcessId = 0;
            GetWindowThreadProcessId(windowHandle, &windowProcessId);
            if (windowProcessId == processId)
            {
                return windowHandle;  // �ҵ�ƥ��Ĵ���
            }
        }
    } while (windowHandle != NULL);

    return nullptr;  // û���ҵ�ƥ��Ĵ���
}

struct FindWindowEnumData {
    DWORD processId;
    const std::vector<std::wstring>* classNames;
    HWND result;
};

BOOL CALLBACK FindWindowByProcessIdAndClassNamesCallback(HWND hwnd, LPARAM lParam)
{
    FindWindowEnumData* data = reinterpret_cast<FindWindowEnumData*>(lParam);
    DWORD windowProcessId = 0;
    GetWindowThreadProcessId(hwnd, &windowProcessId);
    if (windowProcessId != data->processId)
    {
        return TRUE;
    }

    wchar_t className[256] = { 0 };
    if (GetClassNameW(hwnd, className, _countof(className)) > 0)
    {
        std::wstring currentClass(className);
        for (const auto& targetClass : *data->classNames)
        {
            if (_wcsicmp(currentClass.c_str(), targetClass.c_str()) == 0)
            {
                data->result = hwnd;
                return FALSE;
            }
        }
    }

    return TRUE;
}

HWND FindWindowByProcessIdAndClassNames(DWORD processId, const std::vector<std::wstring>& classNames)
{
    FindWindowEnumData data;
    data.processId = processId;
    data.classNames = &classNames;
    data.result = NULL;

    EnumWindows(FindWindowByProcessIdAndClassNamesCallback, reinterpret_cast<LPARAM>(&data));
    return data.result;
}


//解除窗口禁用状态（使用标准 EnableWindow API，确保子窗口也恢复）
void DisableWindow(HWND hwnd)
{
    if (!IsWindowEnabled(hwnd)) {
        EnableWindow(hwnd, TRUE);
    }
}

//����ָ���Ĵ���
void HideWindow(HWND hwnd)
{
    ShowWindow(hwnd, SW_HIDE);
}

//���ݴ���hwnd��ȡ���ڱ���
std::string GetWindowTitle(HWND hwnd)
{
    int titleLength = GetWindowTextLengthW(hwnd);
    if (titleLength == 0)
    {
        return "";
    }

    wchar_t* titleBuffer = new wchar_t[titleLength + 1];
    GetWindowTextW(hwnd, titleBuffer, titleLength + 1);

    std::wstring windowTitleW(titleBuffer);

    delete[] titleBuffer;

    std::string windowTitle(windowTitleW.begin(), windowTitleW.end());

    return windowTitle;
}


//��ȡ������
std::string GetProcessName(DWORD processId)
{
    std::string processName;
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (hProcess != NULL)
    {
        wchar_t buffer[MAX_PATH];
        DWORD bufferSize = sizeof(buffer) / sizeof(buffer[0]);
        if (QueryFullProcessImageNameW(hProcess, 0, buffer, &bufferSize) != 0)
        {
            std::wstring imageName(buffer);
            size_t position = imageName.find_last_of(L"\\");
            if (position != std::wstring::npos)
            {
                processName = std::string(imageName.begin() + position + 1, imageName.end());
            }
        }
        CloseHandle(hProcess);
    }
    return processName;
}


//判断文件是否存在
bool FileExists(const std::string& filename)
{
    std::ifstream infile(filename);
    return infile.good();
}


// === 新增：全系统按进程名查找进程ID ===

// 按进程名查找单个进程ID（返回第一个匹配的）
DWORD FindProcessByName(const std::string& processName)
{
    std::vector<DWORD> all = FindAllProcessesByName(processName);
    return all.empty() ? 0 : all[0];
}

// 按进程名查找所有匹配的进程ID（全系统搜索，不限父子关系）
std::vector<DWORD> FindAllProcessesByName(const std::string& processName)
{
    std::vector<DWORD> result;
    PROCESSENTRY32 processEntry;
    processEntry.dwSize = sizeof(PROCESSENTRY32);

    HANDLE processSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (processSnapshot == INVALID_HANDLE_VALUE) {
        return result;
    }

    if (!Process32First(processSnapshot, &processEntry)) {
        CloseHandle(processSnapshot);
        return result;
    }

    do {
        std::string curName = GetProcessName(processEntry.th32ProcessID);
        if (!curName.empty()) {
            // 不区分大小写比较
            if (_stricmp(curName.c_str(), processName.c_str()) == 0) {
                result.push_back(processEntry.th32ProcessID);
            }
        }
    } while (Process32Next(processSnapshot, &processEntry));

    CloseHandle(processSnapshot);
    return result;
}


// === 新增：深度窗口搜索（递归搜索子窗口，兼容 CEF 层级） ===

// 用于 FindWindowByProcessIdAndClassNamesDeep 的顶层枚举回调数据
struct FindWindowDeepTopData {
    DWORD pid;
    const std::vector<std::wstring>* classes;
    HWND found;
};

// 顶层窗口枚举回调（__stdcall，EnumWindows 要求）
BOOL CALLBACK FindWindowDeepTopCallback(HWND hwnd, LPARAM lParam)
{
    FindWindowDeepTopData* td = reinterpret_cast<FindWindowDeepTopData*>(lParam);
    DWORD wpid = 0;
    GetWindowThreadProcessId(hwnd, &wpid);
    if (wpid != td->pid) return TRUE;

    // 使用 FindWindowEx 迭代搜索该顶层窗口的所有子孙窗口
    // 简单的栈遍历窗口树
    HWND stack[128];
    int stackSize = 0;
    stack[stackSize++] = hwnd;

    while (stackSize > 0) {
        HWND cur = stack[--stackSize];

        // 检查当前窗口类名
        wchar_t className[256] = { 0 };
        if (GetClassNameW(cur, className, _countof(className)) > 0) {
            std::wstring curClass(className);
            for (const auto& tc : *td->classes) {
                if (_wcsicmp(curClass.c_str(), tc.c_str()) == 0) {
                    td->found = cur;
                    return FALSE; // 找到，停止枚举
                }
            }
        }

        // 将子窗口入栈
        HWND child = FindWindowExW(cur, NULL, NULL, NULL);
        while (child != NULL && stackSize < 128) {
            stack[stackSize++] = child;
            child = FindWindowExW(cur, child, NULL, NULL);
        }
    }
    return TRUE;
}

// 深度搜索：先搜顶层窗口，再用 FindWindowEx 迭代遍历子窗口树
// 兼容新版 CEF（CefBrowserWindow/Chrome_WidgetWin 是子窗口，EnumWindows 搜不到）
// 使用栈式迭代避免 EnumChildWindows 重入问题
HWND FindWindowByProcessIdAndClassNamesDeep(DWORD processId, const std::vector<std::wstring>& classNames)
{
    // 第一步：先尝试顶层窗口（最快路径，兼容旧版）
    HWND topResult = FindWindowByProcessIdAndClassNames(processId, classNames);
    if (topResult != NULL) {
        return topResult;
    }

    // 第二步：枚举所有顶层窗口，对每个匹配进程的顶层窗口，迭代搜索其子窗口树
    FindWindowDeepTopData topData;
    topData.pid = processId;
    topData.classes = &classNames;
    topData.found = NULL;

    EnumWindows(FindWindowDeepTopCallback, reinterpret_cast<LPARAM>(&topData));

    return topData.found;
}


// === 新增：递归隐藏 CEF 子窗口树 ===

// 使用栈式迭代遍历窗口树，隐藏所有匹配 CEF 类名的子窗口
void HideWindowTree(HWND hwnd, const std::vector<std::wstring>& cefClasses)
{
    if (hwnd == NULL) return;

    // 使用栈进行非递归的广度优先遍历
    HWND stack[256];
    int stackSize = 0;
    stack[stackSize++] = hwnd;

    while (stackSize > 0) {
        HWND cur = stack[--stackSize];

        // 检查当前窗口是否为 CEF 类，是则隐藏
        wchar_t className[256] = { 0 };
        if (GetClassNameW(cur, className, _countof(className)) > 0) {
            std::wstring curClass(className);
            for (const auto& cefClass : cefClasses) {
                if (_wcsicmp(curClass.c_str(), cefClass.c_str()) == 0) {
                    ShowWindow(cur, SW_HIDE);
                    break;
                }
            }
        }

        // 将子窗口入栈
        HWND child = FindWindowExW(cur, NULL, NULL, NULL);
        while (child != NULL && stackSize < 256) {
            stack[stackSize++] = child;
            child = FindWindowExW(cur, child, NULL, NULL);
        }
    }
}
