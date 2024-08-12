#include <ntifs.h>
#include <ntstrsafe.h>
#include <vector>
#define DRIVER_DEVICE_NAME     L"\\Device\\EventConnectionDriver"
#define DRIVER_LINK_NAME L"\\??\\EventConnectionDriver"
typedef struct PROCESS_LIST
{
	ULONG64 PID;
	ULONG64 PPID;
	WCHAR Path[260];
	ULONG64 EPROCESS;
}PROCESS_LIST, * PPROCESS_LIST;

//需要传输的结构体
#pragma region Common
typedef PVOID(NTAPI* tExAllocatePool2)(
	POOL_FLAGS Flags,
	SIZE_T     NumberOfBytes,
	ULONG      Tag
	);
ULONG WindowsBuildNumber = 0;
PVOID AllocatePool2 = NULL;
#define DRIVER_TAG 'EvkN'
#define WIN_1507 10240
#define WIN_1511 10586
#define WIN_1607 14393
#define WIN_1703 15063
#define WIN_1709 16299
#define WIN_1803 17134
#define WIN_1809 17763
#define WIN_1903 18362
#define WIN_1909 18363
#define WIN_2004 19041
#define WIN_20H2 19042
#define WIN_21H1 19043
#define WIN_21H2 19044
#define WIN_22H2 19045
#define WIN_1121H2 22000
#define WIN_1122H2 22621
extern "C" NTKERNELAPI NTSTATUS NTAPI ZwQueryInformationProcess(
	__in HANDLE ProcessHandle,
	__in PROCESSINFOCLASS ProcessInformationClass,
	__out_bcount(ProcessInformationLength) PVOID ProcessInformation,
	__in ULONG ProcessInformationLength,
	__out_opt PULONG ReturnLength
);
bool Get_Process_Image(HANDLE Process_Handle, UNICODE_STRING* Process_Path)
{
	NTSTATUS status = 0;
	ULONG Query_Return_Lenght = 0;
	UNICODE_STRING* temp_process_image_name = nullptr;
	FILE_OBJECT* process_image_file_object = nullptr;
	DEVICE_OBJECT* process_image_device_object = nullptr;
	OBJECT_NAME_INFORMATION* process_image_object_name = nullptr;

	//get full image name
	status = ZwQueryInformationProcess(Process_Handle, ProcessImageFileName,
		nullptr, 0, &Query_Return_Lenght);
	temp_process_image_name = (UNICODE_STRING*)new char[Query_Return_Lenght];
	RtlZeroMemory(temp_process_image_name, Query_Return_Lenght);
	//frist call ZwQueryInformationProcess get how long memory for we need
	status = ZwQueryInformationProcess(Process_Handle, ProcessImageFileName,
		temp_process_image_name, Query_Return_Lenght, &Query_Return_Lenght);
	if (!NT_SUCCESS(status))
	{
		goto Clean;
	}

	//conversion the image path
	status = IoGetDeviceObjectPointer(temp_process_image_name, SYNCHRONIZE,
		&process_image_file_object, &process_image_device_object);
	if (!NT_SUCCESS(status))
	{
		goto Clean;
	}
	status = IoQueryFileDosDeviceName(process_image_file_object, &process_image_object_name);
	if (!NT_SUCCESS(status))
	{
		goto Clean;
	}
	Process_Path->Length = process_image_object_name->Name.Length;
	Process_Path->MaximumLength = process_image_object_name->Name.MaximumLength;
	Process_Path->Buffer = (PWCH)new char[Process_Path->MaximumLength];
	RtlCopyMemory(Process_Path->Buffer, process_image_object_name->Name.Buffer, Process_Path->MaximumLength);

	ExFreePool(process_image_object_name);
	delete[](char*)temp_process_image_name;
	ObDereferenceObject(process_image_file_object);
	return true;
Clean:
	if (process_image_object_name)
	{
		ExFreePool(process_image_object_name);
	}
	if (temp_process_image_name)
	{
		delete[](char*)temp_process_image_name;
	}
	if (process_image_file_object)
	{
		ObDereferenceObject(process_image_file_object);
	}
	return false;
}
PVOID AllocateMemory(SIZE_T size) {
	bool paged = true;
	if (AllocatePool2 && WindowsBuildNumber >= WIN_2004) {
		return paged ? ((tExAllocatePool2)AllocatePool2)(POOL_FLAG_PAGED, size, DRIVER_TAG) :
			((tExAllocatePool2)AllocatePool2)(POOL_FLAG_NON_PAGED_EXECUTE, size, DRIVER_TAG);
	}

#pragma warning( push )
#pragma warning( disable : 4996)
	return paged ? ExAllocatePoolWithTag(PagedPool, size, DRIVER_TAG) :
		ExAllocatePoolWithTag(NonPagedPool, size, DRIVER_TAG);
#pragma warning( pop )
}
NTSTATUS InitializeFeatures() {
	// Get windows version.
	RTL_OSVERSIONINFOW osVersion = { sizeof(osVersion) };
	NTSTATUS result = RtlGetVersion(&osVersion);

	if (!NT_SUCCESS(result))
		return false;

	WindowsBuildNumber = osVersion.dwBuildNumber;

	if (WindowsBuildNumber < WIN_1507)
		return false;

	UNICODE_STRING routineName = RTL_CONSTANT_STRING(L"ExAllocatePool2");
	AllocatePool2 = MmGetSystemRoutineAddress(&routineName);
	return result;
}
#pragma endregion
DRIVER_OBJECT* Driver_Object = nullptr;
DEVICE_OBJECT* Device_Object = nullptr;
UNICODE_STRING Device_Name;
UNICODE_STRING Link_Name;
NTSTATUS Delete_IO_Control();
NTSTATUS IO_Default(PDEVICE_OBJECT  DeviceObject, PIRP  pIrp);
NTSTATUS IoDeviceControl_Centre(PDEVICE_OBJECT DeviceObject, PIRP pIrp);
VOID CreateProcessThreadRoutine(IN HANDLE pParentId, HANDLE hProcessId, BOOLEAN bisCreate);

