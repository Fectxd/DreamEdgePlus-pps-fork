
#include <windows.h>
#include <shellapi.h>
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

    // 如果没有传入文件，创建临时空白图片让PS进入编辑模式
    std::string tempFile;
    if (argc <= 1) {
        tempFile = CreateTempPng();
    }

    DWORD proId = 0;
    if (!tempFile.empty()) {
        // 方式1：ShellExecute图片文件，通过系统文件关联启动PS（与双击文件一致）
        proId = RunProcessViaFile(tempFile);
    }
    if (proId == 0) {
        // 方式2：直接ShellExecute Photoshop.exe（兜底）
        proId = RunProcess(psPath, argc, argv);
    }

    if (proId == 0) {
        std::cerr << "无法启动 Photoshop。" << std::endl;
        return 1;
    }

    int suppressCount = 0;         // 已隐藏次数
    const int maxSuppress = 10;    // 最多隐藏10次
    int elapsed = 0;               // 累计时间 (x100ms)

    // 兼容新旧两种授权窗口类型
    const std::vector<std::wstring> licenseWindowClasses = {
        L"EmbeddedWB",
        L"CefBrowserWindow",
        L"Chrome_WidgetWin_0",
        L"Chrome_RenderWidgetHostHWND"
    };
    // CEF 子窗口类名
    const std::vector<std::wstring> cefChildClasses = {
        L"CefBrowserWindow",
        L"Chrome_WidgetWin_0",
        L"Chrome_WidgetWin_1",
        L"Chrome_WidgetWin_2",
        L"Chrome_RenderWidgetHostHWND"
    };

    // === 阶段1：等待PS启动并抑制授权窗口 ===
    while (elapsed < 1200) { // 最多等120秒
        // 检查PS是否还在运行
        HANDLE hPS = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, proId);
        if (hPS == NULL) {
            return 0; // PS已退出
        }
        CloseHandle(hPS);

        // 查找并隐藏授权窗口
        DWORD licPid = 0;
        bool foundLicense = FindLicenseProcess(proId, licPid);
        
        if (foundLicense) {
            HWND wfWindowId = FindWindowByProcessIdAndClassNamesDeep(licPid, licenseWindowClasses);
            if (wfWindowId != nullptr && suppressCount < maxSuppress) {
                suppressCount++;
                HWND rootWnd = GetAncestor(wfWindowId, GA_ROOT);
                HideWindowTree(rootWnd, cefChildClasses);
                HideWindow(rootWnd);
            }
        }

        // 始终确保PS窗口处于启用状态
        HWND psWnd = FindWindowByProcessIdAndClassName(proId, L"Photoshop");
        if (psWnd != nullptr) {
            DisableWindow(psWnd);
        }

        Sleep(100);
        elapsed++;
    }

    // === 阶段2：后台守护（PS运行期间持续监控） ===
    while (true) {
        // PS还在吗？
        HANDLE hPS = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, proId);
        if (hPS == NULL) {
            return 0; // PS已退出，我们也退出
        }
        CloseHandle(hPS);

        // 隐藏新出现的授权窗口
        DWORD licPid = 0;
        if (FindLicenseProcess(proId, licPid)) {
            HWND wfWindowId = FindWindowByProcessIdAndClassNamesDeep(licPid, licenseWindowClasses);
            if (wfWindowId != nullptr) {
                HWND rootWnd = GetAncestor(wfWindowId, GA_ROOT);
                HideWindowTree(rootWnd, cefChildClasses);
                HideWindow(rootWnd);
            }
        }

        // 持续确保PS的所有窗口都处于启用状态
        EnableAllProcessWindows(proId);

        Sleep(2000); // 每2秒检查一次，降低CPU占用
    }
    return 0;
}

