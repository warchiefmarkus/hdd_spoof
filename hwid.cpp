/* This file is part of hdd_serial_spoofer by namazso, licensed under the MIT license:
*
* MIT License
*
* Copyright (c) namazso 2018
*
* Permission is hereby granted, free of charge, to any person obtaining a copy
* of this software and associated documentation files (the "Software"), to deal
* in the Software without restriction, including without limitation the rights
* to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
* copies of the Software, and to permit persons to whom the Software is
* furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in all
* copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
* AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
* OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*/
#include <ntddk.h>
#include <ntdddisk.h>
#include <intrin.h>
#include "defs.h"
#include <windef.h>
#include <random>

#define	DUMP(level, lpszFormat, ...)	switch(level)	\
					{	\
						case WRN: DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "[WRN] : " ## lpszFormat ## "\n", __VA_ARGS__); break;	\
						case ERR: DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "[ERR] : " ## lpszFormat ## "\n", __VA_ARGS__); break;	\
						case INF: \
						default: DbgPrintEx(DPFLTR_IHVDRIVER_ID, DPFLTR_ERROR_LEVEL, "[INF] : " ## lpszFormat ## "\n", __VA_ARGS__); break;	\
					};

enum DBG_LEVEL { INF = 0x0, WRN, ERR };


PDRIVER_DISPATCH g_original_device_control;
unsigned long long g_startup_time;

#define DFP_GET_VERSION          0x00074080
#define IOCTL_STORAGE_MANAGE_DATA_SET_ATTRIBUTES 0x2d9404
#define IOCTL_MOUNTDEV_QUERY_DEVICE_NAME 0x4d0008
#define IOCTL_VOLUME_GET_GPT_ATTRIBUTES 0x560038
#define IOCTL_STORAGE_GET_HOTPLUG_INFO 0x2d0c14
#define IOCTL_STORAGE_QUERY_PROPERTY 0x2d1400
#define SMART_GET_VERSION 0x74080
#define IOCTL_DISK_GET_DRIVE_GEOMETRY 0x70000
#define IOCTL_DISK_GET_LENGTH_INFO 0x7405c

#define IOCTL_SCSI_MINIPORT				0x0004D008

typedef struct _GETVERSIONOUTPARAMS
{
	BYTE bVersion;      // Binary driver version.
	BYTE bRevision;     // Binary driver revision.
	BYTE bReserved;     // Not used.
	BYTE bIDEDeviceMap; // Bit map of IDE devices.
	DWORD fCapabilities; // Bit mask of driver capabilities.
	DWORD dwReserved[4]; // For future use.
} GETVERSIONOUTPARAMS, * PGETVERSIONOUTPARAMS, * LPGETVERSIONOUTPARAMS;

typedef struct _MOUNTDEV_NAME {
	USHORT NameLength;
	WCHAR  Name[1];
} MOUNTDEV_NAME, * PMOUNTDEV_NAME;

#pragma pack()
typedef struct _SRB_IO_CONTROL
{
	ULONG HeaderLength;
	UCHAR Signature[8];
	ULONG Timeout;
	ULONG ControlCode;
	ULONG ReturnCode;
	ULONG Length;
} SRB_IO_CONTROL, * PSRB_IO_CONTROL;

typedef struct _IDSECTOR
{
	USHORT wGenConfig;
	USHORT wNumCyls;
	USHORT wReserved;
	USHORT wNumHeads;
	USHORT wBytesPerTrack;
	USHORT wBytesPerSector;
	USHORT wSectorsPerTrack;
	USHORT wVendorUnique[3];
	CHAR sSerialNumber[20];
	USHORT wBufferType;
	USHORT wBufferSize;
	USHORT wECCSize;
	CHAR sFirmwareRev[8];
	CHAR sModelNumber[40];
	USHORT wMoreVendorUnique;
	USHORT wDoubleWordIO;
	USHORT wCapabilities;
	USHORT wReserved1;
	USHORT wPIOTiming;
	USHORT wDMATiming;
	USHORT wBS;
	USHORT wNumCurrentCyls;
	USHORT wNumCurrentHeads;
	USHORT wNumCurrentSectorsPerTrack;
	ULONG ulCurrentSectorCapacity;
	USHORT wMultSectorStuff;
	ULONG ulTotalAddressableSectors;
	USHORT wSingleWordDMA;
	USHORT wMultiWordDMA;
	BYTE bReserved[128];
} IDSECTOR, * PIDSECTOR;



// SPOOF SERIAL
void spoof_serial(char* serial, bool is_smart);




void SwapChars(char* str, int strLen = 0)
{
	size_t lenStr = strLen == 0 ? strlen(str) : strLen;
	for (size_t i = 0; i < lenStr; i += 2)
	{
		char tmp = str[i];
		str[i] = str[i + 1];
		str[i + 1] = tmp;
	}
}
char* ft_strdup(char* src)
{

	char* str;
	int len = 0;

	while (src[len])
		len++;


	str = (char*)ExAllocatePool(NonPagedPool, sizeof(*str) * (len + 1));
	for (int i = 0; i < len + 1; i++)
		str[i] = src[i];
	str[len] = '\0';
	//printf("%s\n", str);
	return (str);
}
void spoof_param(char* param, const char* newValue, bool swap)
{
	int lenStr;
	if (newValue == 0)
	{
		lenStr = strlen(param);
		for (int i = 0; i < lenStr; i++)
			param[i] = (char)(65 + i % 30);
		return;
	}
	lenStr = strlen(newValue);
	char* swaped = ft_strdup((char*)newValue);
	if (swap)
		SwapChars(swaped, lenStr);
	//printf("\nSwapped: %s\n", swaped);
	for (int i = 0; i < lenStr + 1; i++)
		param[i] = swaped[i];
	ExFreePoolWithTag(swaped, 0);
}

struct REQUEST_STRUCT
{
	PIO_COMPLETION_ROUTINE OldRoutine;
	PVOID OldContext;
	ULONG OutputBufferLength;
	PVOID SystemBuffer;
};

NTSTATUS completed_storage_query(
	PDEVICE_OBJECT device_object,
	PIRP irp,
	PVOID context
)
{
	if (!context)
	{
		DUMP(INF, "%s %d : Context was nullptr\n", __FUNCTION__, __LINE__);
		return STATUS_SUCCESS;
	}

	const auto request = (REQUEST_STRUCT*)context;
	const auto buffer_length = request->OutputBufferLength;
	const auto buffer = (PSTORAGE_DEVICE_DESCRIPTOR)request->SystemBuffer;
	const auto old_routine = request->OldRoutine;
	const auto old_context = request->OldContext;
	ExFreePool(context);

	do
	{

		if (buffer_length < FIELD_OFFSET(STORAGE_DEVICE_DESCRIPTOR, RawDeviceProperties))
			break;	// They just want the size

		if (buffer->SerialNumberOffset == 0)
		{
			DUMP(INF, "%s %d : Device doesn't have unique ID\n", __FUNCTION__, __LINE__);
			break;
		}

		if (buffer_length < FIELD_OFFSET(STORAGE_DEVICE_DESCRIPTOR, RawDeviceProperties) + buffer->RawPropertiesLength
			|| buffer->SerialNumberOffset < FIELD_OFFSET(STORAGE_DEVICE_DESCRIPTOR, RawDeviceProperties)
			|| buffer->SerialNumberOffset >= buffer_length
			)
		{
			DUMP(INF, "%s %d : Malformed buffer (should never happen) size: %d\n", __FUNCTION__, __LINE__, buffer_length);
		}
		else
		{
			const auto product = (char*)buffer + buffer->ProductIdOffset;
			const auto serial = (char*)buffer + buffer->SerialNumberOffset;
			//DUMP(INF, "%s %d : Product: %s\n", __FUNCTION__, __LINE__, product);
			//DUMP(INF, "%s %d : Serial: %s\n", __FUNCTION__, __LINE__, serial);
			spoof_param(product, "Samsung", false);
			spoof_param(serial, "00000000000000000002", false);
			//spoof_serial(serial, false);
			//DUMP(INF, "%s %d : Product Modified: %s\n", __FUNCTION__, __LINE__, product);
			//DUMP(INF, "%s %d : Serial Modified: %s\n", __FUNCTION__, __LINE__, serial);
		}
	} while (false);

	// Call next completion routine (if any)
	if (irp->StackCount > 1ul && old_routine)
		return old_routine(device_object, irp, old_context);

	return STATUS_SUCCESS;
}


NTSTATUS completed_smart(
	PDEVICE_OBJECT device_object,
	PIRP irp,
	PVOID context
)
{
	UNREFERENCED_PARAMETER(device_object);

	if (!context)
	{
		DUMP(INF, "%s %d : Context was nullptr\n", __FUNCTION__, __LINE__);
		return STATUS_SUCCESS;
	}

	const auto request = (REQUEST_STRUCT*)context;
	const auto buffer_length = request->OutputBufferLength;
	const auto buffer = (SENDCMDOUTPARAMS*)request->SystemBuffer;
	//const auto old_routine = request->OldRoutine;
	//const auto old_context = request->OldContext;
	ExFreePool(context);

	if (buffer_length < FIELD_OFFSET(SENDCMDOUTPARAMS, bBuffer)
		|| FIELD_OFFSET(SENDCMDOUTPARAMS, bBuffer) + buffer->cBufferSize > buffer_length
		|| buffer->cBufferSize < sizeof(IDINFO)
		)
	{
		DUMP(INF, "%s %d : Malformed buffer (should never happen) size: %d\n", __FUNCTION__, __LINE__, buffer_length);
	}
	else
	{
		const auto info = (IDINFO*)buffer->bBuffer;
		//info->sModelNumber
		const auto storagedevdesc = (STORAGE_DEVICE_DESCRIPTOR*)buffer->bBuffer;
		const auto serial = info->sSerialNumber;
		const auto sFirmwareRev = info->sFirmwareRev;//(char*)storagedevdesc + storagedevdesc->ProductIdOffset;
		//DUMP(INF, "%s %d : FirmwareRev: %s\n", __FUNCTION__, __LINE__, info->sFirmwareRev);
		//DUMP(INF, "%s %d : ModelNumber: %s\n", __FUNCTION__, __LINE__, info->sModelNumber);
		//DUMP(INF, "%s %d : VendorUnique: %s\n", __FUNCTION__, __LINE__, info->wVendorUnique);
		//DUMP(INF, "%s %d : wMoreVendorUnique: %s\n", __FUNCTION__, __LINE__, info->wMoreVendorUnique);
		info->sModelNumber[0] = 'M';
		info->sModelNumber[1] = 'D';
		info->sFirmwareRev[0] = 'M';
		info->sFirmwareRev[1] = 'D';
		info->wMoreVendorUnique = 'M';
		//DUMP(INF, "%s %d : FirmwareRev  Modified: %s\n", __FUNCTION__, __LINE__, info->sFirmwareRev);
		//DUMP(INF, "%s %d : ModelNumber  Modified: %s\n", __FUNCTION__, __LINE__, info->sModelNumber);
		//DUMP(INF, "%s %d : VendorUnique Modified: %s\n", __FUNCTION__, __LINE__, info->wVendorUnique);


		//DUMP(INF, "%s %d : Serial20: %s\n", __FUNCTION__, __LINE__, serial);
		//spoof_serial(serial, true);
		spoof_param(serial, "00000000000000000002", false);
		//DUMP(INF, "%s %d : Serial21: %s\n", __FUNCTION__, __LINE__, serial);
	}

	// I have no fucking idea why not calling the original doesnt cause problems but whatever

	//DUMP(INF,"%s: Returning STATUS_NOT_SUPPORTED\n", __FUNCTION__));

	// We deny access by returning an ERROR code
	//irp->IoStatus.Status = STATUS_NOT_SUPPORTED;

	// Call next completion routine (if any)
	//if ((irp->StackCount > (ULONG)1) && (OldCompletionRoutine != NULL))
	//	return OldCompletionRoutine(device_object, irp, OldContext);

	return irp->IoStatus.Status;
}



void do_completion_hook(PIRP irp, PIO_STACK_LOCATION ioc, PIO_COMPLETION_ROUTINE routine)
{
	// Register CompletionRotuine
	ioc->Control = 0;
	ioc->Control |= SL_INVOKE_ON_SUCCESS;

	// Save old completion routine
	// Yes we rewrite any routine to be on success only
	// and somehow it doesnt cause disaster
	const auto old_context = ioc->Context;
	ioc->Context = ExAllocatePool(NonPagedPool, sizeof(REQUEST_STRUCT));
	const auto request = (REQUEST_STRUCT*)ioc->Context;
	request->OldRoutine = ioc->CompletionRoutine;
	request->OldContext = old_context;
	request->OutputBufferLength = ioc->Parameters.DeviceIoControl.OutputBufferLength;
	request->SystemBuffer = irp->AssociatedIrp.SystemBuffer;

	// Setup our function to be called upon completion of the IRP
	ioc->CompletionRoutine = routine;
}

// HOOK DEVICE
NTSTATUS hooked_device_control(PDEVICE_OBJECT device_object, PIRP irp)
{
	const auto ioc = IoGetCurrentIrpStackLocation(irp);

	DUMP(INF, "CTL_CODE %x", ioc->Parameters.DeviceIoControl.IoControlCode);

	switch (ioc->Parameters.DeviceIoControl.IoControlCode)
	{
		

		case IOCTL_STORAGE_QUERY_PROPERTY:
		{
			const auto query = (PSTORAGE_PROPERTY_QUERY)irp->AssociatedIrp.SystemBuffer;
			//DUMP(INF, "[IOCTL_STORAGE_QUERY_PROPERTY] query->PropertyId = %d", query->PropertyId);
			if (query->PropertyId == StorageDeviceProperty)
				do_completion_hook(irp, ioc, &completed_storage_query);
			break;
		}
	

		case SMART_RCV_DRIVE_DATA:
		{
			do_completion_hook(irp, ioc, &completed_smart);
			break;
		}

		//case IOCTL_MOUNTDEV_QUERY_DEVICE_NAME:
		//{
		//	PMOUNTDEV_NAME PtrMountedDeviceName;
		//	UNICODE_STRING DeviceName;
		//	PtrMountedDeviceName = (PMOUNTDEV_NAME)irp->AssociatedIrp.SystemBuffer;
		//	
		//	DUMP(INF, "[IOCTL_MOUNTDEV_QUERY_DEVICE_NAME] %c", PtrMountedDeviceName->Name);
		//	break;
		//}
		//case IOCTL_STORAGE_MANAGE_DATA_SET_ATTRIBUTES:
		//{
		//	PDEVICE_MANAGE_DATA_SET_ATTRIBUTES_OUTPUT var;
		//	var = (PDEVICE_MANAGE_DATA_SET_ATTRIBUTES_OUTPUT)irp->AssociatedIrp.SystemBuffer;
		//	
		//	DUMP(INF, "[PDEVICE_MANAGE_DATA_SET_ATTRIBUTES_OUTPUT] %d", var->Size);
		//	break;
		//}
		case IOCTL_SCSI_MINIPORT:
			//g_original_device_control(device_object, irp);
			//SENDCMDOUTPARAMS* buff = (SENDCMDOUTPARAMS*)((char*)irp->AssociatedIrp.SystemBuffer + sizeof(SRB_IO_CONTROL));
			//IDSECTOR* idsector = (IDSECTOR*)buff;
			DUMP(INF,"[IOCTL_SCSI_MINIPORT] works ");
			//return 0;
			break;
		//case DFP_GET_VERSION:
		//{
		//	NTSTATUS result = g_original_device_control(device_object, irp);
		//	DUMP(INF, "[DFP_GET_VERSION] - SystemBuffer->bVersion: %x", ((GETVERSIONOUTPARAMS*)(((REQUEST_STRUCT*)(ioc->Context))->SystemBuffer))->bVersion);
		//	return result;
		//	 g_original_device_control(device_object, irp);
		//	 DUMP(INF, "[DFP_GET_VERSION] - SystemBuffer->bVersion: %x", ((GETVERSIONOUTPARAMS*)(((REQUEST_STRUCT*)(ioc->Context))->SystemBuffer))->bVersion);
		//	break;
		//}

		//default:
		//	break;
	}

	return g_original_device_control(device_object, irp);
}

// HOOK
void apply_hook()
{
	UNICODE_STRING driver_name = RTL_CONSTANT_STRING(L"\\Driver\\Disk");
	PDRIVER_OBJECT driver_object = nullptr;
	auto status = ObReferenceObjectByName(
		&driver_name,
		OBJ_CASE_INSENSITIVE,
		nullptr,
		0,
		*IoDriverObjectType,
		KernelMode,
		nullptr,
		(PVOID*)&driver_object
	);

	if (!driver_object || !NT_SUCCESS(status))
	{
		DUMP(INF, "%s %d : ObReferenceObjectByName returned 0x%08X driver_object: 0x%016X\n", __FUNCTION__, __LINE__, status, driver_object);
		return;
	}

	auto& device_control = driver_object->MajorFunction[IRP_MJ_DEVICE_CONTROL];
	g_original_device_control = device_control;
	device_control = &hooked_device_control;
	ObDereferenceObject(driver_object);
}

//---------------------------------------------------------------------------------


// UNLOAD
void DriverUnload(PDRIVER_OBJECT driver)
{
	DUMP(INF, "UNLOAD DRIVER \n");

	UNICODE_STRING driver_name = RTL_CONSTANT_STRING(L"\\Driver\\Disk");
	PDRIVER_OBJECT driver_object = nullptr;
	auto status = ObReferenceObjectByName(
		&driver_name,
		OBJ_CASE_INSENSITIVE,
		nullptr,
		0,
		*IoDriverObjectType,
		KernelMode,
		nullptr,
		(PVOID*)&driver_object
	);

	if (g_original_device_control)
		driver_object->MajorFunction[IRP_MJ_DEVICE_CONTROL] = g_original_device_control;
	UNREFERENCED_PARAMETER(driver);
}
extern "C"
NTSTATUS EntryPoint(
	_DRIVER_OBJECT * DriverObject,
	PUNICODE_STRING RegistryPath
)
{
	UNREFERENCED_PARAMETER(DriverObject);
	UNREFERENCED_PARAMETER(RegistryPath);

	KeQuerySystemTime(&g_startup_time);
	apply_hook();
	DriverObject->DriverUnload = DriverUnload;



	//n_disk::disable_smart();
	// change_disk_serials();
	//n_disk::fuck_dispatch();
	// start_hook();

	return STATUS_SUCCESS;
}