/*++

Copyright (c) Microsoft Corporation.  All rights reserved.

    THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY
    KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A PARTICULAR
    PURPOSE.

Module Name:

    private.h

Abstract:

    Contains structure definitions and function prototypes private to
    the driver.

Environment:

    Kernel mode

--*/

#include <initguid.h>
#include <ntddk.h>
#include <ntintsafe.h>
#include "usbdi.h"
#include "usbdlib.h"

#include <wdf.h>
#include <wdfusb.h>
#include "public.h"

#pragma once

#define POOL_TAG (ULONG)'R500'

#undef ExAllocatePool
#define ExAllocatePool(type, size) \
  ExAllocatePoolWithTag(type, size, POOL_TAG);

#if DBG
#define Rio500_DbgPrint(level, _x_) \
  if ((level) <= DebugLevel) { \
    DbgPrint("Rio500: "); DbgPrint _x_; \
  }
#else
#define Rio500_DbgPrint(level, _x_)
#endif

#define MAX_TRANSFER_SIZE              32768
#define DEFAULT_REGISTRY_TRANSFER_SIZE 65536

#define IDLE_CAPS_TYPE IdleUsbSelectiveSuspend

//
// A structure representing the instance information associated with
// this particular device.
//

typedef struct _DEVICE_CONTEXT {
  USB_DEVICE_DESCRIPTOR          UsbDeviceDescriptor;
  PUSB_CONFIGURATION_DESCRIPTOR  UsbConfigurationDescriptor;
  WDFUSBDEVICE                   WdfUsbTargetDevice;
  ULONG                          WaitWakeEnable;
  BOOLEAN                        IsDeviceHighSpeed;
  WDFUSBINTERFACE                UsbInterface;
  UCHAR                          SelectedAlternateSetting;
  UCHAR                          NumberConfiguredPipes;
  ULONG                          MaximumTransferSize;
  USBD_HANDLE                    UsbdHandle;
  WDFUSBPIPE                     ReadPipe;
  WDFUSBPIPE                     WritePipe;
} DEVICE_CONTEXT, *PDEVICE_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE_CONTEXT, GetDeviceContext)

//
// This context is associated with every request recevied by the driver
// from the app.
//
typedef struct _REQUEST_CONTEXT {
    WDFMEMORY     UrbMemory;
    PMDL          Mdl;
    ULONG         Length;         // remaining to xfer
    ULONG         Numxfer;
    ULONG_PTR     VirtualAddress; // va for next segment of xfer.
    BOOLEAN       Read; // TRUE if Read
} REQUEST_CONTEXT, * PREQUEST_CONTEXT;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(REQUEST_CONTEXT, GetRequestContext)

typedef struct _WORKITEM_CONTEXT {
    WDFDEVICE       Device;
    WDFUSBPIPE      Pipe;
} WORKITEM_CONTEXT, *PWORKITEM_CONTEXT;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(WORKITEM_CONTEXT, GetWorkItemContext)

extern ULONG DebugLevel;

DRIVER_INITIALIZE DriverEntry;

EVT_WDF_DRIVER_DEVICE_ADD          Rio500_EvtDeviceAdd;
EVT_WDF_DEVICE_PREPARE_HARDWARE    Rio500_EvtDevicePrepareHardware;
EVT_WDF_OBJECT_CONTEXT_CLEANUP     Rio500_EvtDeviceContextCleanup;
EVT_WDF_DEVICE_FILE_CREATE         Rio500_EvtDeviceFileCreate;
EVT_WDF_IO_QUEUE_IO_READ           Rio500_EvtIoRead;
EVT_WDF_IO_QUEUE_IO_WRITE          Rio500_EvtIoWrite;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL Rio500_EvtIoDeviceControl;
EVT_WDF_REQUEST_COMPLETION_ROUTINE Rio500_EvtReadWriteCompletion;
EVT_WDF_IO_QUEUE_IO_STOP           Rio500_EvtIoStop;
EVT_WDF_WORKITEM                   Rio500_EvtReadWriteWorkItem;

VOID
ReadWriteBulkEndPoints(
  _In_ WDFQUEUE         Queue,
  _In_ WDFREQUEST       Request,
  _In_ ULONG            Length,
  _In_ WDF_REQUEST_TYPE RequestType
);

NTSTATUS
ResetPipe(
  _In_ WDFUSBPIPE Pipe
);

NTSTATUS
ResetDevice(
  _In_ WDFDEVICE Device
);

NTSTATUS
ReadAndSelectDescriptors(
  _In_ WDFDEVICE Device
);

NTSTATUS
ConfigureDevice(
  _In_ WDFDEVICE Device
);

NTSTATUS
SelectInterfaces(
  _In_ WDFDEVICE Device
);

NTSTATUS
SetPowerPolicy(
  _In_ WDFDEVICE Device
);

NTSTATUS
QueuePassiveLevelCallback(
  _In_ WDFDEVICE Device,
  _In_ WDFUSBPIPE Pipe
);

VOID
DbgPrintRWContext(
  PREQUEST_CONTEXT rwContext
);

NTSTATUS
ReadFdoRegistryKeyValue(
  _In_  WDFDRIVER Driver,
  _In_  LPWSTR    Name,
  _Out_ PULONG    Value
);

NTSTATUS
RetrieveDeviceInformation(
  _In_ WDFDEVICE Device
);

USBD_STATUS
Rio500_ValidateConfigurationDescriptor(  
  _In_reads_bytes_(BufferLength) PUSB_CONFIGURATION_DESCRIPTOR ConfigDesc,
  _In_    ULONG  BufferLength,
  _Inout_ PUCHAR *Offset
);

typedef struct _RIO_IOCTL_BLOCK {
  BYTE RequestType;
  BYTE RequestCode;
  WORD MsgValue;
  WORD MsgIndex;
  WORD MsgLength;
  union {
    PVOID Data;      // data ptr in 64-bit process
#ifdef _WIN64
    DWORD32 DataWow; // data ptr in WoW process
#endif // _WIN64
  } MsgData;
} RIO_IOCTL_BLOCK, *PRIO_IOCTL_BLOCK;

__inline ULONG
GetMaxTransferSize()
/*++

Routine Description:

This routine returns maximum packet size of a bulk pipe

Return Value:

    Maximum Packet Size

--*/
{
  return MAX_TRANSFER_SIZE;
}
