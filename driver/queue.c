/*++

Copyright (c) Microsoft Corporation.  All rights reserved.

    THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY
    KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A PARTICULAR
    PURPOSE.

Module Name:

    queue.c

Abstract:

    This file contains dispatch routines for create,
    close, device-control, read & write.

Environment:

    Kernel mode

--*/

#include "private.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, Rio500_EvtDeviceFileCreate)
#pragma alloc_text(PAGE, Rio500_EvtIoDeviceControl)
#pragma alloc_text(PAGE, Rio500_EvtIoRead)
#pragma alloc_text(PAGE, Rio500_EvtIoWrite)
#pragma alloc_text(PAGE, ResetPipe)
#pragma alloc_text(PAGE, ResetDevice)
#endif

VOID
Rio500_EvtDeviceFileCreate(
  _In_ WDFDEVICE     Device,
  _In_ WDFREQUEST    Request,
  _In_ WDFFILEOBJECT FileObject
)
/*++

Routine Description:

    The framework calls a driver's EvtDeviceFileCreate callback
    when the framework receives an IRP_MJ_CREATE request.
    The system sends this request when a user application opens the
    device to perform an I/O operation, such as reading or writing a file.
    This callback is called synchronously, in the context of the thread
    that created the IRP_MJ_CREATE request.

Arguments:

    Device - Handle to a framework device object.
    FileObject - Pointer to fileobject that represents the open handle.
    CreateParams - copy of the create IO_STACK_LOCATION

Return Value:

   NT status code

--*/
{
  NTSTATUS        status = STATUS_SUCCESS;

  UNREFERENCED_PARAMETER(Device);
  UNREFERENCED_PARAMETER(FileObject);

  Rio500_DbgPrint(3, ("EvtDeviceFileCreate - begins\n"));

  PAGED_CODE();

  // nothing to do here, we pre-selected out pipes in SelectInterfaces
  WdfRequestComplete(Request, status);

  Rio500_DbgPrint(3, ("EvtDeviceFileCreate - ends\n"));
  return;
}

