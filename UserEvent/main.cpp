#include <iostream>
#include <Windows.h>
#include <vector>
#define EVENT_NAME L"Global\\kProcessEvent"
//	RtlInitUnicodeString(&EventName, L"\\BaseNamedObjects\\kProcessEvent");
typedef struct PROCESS_LIST
{
	ULONG64 PID;
	ULONG64 PPID;
	WCHAR Path[260];
	ULONG64 EPROCESS;
}PROCESS_LIST, * PPROCESS_LIST;
#define GET_PROCESS_NUMBER CTL_CODE(FILE_DEVICE_UNKNOWN,0x4a000,METHOD_BUFFERED ,FILE_ANY_ACCESS)
#define GET_PROCESS_INFO CTL_CODE(FILE_DEVICE_UNKNOWN,0x4a001,METHOD_BUFFERED ,FILE_ANY_ACCESS)
int main() {
	PROCESS_LIST TempProcessList = {};//∑¿÷π÷ÿ∏¥
	DWORD dwRet = 0;
	BOOL bRet = FALSE;
	HANDLE hDriver = CreateFile(L"\\\\.\\EventConnectionDriver", GENERIC_READ | GENERIC_WRITE, NULL, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hDriver == INVALID_HANDLE_VALUE)
	{
		std::cout << "[-]Failed to Open Driver ! ! ! " << std::endl;
		getchar();
		return 0;
	}		
	std::cout << "[+]Open Driver Success" << std::endl;
	HANDLE hProcessEvent = OpenEventW(SYNCHRONIZE, FALSE, EVENT_NAME);
	if (hProcessEvent == INVALID_HANDLE_VALUE)
	{
		std::cout << "[-]Failed to Open Event ! ! ! " << std::endl;
		getchar();
		return 0;
	}	
	std::cout << "[+]Open Event Success" << std::endl;
	while (true) {
		DWORD dwWaitResult = WaitForSingleObject(hProcessEvent, INFINITE);
		if (dwWaitResult == WAIT_OBJECT_0) {
			std::vector<PROCESS_LIST> ProcessList;
			ProcessList.clear();
			std::cout << "[+]GET EVENT ! ! !" << std::endl;
			DWORD process_number = 0;
			bRet = DeviceIoControl(hDriver, GET_PROCESS_NUMBER, 0, 0, 0, 0, &process_number, NULL);
			if (!process_number)
			{
				break;
			}

			DWORD dwRet = 0;
			PROCESS_LIST* temp_list = (PROCESS_LIST*)new char[process_number * sizeof(PROCESS_LIST)];
			if (!temp_list)
			{
				break;
			}

			bRet = DeviceIoControl(hDriver, GET_PROCESS_INFO, 0, 0, temp_list, sizeof(PROCESS_LIST) * process_number, &dwRet, NULL);
			if (dwRet)
			{
				for (int i = 0; i < process_number; i++)
				{
					ProcessList.push_back(temp_list[i]);
				}
			}
			for (auto ProcessInfo : ProcessList) {
				if (ProcessInfo.EPROCESS != TempProcessList.EPROCESS) {
					std::cout << "Create New Process | Pid : " << ProcessInfo.PID << std::endl;
				}
				TempProcessList = ProcessInfo;
			}
		}
		else {
			std::cout << "[-]WaitForSingleObject failed with error: " << GetLastError() << std::endl;
			break;
		}
	}
	getchar();
	CloseHandle(hDriver);
}