HANDLE  hProcessEventHandle;
PKEVENT kProcessEvent;
#define GET_PROCESS_NUMBER CTL_CODE(FILE_DEVICE_UNKNOWN,0x4a000,METHOD_BUFFERED ,FILE_ANY_ACCESS)
#define GET_PROCESS_INFO CTL_CODE(FILE_DEVICE_UNKNOWN,0x4a001,METHOD_BUFFERED ,FILE_ANY_ACCESS)
std::vector<PROCESS_LIST> ProcessList;
std::vector<PROCESS_LIST> TempProcessList;
NTSTATUS CreateDriverConnection() {

	NTSTATUS status = 0;
	//创建设备对象
	RtlInitUnicodeString(&Device_Name,DRIVER_DEVICE_NAME);

	status = IoCreateDevice(Driver_Object, 0, &Device_Name, FILE_DEVICE_UNKNOWN, 0, FALSE, &Device_Object);
	if (!NT_SUCCESS(status))
	{
		DbgPrint("Create Device error!\n");
		return status;
	}
	DbgPrint("[EventConnection] Create Device Success DEVICE_EXTEN");
	Device_Object->Flags |= DO_BUFFERED_IO;
	//创建符号连接
	RtlInitUnicodeString(&Link_Name, DRIVER_LINK_NAME);
	status = IoCreateSymbolicLink(&Link_Name, &Device_Name);
	if (!NT_SUCCESS(status))
	{
		IoDeleteDevice(Device_Object);
		DbgPrint("Create Link error!\n");
		return status;
	}

	UNICODE_STRING EventName = { 0 };
	RtlInitUnicodeString(&EventName, L"\\BaseNamedObjects\\kProcessEvent");
	kProcessEvent = IoCreateNotificationEvent(&EventName, &hProcessEventHandle);
	KeClearEvent(kProcessEvent);
	DbgPrint("[EventConnection] Create Event | Name : kProcessEvent");
	Driver_Object->MajorFunction[IRP_MJ_CREATE] = IO_Default;
	Driver_Object->MajorFunction[IRP_MJ_DEVICE_CONTROL] = IoDeviceControl_Centre;
	return STATUS_SUCCESS;
}