VOID
Rio500_EvtIoDeviceControl(
  _In_ WDFQUEUE   Queue,
  _In_ WDFREQUEST Request,
  _In_ size_t     OutputBufferLength,
  _In_ size_t     InputBufferLength,
  _In_ ULONG      IoControlCode
)
/*++

Routine Description:

    This event is called when the framework receives IRP_MJ_DEVICE_CONTROL
    requests from the system.

Arguments:

    Queue - Handle to the framework queue object that is associated
            with the I/O request.
    Request - Handle to a framework request object.

    OutputBufferLength - length of the request's output buffer,
                        if an output buffer is available.
    InputBufferLength - length of the request's input buffer,
                        if an input buffer is available.

    IoControlCode - the driver-defined or system-defined I/O control code
                    (IOCTL) that is associated with the request.
Return Value:

    VOID

--*/
{
  WDFDEVICE             device;
  PVOID                 ioBuffer;
	PRIO_IOCTL_BLOCK      ioBufferRio;
	size_t                bufLength;
  NTSTATUS              status;
  PDEVICE_CONTEXT       pDevContext;
  ULONG                 length = 0;
	URB                   urb;
	PMDL                  pMdl = NULL;
  WDF_MEMORY_DESCRIPTOR memoryDesc;
#ifdef _WIN64
  BOOLEAN               isWowRequest = FALSE;
#endif // _WIN64

  UNREFERENCED_PARAMETER(OutputBufferLength);
  UNREFERENCED_PARAMETER(InputBufferLength);

  Rio500_DbgPrint(3, ("Entered Rio500_DispatchDevCtrl\n"));

  PAGED_CODE();

  //
  // initialize variables
  //
  device = WdfIoQueueGetDevice(Queue);
  pDevContext = GetDeviceContext(device);

  switch(IoControlCode) {
    case IOCTL_RIO500_RESET_PIPE:
      status = ResetPipe(pDevContext->ReadPipe);
      if (NT_SUCCESS(status)) {
        status = ResetPipe(pDevContext->WritePipe);
      }
      break;

    case IOCTL_RIO500_GET_CONFIG_DESCRIPTOR:
      if (pDevContext->UsbConfigurationDescriptor) {
        length = pDevContext->UsbConfigurationDescriptor->wTotalLength;

        status = WdfRequestRetrieveOutputBuffer(Request, length, &ioBuffer, &bufLength);
        if (!NT_SUCCESS(status)) {
          Rio500_DbgPrint(1, ("WdfRequestRetrieveInputBuffer failed\n"));
          break;
        }

        RtlCopyMemory(ioBuffer, pDevContext->UsbConfigurationDescriptor, length);
        status = STATUS_SUCCESS;
      } else {
        status = STATUS_INVALID_DEVICE_STATE;
      }
      break;

    case IOCTL_RIO500_RESET_DEVICE:
      status = ResetDevice(device);
      break;

    case IOCTL_RIO500_RIO_COMMAND:
#ifdef _WIN64
      if (IoIs32bitProcess(NULL)) {
        bufLength = sizeof(RIO_IOCTL_BLOCK) - sizeof(DWORD32);
        isWowRequest = TRUE;
      } else
#endif // _WIN64
      {
        bufLength = sizeof(RIO_IOCTL_BLOCK);
      }
	    status = WdfRequestRetrieveInputBuffer(
		    Request,
		    bufLength,
		    &ioBufferRio,
		    NULL
	    );

      if (NT_SUCCESS(status)) {
#ifdef _WIN64
        if (isWowRequest) {
          ioBuffer = (PVOID)ioBufferRio->MsgData.DataWow;
        } else
#endif // _WIN64
        {
          ioBuffer = ioBufferRio->MsgData.Data;
        }
		    if (ioBufferRio->MsgLength != 0 && ioBuffer != NULL) {
			    pMdl = IoAllocateMdl(
            ioBuffer,
            ioBufferRio->MsgLength,
            FALSE,
            FALSE,
            NULL
          );
			    MmProbeAndLockPages(pMdl, (KPROCESSOR_MODE)KernelMode, IoModifyAccess);
		    }

		    memset(&urb, 0, sizeof(urb));
		    urb.UrbControlVendorClassRequest.Hdr.Length = sizeof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST);
		    urb.UrbControlVendorClassRequest.Hdr.Function = URB_FUNCTION_VENDOR_DEVICE;
		    urb.UrbControlVendorClassRequest.TransferBuffer = NULL;
		    urb.UrbControlVendorClassRequest.RequestTypeReservedBits = 0x00;
		    urb.UrbControlVendorClassRequest.TransferBufferLength = ioBufferRio->MsgLength;
		    urb.UrbControlVendorClassRequest.TransferBufferMDL = pMdl;
		    urb.UrbControlVendorClassRequest.Request = ioBufferRio->RequestCode;
		    urb.UrbControlVendorClassRequest.Value = ioBufferRio->MsgValue;
		    urb.UrbControlVendorClassRequest.Index = ioBufferRio->MsgIndex;
		    urb.UrbControlVendorClassRequest.TransferFlags = USBD_SHORT_TRANSFER_OK;
		    if (ioBufferRio->RequestType & 0x80) {
			    urb.UrbControlVendorClassRequest.TransferFlags |= USBD_TRANSFER_DIRECTION_IN;
		    }
		    urb.UrbControlVendorClassRequest.UrbLink = NULL;
			
		    WDF_MEMORY_DESCRIPTOR_INIT_BUFFER(&memoryDesc, &urb, sizeof(urb));
		    status = WdfIoTargetSendInternalIoctlOthersSynchronously(
			    WdfDeviceGetIoTarget(device),
			    Request,
			    IOCTL_INTERNAL_USB_SUBMIT_URB,
			    &memoryDesc,
			    NULL,
			    NULL,
			    NULL,
			    NULL
		    );

		    if (pMdl != NULL) {
			    MmUnlockPages(pMdl);
			    IoFreeMdl(pMdl);
		    }
	    }
	    break;

    default:
      status = STATUS_INVALID_DEVICE_REQUEST;
      break;
  }

  WdfRequestCompleteWithInformation(Request, status, length);

  Rio500_DbgPrint(3, ("Exit Rio500_DispatchDevCtrl\n"));
  return;
}

