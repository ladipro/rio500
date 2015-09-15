/*++

Copyright (c) Microsoft Corporation.  All rights reserved.

    THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY
    KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A PARTICULAR
    PURPOSE.

Module Name:

    bulkrwr.c

Abstract:

    This file has routines to perform reads and writes.
    The read and writes are for bulk transfers.

Environment:

    Kernel mode

--*/

#include "private.h"

VOID
ReadWriteBulkEndPoints(
  _In_ WDFQUEUE         Queue,
  _In_ WDFREQUEST       Request,
  _In_ ULONG            Length,
  _In_ WDF_REQUEST_TYPE RequestType
)
/*++

Routine Description:

    This callback is invoked when the framework received  WdfRequestTypeRead or
    WdfRequestTypeWrite request. This read/write is performed in stages of
    maximum transfer size. Once a stage of transfer is complete, then the
    request is circulated again, until the requested length of transfer is
    performed.

Arguments:

    Queue - Handle to the framework queue object that is associated
            with the I/O request.

    Request - Handle to a framework request object. This one represents
              the WdfRequestTypeRead/WdfRequestTypeWrite IRP received by the framework.

    Length - Length of the input/output buffer.

Return Value:

    VOID

--*/
{
  PMDL                     newMdl = NULL, requestMdl = NULL;
  PURB                     urb = NULL;
  WDFMEMORY                urbMemory;
  ULONG                    totalLength = Length;
  ULONG                    stageLength = 0;
  ULONG                    urbFlags = 0;
  NTSTATUS                 status;
  ULONG_PTR                virtualAddress = 0;
  PREQUEST_CONTEXT         rwContext = NULL;
  WDFUSBPIPE               pipe;
  WDF_USB_PIPE_INFORMATION pipeInfo;
  WDF_OBJECT_ATTRIBUTES    objectAttribs;
  USBD_PIPE_HANDLE         usbdPipeHandle;
  PDEVICE_CONTEXT          deviceContext;
  ULONG                    maxTransferSize;

  Rio500_DbgPrint(3, ("Rio500_DispatchReadWrite - begins\n"));

  //
  // First validate input parameters.
  //
  deviceContext = GetDeviceContext(WdfIoQueueGetDevice(Queue));

  if (totalLength > deviceContext->MaximumTransferSize) {
    Rio500_DbgPrint(1, ("Transfer length > circular buffer\n"));
    status = STATUS_INVALID_PARAMETER;
    goto Exit;
  }

  if (RequestType != WdfRequestTypeRead && RequestType != WdfRequestTypeWrite) {
    Rio500_DbgPrint(1, ("RequestType has to be either Read or Write\n"));
    status = STATUS_INVALID_PARAMETER;
    goto Exit;
  }

  //
  // Get the pipe associate with this request.
  //
  if (RequestType == WdfRequestTypeWrite) {
    pipe = deviceContext->WritePipe;
  } else {
    pipe = deviceContext->ReadPipe;
  }
  WDF_USB_PIPE_INFORMATION_INIT(&pipeInfo);
  WdfUsbTargetPipeGetInformation(pipe, &pipeInfo);

  if (WdfUsbPipeTypeBulk != pipeInfo.PipeType) {
    Rio500_DbgPrint(1, ("Usbd pipe type is not bulk\n"));
    status = STATUS_INVALID_DEVICE_REQUEST;
    goto Exit;
  }

  rwContext = GetRequestContext(Request);

  if (RequestType == WdfRequestTypeRead) {
    status = WdfRequestRetrieveOutputWdmMdl(Request, &requestMdl);
    if (!NT_SUCCESS(status)) {
      Rio500_DbgPrint(1, ("WdfRequestRetrieveOutputWdmMdl failed %x\n", status));
      goto Exit;
    }

    urbFlags |= USBD_TRANSFER_DIRECTION_IN;
    rwContext->Read = TRUE;
    Rio500_DbgPrint(3, ("Read operation\n"));
  } else {
    status = WdfRequestRetrieveInputWdmMdl(Request, &requestMdl);
    if (!NT_SUCCESS(status)) {
      Rio500_DbgPrint(1, ("WdfRequestRetrieveInputWdmMdl failed %x\n", status));
      goto Exit;
    }

    urbFlags |= USBD_TRANSFER_DIRECTION_OUT;
    rwContext->Read = FALSE;
    Rio500_DbgPrint(3, ("Write operation\n"));
  }

  urbFlags |= USBD_SHORT_TRANSFER_OK;
  virtualAddress = (ULONG_PTR) MmGetMdlVirtualAddress(requestMdl);

  //
  // The transfer request is for totalLength.
  // We can perform a max of maxTransfersize in each stage.
  //
  maxTransferSize = GetMaxTransferSize();

  if (totalLength > maxTransferSize) {
    stageLength = maxTransferSize;
  } else {
    stageLength = totalLength;
  }

  newMdl = IoAllocateMdl(
    (PVOID)virtualAddress,
    totalLength,
    FALSE,
    FALSE,
    NULL
  );

  if (newMdl == NULL) {
      Rio500_DbgPrint(1, ("Failed to alloc mem for mdl\n"));
      status = STATUS_INSUFFICIENT_RESOURCES;
      goto Exit;
  }

  //
  // map the portion of user-buffer described by an mdl to another mdl
  //
  IoBuildPartialMdl(
    requestMdl,
    newMdl,
    (PVOID)virtualAddress,
    stageLength
  );

  WDF_OBJECT_ATTRIBUTES_INIT(&objectAttribs);
  objectAttribs.ParentObject = Request;

  status = WdfUsbTargetDeviceCreateUrb(
    deviceContext->WdfUsbTargetDevice,
    &objectAttribs,
    &urbMemory,
    &urb
  );

  if (!NT_SUCCESS(status)) {
    Rio500_DbgPrint(1, ("WdfUsbTargetDeviceCreateUrb failed %x\n", status));
    goto Exit;
  }

  usbdPipeHandle = WdfUsbTargetPipeWdmGetPipeHandle(pipe);

  UsbBuildInterruptOrBulkTransferRequest(
    urb,
    sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER),
    usbdPipeHandle,
    NULL,
    newMdl,
    stageLength,
    urbFlags,
    NULL
  );

  status = WdfUsbTargetPipeFormatRequestForUrb(pipe, Request, urbMemory, NULL);
  if (!NT_SUCCESS(status)) {
    Rio500_DbgPrint(1, ("Failed to format requset for urb\n"));
    status = STATUS_INSUFFICIENT_RESOURCES;
    goto Exit;
  }

  WdfRequestSetCompletionRoutine(Request, Rio500_EvtReadWriteCompletion, deviceContext);

  //
  // set REQUEST_CONTEXT  parameters.
  //
  rwContext->UrbMemory      = urbMemory;
  rwContext->Mdl            = newMdl;
  rwContext->Length         = totalLength - stageLength;
  rwContext->Numxfer        = 0;
  rwContext->VirtualAddress = virtualAddress + stageLength;

  if (!WdfRequestSend(Request, WdfUsbTargetPipeGetIoTarget(pipe), WDF_NO_SEND_OPTIONS)) {
    status = WdfRequestGetStatus(Request);
    NT_ASSERT(!NT_SUCCESS(status));
  }

