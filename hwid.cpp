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
	ExFreePoolWithTag(swaped,0);
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
		DUMP(INF,"%s %d : Context was nullptr\n", __FUNCTION__, __LINE__);
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
			DUMP(INF,"%s %d : Device doesn't have unique ID\n", __FUNCTION__, __LINE__);
			break;
		}

		if (buffer_length < FIELD_OFFSET(STORAGE_DEVICE_DESCRIPTOR, RawDeviceProperties) + buffer->RawPropertiesLength
			|| buffer->SerialNumberOffset < FIELD_OFFSET(STORAGE_DEVICE_DESCRIPTOR, RawDeviceProperties)
			|| buffer->SerialNumberOffset >= buffer_length
			)
		{
			DUMP(INF,"%s %d : Malformed buffer (should never happen) size: %d\n", __FUNCTION__, __LINE__, buffer_length);
		}
		else
		{
			const auto product = (char*)buffer + buffer->ProductIdOffset;
			const auto serial = (char*)buffer + buffer->SerialNumberOffset;
			DUMP(INF, "%s %d : Product: %s\n", __FUNCTION__, __LINE__, product);
			DUMP(INF, "%s %d : Serial: %s\n", __FUNCTION__, __LINE__, serial);
			spoof_param(product, "Samsung", false);
			spoof_serial(serial, false);
			DUMP(INF, "%s %d : Product Modified: %s\n", __FUNCTION__, __LINE__, product);			
			DUMP(INF, "%s %d : Serial Modified: %s\n", __FUNCTION__, __LINE__, serial);
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
		DUMP(INF,"%s %d : Context was nullptr\n", __FUNCTION__, __LINE__);
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
		DUMP(INF,"%s %d : Malformed buffer (should never happen) size: %d\n", __FUNCTION__, __LINE__, buffer_length);
	}
	else
	{
		const auto info = (IDINFO*)buffer->bBuffer;
		//info->sModelNumber
		const auto storagedevdesc = (STORAGE_DEVICE_DESCRIPTOR*)buffer->bBuffer;
		const auto serial = info->sSerialNumber;
		const auto sFirmwareRev = info->sFirmwareRev;//(char*)storagedevdesc + storagedevdesc->ProductIdOffset;
		DUMP(INF, "%s %d : FirmwareRev: %s\n", __FUNCTION__, __LINE__, info->sFirmwareRev);
		DUMP(INF, "%s %d : ModelNumber: %s\n", __FUNCTION__, __LINE__, info->sModelNumber);
		DUMP(INF, "%s %d : VendorUnique: %s\n", __FUNCTION__, __LINE__, info->wVendorUnique);
		DUMP(INF, "%s %d : wMoreVendorUnique: %s\n", __FUNCTION__, __LINE__, info->wMoreVendorUnique);
		info->sModelNumber[0] = 'M';
		info->sModelNumber[1] = 'D';
		info->sFirmwareRev[0] = 'M';
		info->sFirmwareRev[1] = 'D';
		info->wMoreVendorUnique = 'M';
		DUMP(INF, "%s %d : FirmwareRev  Modified: %s\n", __FUNCTION__, __LINE__, info->sFirmwareRev);
		DUMP(INF, "%s %d : ModelNumber  Modified: %s\n", __FUNCTION__, __LINE__, info->sModelNumber);
		DUMP(INF, "%s %d : VendorUnique Modified: %s\n", __FUNCTION__, __LINE__, info->wVendorUnique);


		DUMP(INF, "%s %d : Serial20: %s\n", __FUNCTION__, __LINE__, serial);
		spoof_serial(serial, true);
		DUMP(INF, "%s %d : Serial21: %s\n", __FUNCTION__, __LINE__, serial);
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

NTSTATUS hooked_device_control(PDEVICE_OBJECT device_object, PIRP irp)
{
	const auto ioc = IoGetCurrentIrpStackLocation(irp);

	switch (ioc->Parameters.DeviceIoControl.IoControlCode)
	{
	case IOCTL_STORAGE_QUERY_PROPERTY:
	{
		const auto query = (PSTORAGE_PROPERTY_QUERY)irp->AssociatedIrp.SystemBuffer;

		if (query->PropertyId == StorageDeviceProperty)
			do_completion_hook(irp, ioc, &completed_storage_query);
	}
	break;
	case SMART_RCV_DRIVE_DATA:
		do_completion_hook(irp, ioc, &completed_smart);
		break;
	default:
		break;
	}

	return g_original_device_control(device_object, irp);
}


bool handle_disk_serials(PDEVICE_OBJECT device_object, RaidUnitRegisterInterfaces func)
{
	if (device_object == 0 || func == 0) return false;

	while (device_object->NextDevice)
	{
		do
		{
			if (device_object->DeviceType == FILE_DEVICE_DISK)
			{
				PRAID_UNIT_EXTENSION extension = reinterpret_cast<PRAID_UNIT_EXTENSION>(device_object->DeviceExtension);
				if (extension == 0)
				{
					n_log::printf("DeviceExtension buffer is null \n");
					break;
				}

				unsigned short length = extension->_Identity.Identity.SerialNumber.Length;
				if (length == 0)
				{
					n_log::printf("serial_number length is null \n");
					break;
				}

				n_log::printf("old disk serial number : %s \n", extension->_Identity.Identity.SerialNumber.Buffer);
				RtlCopyMemory(extension->_Identity.Identity.SerialNumber.Buffer, disk_serial_buffer, length);
				//n_util::random_string(extension->_Identity.Identity.SerialNumber.Buffer, length);
				n_log::printf("new disk serial number : %s \n", extension->_Identity.Identity.SerialNumber.Buffer);

				extension->_Smart.Telemetry.SmartMask = 0;
				func(extension);
			}
		} while (false);

		device_object = device_object->NextDevice;
	}

	return true;
}

bool change_disk_serials()
{
	DWORD64 address = 0;
	DWORD32 size = 0;
	if (n_util::get_module_base_address("storport.sys", address, size) == false) return false;
	n_log::printf("storport address : %llx \t size : %x \n", address, size);

	RaidUnitRegisterInterfaces func = (RaidUnitRegisterInterfaces)n_util::find_pattern_image(address,
		"\x48\x89\x5C\x24\x00\x55\x56\x57\x48\x83\xEC\x50",
		"xxxx?xxxxxxx");// RaidUnitRegisterInterfaces
	if (func == 0) return false;
	n_log::printf("RaidUnitRegisterInterfaces address : %llx \n", func);

	for (int i = 0; i < 5; i++)
	{
		const wchar_t* format = L"\\Device\\RaidPort%d";
		wchar_t buffer[18]{ 0 };
		RtlStringCbPrintfW(buffer, 18 * sizeof(wchar_t), format, i);

		UNICODE_STRING raid_port;
		RtlInitUnicodeString(&raid_port, buffer);

		PFILE_OBJECT file_object = 0;
		PDEVICE_OBJECT device_object = 0;
		NTSTATUS status = IoGetDeviceObjectPointer(&raid_port, FILE_READ_DATA, &file_object, &device_object);
		if (NT_SUCCESS(status))
		{
			handle_disk_serials(device_object->DriverObject->DeviceObject, func);

			ObDereferenceObject(file_object);
		}
	}

	return true;
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
		DUMP(INF,"%s %d : ObReferenceObjectByName returned 0x%08X driver_object: 0x%016X\n", __FUNCTION__, __LINE__, status, driver_object);
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
	change_disk_serials();
	//n_disk::fuck_dispatch();
	start_hook();

	return STATUS_SUCCESS;
}