VOID
Rio500_EvtIoRead(
  _In_ WDFQUEUE   Queue,
  _In_ WDFREQUEST Request,
  _In_ size_t     Length
)
/*++

Routine Description:

    Called by the framework when it receives Read requests.

Arguments:

    Queue - Default queue handle
    Request - Handle to the read/write request
    Lenght - Length of the data buffer associated with the request.
                 The default property of the queue is to not dispatch
                 zero lenght read & write requests to the driver and
                 complete is with status success. So we will never get
                 a zero length request.

Return Value:

    VOID

--*/
{
  WDFUSBPIPE               pipe;
  WDF_USB_PIPE_INFORMATION pipeInfo;

  PAGED_CODE();

  //
  // Get the pipe associate with this request.
  //
  WDFDEVICE device = WdfIoQueueGetDevice(Queue);
  PDEVICE_CONTEXT pDevContext = GetDeviceContext(device);
  
  pipe = pDevContext->ReadPipe;
  if (pipe == NULL) {
    Rio500_DbgPrint(1, ("Read pipe handle is NULL\n"));
    WdfRequestCompleteWithInformation(Request, STATUS_INVALID_PARAMETER, 0);
    return;
  }
  WDF_USB_PIPE_INFORMATION_INIT(&pipeInfo);
  WdfUsbTargetPipeGetInformation(pipe, &pipeInfo);

  if (WdfUsbPipeTypeBulk == pipeInfo.PipeType) {
    ReadWriteBulkEndPoints(Queue, Request, (ULONG)Length, WdfRequestTypeRead);
    return;
  } 

  Rio500_DbgPrint(1, ("ISO transfer is not supported for buffered I/O transfer\n"));
  WdfRequestCompleteWithInformation(Request, STATUS_INVALID_DEVICE_REQUEST, 0);
  return;
}

VOID
Rio500_EvtIoWrite(
  _In_ WDFQUEUE   Queue,
  _In_ WDFREQUEST Request,
  _In_ size_t     Length
)
/*++

Routine Description:

    Called by the framework when it receives Write requests.

Arguments:

    Queue - Default queue handle
    Request - Handle to the read/write request
    Lenght - Length of the data buffer associated with the request.
                 The default property of the queue is to not dispatch
                 zero lenght read & write requests to the driver and
                 complete is with status success. So we will never get
                 a zero length request.

Return Value:

    VOID

--*/
{
  WDFUSBPIPE               pipe;
  WDF_USB_PIPE_INFORMATION pipeInfo;

  PAGED_CODE();

  //
  // Get the pipe associate with this request.
  //
  WDFDEVICE device = WdfIoQueueGetDevice(Queue);
  PDEVICE_CONTEXT pDevContext = GetDeviceContext(device);

  pipe = pDevContext->WritePipe;
  if (pipe == NULL) {
    Rio500_DbgPrint(1, ("Write pipe handle is NULL\n"));
    WdfRequestCompleteWithInformation(Request, STATUS_INVALID_PARAMETER, 0);
    return;
  }
  WDF_USB_PIPE_INFORMATION_INIT(&pipeInfo);
  WdfUsbTargetPipeGetInformation(pipe, &pipeInfo);

  if (WdfUsbPipeTypeBulk == pipeInfo.PipeType) {
    ReadWriteBulkEndPoints(Queue, Request, (ULONG)Length, WdfRequestTypeWrite);
    return;
  } 

  Rio500_DbgPrint(1, ("ISO transfer is not supported for buffered I/O transfer\n"));
  WdfRequestCompleteWithInformation(Request, STATUS_INVALID_DEVICE_REQUEST, 0);
  return;
}

VOID
Rio500_EvtIoStop(
  _In_ WDFQUEUE   Queue,
  _In_ WDFREQUEST Request,
  _In_ ULONG      ActionFlags
)
/*++

Routine Description:

This callback is invoked on every inflight request when the device
is suspended or removed. Since our inflight read and write requests
are actually pending in the target device, we will just acknowledge
its presence. Until we acknowledge, complete, or requeue the requests
framework will wait before allowing the device suspend or remove to
proceeed. When the underlying USB stack gets the request to suspend or
remove, it will fail all the pending requests.

Arguments:

Return Value:

    VOID

--*/
{
	UNREFERENCED_PARAMETER(Queue);

	if (ActionFlags & WdfRequestStopActionSuspend) {
		WdfRequestStopAcknowledge(Request, FALSE); // Don't requeue
	} else if (ActionFlags & WdfRequestStopActionPurge) {
		WdfRequestCancelSentRequest(Request);
	}

	return;
}