Exit:
  if (!NT_SUCCESS(status)) {
    WdfRequestCompleteWithInformation(Request, status, 0);
    if (newMdl != NULL)  {
      IoFreeMdl(newMdl);
    }
  }

  Rio500_DbgPrint(3, ("Rio500_DispatchReadWrite - ends\n"));
}

VOID
Rio500_EvtReadWriteCompletion(
  _In_ WDFREQUEST                Request,
  _In_ WDFIOTARGET               Target,
  PWDF_REQUEST_COMPLETION_PARAMS CompletionParams,
  _In_ WDFCONTEXT                Context
)
/*++

Routine Description:

    This is the completion routine for reads/writes
    If the irp completes with success, we check if we
    need to recirculate this irp for another stage of
    transfer.

Arguments:

    Context - Driver supplied context
    Device - Device handle
    Request - Request handle
    Params - request completion params

Return Value:

    VOID

--*/
{
  PMDL             requestMdl;
  WDFUSBPIPE       pipe;
  ULONG            stageLength;
  NTSTATUS         status;
  PREQUEST_CONTEXT rwContext;
  PURB             urb;
  PCHAR            operation;
  ULONG            bytesReadWritten;
  ULONG            maxTransferSize;
  PDEVICE_CONTEXT  deviceContext;

  rwContext = GetRequestContext(Request);
  deviceContext = Context;

  if (rwContext->Read) {
    operation = "Read";
  } else {
    operation = "Write";
  }

  pipe = (WDFUSBPIPE)Target;
  status = CompletionParams->IoStatus.Status;

  if (!NT_SUCCESS(status)) {
    //
    // Queue a workitem to reset the pipe because the completion could be
    // running at DISPATCH_LEVEL.
    //
    QueuePassiveLevelCallback(WdfIoTargetGetDevice(Target), pipe);
    goto End;
  }

  urb = (PURB)WdfMemoryGetBuffer(rwContext->UrbMemory, NULL);
  bytesReadWritten = urb->UrbBulkOrInterruptTransfer.TransferBufferLength;
  rwContext->Numxfer += bytesReadWritten;

  //
  // If there is anything left to transfer.
  //
  if (rwContext->Length == 0) {
    //
    // this is the last transfer
    //
    WdfRequestSetInformation(Request, rwContext->Numxfer);
    goto End;
  }

  //
  // Start another transfer
  //
  Rio500_DbgPrint(3, ("Stage next %s transfer...\n", operation));

  //
  // The transfer request is for totalLength. 
  // We can perform a max of maxTransfersize in each stage.
  //
  maxTransferSize = GetMaxTransferSize();

  if (rwContext->Length > maxTransferSize) {
    stageLength = maxTransferSize;
  } else {
    stageLength = rwContext->Length;
  }

  //
  // Following call is required to free any mapping made on the partial MDL
  // and reset internal MDL state.
  //
  MmPrepareMdlForReuse(rwContext->Mdl);

  if (rwContext->Read) {
    status = WdfRequestRetrieveOutputWdmMdl(Request, &requestMdl);
    if (!NT_SUCCESS(status)) {
      Rio500_DbgPrint(1, ("WdfRequestRetrieveOutputWdmMdl for Read failed %x\n", status));
      goto End;
    }
  } else {
    status = WdfRequestRetrieveInputWdmMdl(Request, &requestMdl);
    if (!NT_SUCCESS(status)) {
      Rio500_DbgPrint(1, ("WdfRequestRetrieveInputWdmMdl for Write failed %x\n", status));
      goto End;
    }
  }

  IoBuildPartialMdl(
    requestMdl,
    rwContext->Mdl,
    (PVOID)rwContext->VirtualAddress,
    stageLength
  );

  //
  // reinitialize the urb
  //
  urb->UrbBulkOrInterruptTransfer.TransferBufferLength = stageLength;

  rwContext->VirtualAddress += stageLength;
  rwContext->Length -= stageLength;

  //
  // Format the request to send a URB to a USB pipe.
  //
  status = WdfUsbTargetPipeFormatRequestForUrb(
    pipe,
    Request,
    rwContext->UrbMemory,
    NULL
  );
  if (!NT_SUCCESS(status)) {
    Rio500_DbgPrint(1, ("Failed to format requset for urb\n"));
    status = STATUS_INSUFFICIENT_RESOURCES;
    goto End;
  }

  WdfRequestSetCompletionRoutine(Request, Rio500_EvtReadWriteCompletion, deviceContext);

  //
  // Send the request asynchronously.
  //
  if (!WdfRequestSend(Request, WdfUsbTargetPipeGetIoTarget(pipe), WDF_NO_SEND_OPTIONS)) {
    Rio500_DbgPrint(1, ("WdfRequestSend for %s failed\n", operation));
    status = WdfRequestGetStatus(Request);
    goto End;
  }

  //
  // Else when the request completes, this completion routine will be
  // called again.
  //
  return;

End:
  //
  // We are here because the request failed or some other call failed.
  // Dump the request context, complete the request and return.
  //
  DbgPrintRWContext(rwContext);

  IoFreeMdl(rwContext->Mdl);

  Rio500_DbgPrint(3, ("%s request completed with status 0x%x\n", operation, status));

  WdfRequestComplete(Request, status);
}