NTSTATUS Delete_IO_Control()
{

	IoDeleteSymbolicLink(&Link_Name);
	IoDeleteDevice(Device_Object);
	DbgPrint("Link_Unload\n");
	return STATUS_SUCCESS;
}
NTSTATUS IO_Default(PDEVICE_OBJECT  DeviceObject, PIRP  pIrp)
{
	pIrp->IoStatus.Status = STATUS_SUCCESS;
	pIrp->IoStatus.Information = 0;
	IoCompleteRequest(pIrp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}

NTSTATUS IoDeviceControl_Centre(PDEVICE_OBJECT DeviceObject, PIRP pIrp)
{
	PIO_STACK_LOCATION irp = IoGetCurrentIrpStackLocation(pIrp);
	ULONG Io_Control_Code = irp->Parameters.DeviceIoControl.IoControlCode;
	ULONG Input_Length = irp->Parameters.DeviceIoControl.InputBufferLength;
	ULONG Output_Length = irp->Parameters.DeviceIoControl.OutputBufferLength;
	char* Input_Buffer = (char*)pIrp->AssociatedIrp.SystemBuffer;
	switch (Io_Control_Code)
	{
	case GET_PROCESS_NUMBER:{
		TempProcessList.clear();
		TempProcessList = ProcessList;
		ProcessList.clear();
		pIrp->IoStatus.Status = STATUS_SUCCESS;
		pIrp->IoStatus.Information = TempProcessList.size();
		IoCompleteRequest(pIrp, IO_NO_INCREMENT);
		return STATUS_SUCCESS;
		break;
		}
	case GET_PROCESS_INFO: {
		if (Output_Length < TempProcessList.size() * sizeof(PROCESS_LIST))
		{
			pIrp->IoStatus.Status = STATUS_UNSUCCESSFUL;
			pIrp->IoStatus.Information = 0;
			IoCompleteRequest(pIrp, IO_NO_INCREMENT);
			return STATUS_SUCCESS;
		}

		int i = 0;
		for (auto x : TempProcessList)
		{
			RtlCopyMemory(Input_Buffer + i * sizeof(PROCESS_LIST), &x, sizeof(PROCESS_LIST));
			i++;
		}
		pIrp->IoStatus.Status = STATUS_SUCCESS;
		pIrp->IoStatus.Information = TempProcessList.size() * sizeof(PROCESS_LIST);
		IoCompleteRequest(pIrp, IO_NO_INCREMENT);
		return STATUS_SUCCESS;
		break;
	}
	default:
		break;
	}
	pIrp->IoStatus.Status = STATUS_SUCCESS;
	pIrp->IoStatus.Information = 0;
	IoCompleteRequest(pIrp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}
void DriverUnload(PDRIVER_OBJECT drive_object)
{
	Delete_IO_Control();
	PsSetCreateProcessNotifyRoutine(CreateProcessThreadRoutine, TRUE);
}

VOID CreateProcessThreadRoutine(IN HANDLE pParentId, HANDLE hProcessId, BOOLEAN bisCreate)
{
	if (bisCreate) {
		KeSetEvent(kProcessEvent, 0, FALSE);
		DbgPrint("[EventConnection] Set Event Success");
		PPROCESS_LIST NewProcess = (PPROCESS_LIST)AllocateMemory(sizeof(PROCESS_LIST)); 
		NewProcess->PID = (ULONG64)hProcessId;
		NewProcess->PPID = (ULONG64)pParentId;
		PEPROCESS tempep;
		NTSTATUS status = PsLookupProcessByProcessId((HANDLE)hProcessId, &tempep);
		if (NT_SUCCESS(status))
		{
			NewProcess->EPROCESS = (ULONG64)tempep;
			ObDereferenceObject(tempep);
		}
		HANDLE handle;
		OBJECT_ATTRIBUTES ObjectAttributes;
		CLIENT_ID clientid;
		InitializeObjectAttributes(&ObjectAttributes, 0, OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE, 0, 0);
		clientid.UniqueProcess = (HANDLE)hProcessId;
		clientid.UniqueThread = 0;
		if (NT_SUCCESS(ZwOpenProcess(&handle, PROCESS_ALL_ACCESS, &ObjectAttributes, &clientid)))
		{
			UNICODE_STRING temp_str;
			if (Get_Process_Image(handle, &temp_str))
			{
				RtlCopyMemory(NewProcess->Path, temp_str.Buffer, temp_str.MaximumLength);
				delete temp_str.Buffer;
			}
		}
		ProcessList.push_back(*NewProcess);
		ExFreePoolWithTag(NewProcess, DRIVER_TAG);
		KeResetEvent(kProcessEvent);
	}
	else {

	}
}
extern "C" NTSTATUS DriverMain(PDRIVER_OBJECT drive_object, PUNICODE_STRING path)
{
	DbgPrint("[+]Driver Load Success");
	Driver_Object = drive_object;
	CreateDriverConnection();
	PsSetCreateProcessNotifyRoutine(CreateProcessThreadRoutine, FALSE);
	drive_object->DriverUnload = DriverUnload;
	return STATUS_SUCCESS;
}