NTSTATUS
ResetPipe(
  _In_ WDFUSBPIPE Pipe
)
/*++

Routine Description:

    This routine resets the pipe.

Arguments:

    Pipe - framework pipe handle

Return Value:

    NT status value

--*/
{
  NTSTATUS status;

  PAGED_CODE();

  //
  // This routine synchronously submits a URB_FUNCTION_RESET_PIPE
  // request down the stack.
  //
  status = WdfUsbTargetPipeResetSynchronously(
    Pipe,
    WDF_NO_HANDLE, // WDFREQUEST
    NULL  // PWDF_REQUEST_SEND_OPTIONS
  );

  if (NT_SUCCESS(status)) {
    Rio500_DbgPrint(3, ("ResetPipe - success\n"));
    status = STATUS_SUCCESS;
  } else {
    Rio500_DbgPrint(1, ("ResetPipe - failed\n"));
  }

  return status;
}

VOID
StopAllPipes(
  _In_ PDEVICE_CONTEXT DeviceContext
)
{
  UCHAR count,i;

  count = DeviceContext->NumberConfiguredPipes;
  for (i = 0; i < count; i++) {
    WDFUSBPIPE pipe;
    pipe = WdfUsbInterfaceGetConfiguredPipe(
      DeviceContext->UsbInterface,
      i, // PipeIndex,
      NULL
    );
    WdfIoTargetStop(WdfUsbTargetPipeGetIoTarget(pipe), WdfIoTargetCancelSentIo);
  }
}

VOID
StartAllPipes(
  _In_ PDEVICE_CONTEXT DeviceContext
)
{
  NTSTATUS status;
  UCHAR count, i;

  count = DeviceContext->NumberConfiguredPipes;
  for (i = 0; i < count; i++) {
    WDFUSBPIPE pipe;
    pipe = WdfUsbInterfaceGetConfiguredPipe(
      DeviceContext->UsbInterface,
      i, // PipeIndex,
      NULL
    );
    status = WdfIoTargetStart(WdfUsbTargetPipeGetIoTarget(pipe));
    if (!NT_SUCCESS(status)) {
      Rio500_DbgPrint(1, ("StartAllPipes - failed pipe #%d\n", i));
    }
  }
}

NTSTATUS
ResetDevice(
  _In_ WDFDEVICE Device
)
/*++

Routine Description:

    This routine calls WdfUsbTargetDeviceResetPortSynchronously to reset the device if it's still
    connected.

Arguments:

    Device - Handle to a framework device

Return Value:

    NT status value

--*/
{
  PDEVICE_CONTEXT pDeviceContext;
  NTSTATUS status;

  Rio500_DbgPrint(3, ("ResetDevice - begins\n"));

  PAGED_CODE();

  pDeviceContext = GetDeviceContext(Device);
    
  //
  // A reset-device
  // request will be stuck in the USB until the pending transactions
  // have been canceled. Similarly, if there are pending tranasfers on the BULK
  // _In_/OUT pipe cancel them.
  // To work around this issue, the driver should stop the continuous reader
  // (by calling WdfIoTargetStop) before resetting the device, and restart the
  // continuous reader (by calling WdfIoTargetStart) after the request completes.
  //
  StopAllPipes(pDeviceContext);
    
  //
  // It may not be necessary to check whether device is connected before
  // resetting the port.
  //
  status = WdfUsbTargetDeviceIsConnectedSynchronous(pDeviceContext->WdfUsbTargetDevice);

  if (NT_SUCCESS(status)) {
    status = WdfUsbTargetDeviceResetPortSynchronously(pDeviceContext->WdfUsbTargetDevice);
  }

  StartAllPipes(pDeviceContext);
    
  Rio500_DbgPrint(3, ("ResetDevice - ends\n"));
  return status;
}