// 创建临时1x1透明PNG，用于无文件参数时让PS进入编辑模式
std::string CreateTempPng()
{
    // 1x1 透明 PNG (68 bytes) - Python生成，CRC校验正确
    static const unsigned char pngData[] = {
        0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A,0x00,0x00,0x00,0x0D,0x49,0x48,0x44,0x52,
        0x00,0x00,0x00,0x01,0x00,0x00,0x00,0x01,0x08,0x06,0x00,0x00,0x00,0x1F,0x15,0xC4,0x89,
        0x00,0x00,0x00,0x0B,0x49,0x44,0x41,0x54,0x78,0x9C,0x63,0x60,0x00,0x02,0x00,0x00,0x05,
        0x00,0x01,0x7A,0x5E,0xAB,0x3F,0x00,0x00,0x00,0x00,0x49,0x45,0x4E,0x44,0xAE,0x42,0x60,
        0x82
    };

    wchar_t tempPath[MAX_PATH];
    wchar_t tempFile[MAX_PATH];
    if (!GetTempPathW(MAX_PATH, tempPath)) return "";
    if (!GetTempFileNameW(tempPath, L"pps", 0, tempFile)) return "";

    // 替换扩展名为 .png
    std::wstring wFilePath(tempFile);
    size_t dot = wFilePath.find_last_of(L'.');
    if (dot != std::wstring::npos) {
        wFilePath = wFilePath.substr(0, dot);
    }
    wFilePath += L".png";

    HANDLE hFile = CreateFileW(wFilePath.c_str(), GENERIC_WRITE, 0, NULL,
        CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return "";

    DWORD written;
    WriteFile(hFile, pngData, sizeof(pngData), &written, NULL);
    CloseHandle(hFile);

    std::string result(wFilePath.begin(), wFilePath.end());
    return result;
}

// 查找授权进程（先在子进程中找，再全局搜索）
// 返回 true 并设置 outPid 如果找到
bool FindLicenseProcess(DWORD parentPid, DWORD& outPid)
{
    // 方案1：子进程
    std::vector<DWORD> subPros = GetChildProcessIds(parentPid);
    for (DWORD pid : subPros) {
        if (GetProcessName(pid) == "adobe_licensing_wf.exe") {
            outPid = pid;
            return true;
        }
    }
    // 方案2：全局搜索
    std::vector<DWORD> allPros = FindAllProcessesByName("adobe_licensing_wf.exe");
    if (!allPros.empty()) {
        outPid = allPros[0];
        return true;
    }
    return false;
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

// 通过ShellExecute打开文件本身（让系统通过文件关联启动PS，与双击文件/Acrobat调用一致）
DWORD RunProcessViaFile(const std::string& filePath)
{
    std::wstring wFilePath(filePath.begin(), filePath.end());

    SHELLEXECUTEINFOW sei = { sizeof(SHELLEXECUTEINFOW) };
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_DDEWAIT;
    sei.lpVerb = L"open";
    sei.lpFile = wFilePath.c_str();  // 直接打开文件，让系统找关联程序
    sei.lpParameters = NULL;
    sei.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&sei)) {
        return 0;
    }

    DWORD processId = 0;
    if (sei.hProcess != NULL) {
        processId = GetProcessId(sei.hProcess);
        CloseHandle(sei.hProcess);
    }
    return processId;
}

//启动指定程序-使用ShellExecute模拟正常用户启动(父进程为explorer,避免PS检测)
DWORD RunProcess(std::string proPath, int argc, char* argv[])
{
    std::wstring wProPath(proPath.begin(), proPath.end());
    
    // 构建参数（跳过argv[0]即pps.exe自身）
    std::wstring params;
    for (int i = 1; i < argc; ++i) {
        // 处理含空格的参数，加引号
        std::string arg(argv[i]);
        if (arg.find(' ') != std::string::npos) {
            params += L"\"";
            params += std::wstring(arg.begin(), arg.end());
            params += L"\" ";
        } else {
            params += std::wstring(arg.begin(), arg.end());
            params += L" ";
        }
    }

    SHELLEXECUTEINFOW sei = { sizeof(SHELLEXECUTEINFOW) };
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_DDEWAIT;
    sei.lpVerb = L"open";
    sei.lpFile = wProPath.c_str();
    sei.lpParameters = params.empty() ? NULL : params.c_str();
    sei.lpDirectory = NULL;  // 使用PS所在目录作为工作目录
    sei.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&sei)) {
        return 0;
    }

    // 从进程句柄获取PID
    DWORD processId = GetProcessId(sei.hProcess);
    
    // 不需要等待，关闭句柄
    CloseHandle(sei.hProcess);

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


//解除窗口禁用状态（标准 EnableWindow + 强制重绘非客户区）
void DisableWindow(HWND hwnd)
{
    if (!IsWindowEnabled(hwnd)) {
        EnableWindow(hwnd, TRUE);
        // 强制重绘整个窗口，包括菜单栏等非客户区，以及所有子窗口
        RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_FRAME | RDW_ALLCHILDREN);
    }
}

// 枚举回调：重新启用指定进程的所有已禁用顶层窗口
struct EnableAllData { DWORD pid; };
BOOL CALLBACK EnableAllWindowsCallback(HWND hwnd, LPARAM lParam)
{
    EnableAllData* data = reinterpret_cast<EnableAllData*>(lParam);
    DWORD wpid = 0;
    GetWindowThreadProcessId(hwnd, &wpid);
    if (wpid == data->pid) {
        if (!IsWindowEnabled(hwnd)) {
            EnableWindow(hwnd, TRUE);
            RedrawWindow(hwnd, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_FRAME | RDW_ALLCHILDREN);
        }
    }
    return TRUE;
}

// 重新启用指定进程的所有顶层窗口
void EnableAllProcessWindows(DWORD processId)
{
    EnableAllData data;
    data.pid = processId;
    EnumWindows(EnableAllWindowsCallback, reinterpret_cast<LPARAM>(&data));
}

// 终止指定进程（保留供将来使用，当前版本未调用）
void TerminateProcessById(DWORD processId)
{
    HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, processId);
    if (hProcess != NULL) {
        TerminateProcess(hProcess, 0);
        CloseHandle(hProcess);
    }
}

//隐藏指定窗口
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