VOID
Rio500_EvtReadWriteWorkItem(
  _In_ WDFWORKITEM  WorkItem
)
{
  PWORKITEM_CONTEXT pItemContext;
  NTSTATUS status;

  Rio500_DbgPrint(3, ("ReadWriteWorkItem called\n"));

  pItemContext = GetWorkItemContext(WorkItem);

  status = ResetPipe(pItemContext->Pipe);
  if (!NT_SUCCESS(status)) {
    Rio500_DbgPrint(1, ("ResetPipe failed 0x%x\n", status));

    status = ResetDevice(pItemContext->Device);
    if (!NT_SUCCESS(status)) {
      Rio500_DbgPrint(1, ("ResetDevice failed 0x%x\n", status));
    }
  }

  WdfObjectDelete(WorkItem);
}

NTSTATUS
QueuePassiveLevelCallback(
  _In_ WDFDEVICE  Device,
  _In_ WDFUSBPIPE Pipe
)
/*++

Routine Description:

    This routine is used to queue workitems so that the callback
    functions can be executed at PASSIVE_LEVEL in the context of
    a system thread.

Arguments:

Return Value:

--*/
{
  NTSTATUS              status = STATUS_SUCCESS;
  PWORKITEM_CONTEXT     context;
  WDF_OBJECT_ATTRIBUTES attributes;
  WDF_WORKITEM_CONFIG   workitemConfig;
  WDFWORKITEM           hWorkItem;

  WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
  WDF_OBJECT_ATTRIBUTES_SET_CONTEXT_TYPE(&attributes, WORKITEM_CONTEXT);
  attributes.ParentObject = Device;

  WDF_WORKITEM_CONFIG_INIT(&workitemConfig, Rio500_EvtReadWriteWorkItem);

  status = WdfWorkItemCreate(
    &workitemConfig,
    &attributes,
    &hWorkItem
  );

  if (!NT_SUCCESS(status)) {
    return status;
  }

  context = GetWorkItemContext(hWorkItem);

  context->Device = Device;
  context->Pipe = Pipe;

  //
  // Execute this work item.
  //
  WdfWorkItemEnqueue(hWorkItem);

  return STATUS_SUCCESS;
}

VOID
DbgPrintRWContext(
  PREQUEST_CONTEXT rwContext
)
{
  UNREFERENCED_PARAMETER(rwContext);

  Rio500_DbgPrint(3, ("rwContext->UrbMemory       = %p\n", rwContext->UrbMemory));
  Rio500_DbgPrint(3, ("rwContext->Mdl             = %p\n", rwContext->Mdl));
  Rio500_DbgPrint(3, ("rwContext->Length          = %d\n", rwContext->Length));
  Rio500_DbgPrint(3, ("rwContext->Numxfer         = %d\n", rwContext->Numxfer));
  Rio500_DbgPrint(3, ("rwContext->VirtualAddress  = %p\n", (PVOID)rwContext->VirtualAddress));
}
