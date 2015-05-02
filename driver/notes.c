/*++

RioUsb.sys decompilation notes. Just notes, not to be built.

--*/

#ifdef _JUST_NOTES

// Struct field offsets

typedef struct DECLSPEC_ALIGN(MEMORY_ALLOCATION_ALIGNMENT) _IRP {
	CSHORT Type; // 00
	USHORT Size; // 02

	//
	// Define the common fields used to control the IRP.
	//

	//
	// Define a pointer to the Memory Descriptor List (MDL) for this I/O
	// request.  This field is only used if the I/O is "direct I/O".
	//

	PMDL MdlAddress; // 04

	//
	// Flags word - used to remember various flags.
	//

	ULONG Flags; // 08

	//
	// The following union is used for one of three purposes:
	//
	//    1. This IRP is an associated IRP.  The field is a pointer to a master
	//       IRP.
	//
	//    2. This is the master IRP.  The field is the count of the number of
	//       IRPs which must complete (associated IRPs) before the master can
	//       complete.
	//
	//    3. This operation is being buffered and the field is the address of
	//       the system space buffer.
	//

	union { // 0c
		struct _IRP *MasterIrp;
		__volatile LONG IrpCount;
		PVOID SystemBuffer;
	} AssociatedIrp;

	//
	// Thread list entry - allows queueing the IRP to the thread pending I/O
	// request packet list.
	//

	LIST_ENTRY ThreadListEntry; // 10

	//
	// I/O status - final status of operation.
	//

	IO_STATUS_BLOCK IoStatus; // 18

	//
	// Requestor mode - mode of the original requestor of this operation.
	//

	KPROCESSOR_MODE RequestorMode; // 20

	//
	// Pending returned - TRUE if pending was initially returned as the
	// status for this packet.
	//

	BOOLEAN PendingReturned; // 21

	//
	// Stack state information.
	//

	CHAR StackCount; // 22
	CHAR CurrentLocation; // 23

	//
	// Cancel - packet has been canceled.
	//

	BOOLEAN Cancel; // 24

	//
	// Cancel Irql - Irql at which the cancel spinlock was acquired.
	//

	KIRQL CancelIrql; // 25

	//
	// ApcEnvironment - Used to save the APC environment at the time that the
	// packet was initialized.
	//

	CCHAR ApcEnvironment; // 26

	//
	// Allocation control flags.
	//

	UCHAR AllocationFlags; // 27

	//
	// User parameters.
	//

	PIO_STATUS_BLOCK UserIosb; // 28
	PKEVENT UserEvent; // 2c
	union {
		struct {
			union {
				PIO_APC_ROUTINE UserApcRoutine;
				PVOID IssuingProcess;
			};
			PVOID UserApcContext;
		} AsynchronousParameters;
		LARGE_INTEGER AllocationSize;
	} Overlay; // 30

	//
	// CancelRoutine - Used to contain the address of a cancel routine supplied
	// by a device driver when the IRP is in a cancelable state.
	//

	__volatile PDRIVER_CANCEL CancelRoutine; // 38

	//
	// Note that the UserBuffer parameter is outside of the stack so that I/O
	// completion can copy data back into the user's address space without
	// having to know exactly which service was being invoked.  The length
	// of the copy is stored in the second half of the I/O status block. If
	// the UserBuffer field is NULL, then no copy is performed.
	//

	PVOID UserBuffer; // 3c

	//
	// Kernel structures
	//
	// The following section contains kernel structures which the IRP needs
	// in order to place various work information in kernel controller system
	// queues.  Because the size and alignment cannot be controlled, they are
	// placed here at the end so they just hang off and do not affect the
	// alignment of other fields in the IRP.
	//

	union {
		struct {
			union {
				//
				// DeviceQueueEntry - The device queue entry field is used to
				// queue the IRP to the device driver device queue.
				//

				KDEVICE_QUEUE_ENTRY DeviceQueueEntry; // 40

				struct {
					//
					// The following are available to the driver to use in
					// whatever manner is desired, while the driver owns the
					// packet.
					//

					PVOID DriverContext[4]; // 40
				};
			};

			//
			// Thread - pointer to caller's Thread Control Block.
			//

			PETHREAD Thread; // 50

			//
			// Auxiliary buffer - pointer to any auxiliary buffer that is
			// required to pass information to a driver that is not contained
			// in a normal buffer.
			//

			PCHAR AuxiliaryBuffer; // 54

			//
			// The following unnamed structure must be exactly identical
			// to the unnamed structure used in the minipacket header used
			// for completion queue entries.
			//

			struct {

			//
			// List entry - used to queue the packet to completion queue, among
			// others.
			//

			LIST_ENTRY ListEntry; // 58

			union { // 60
				//
				// Current stack location - contains a pointer to the current
				// IO_STACK_LOCATION structure in the IRP stack.  This field
				// should never be directly accessed by drivers.  They should
				// use the standard functions.
				//

				struct _IO_STACK_LOCATION *CurrentStackLocation; // 60

				//
				// Minipacket type.
				//

				ULONG PacketType;
			};
		};

		//
		// Original file object - pointer to the original file object
		// that was used to open the file.  This field is owned by the
		// I/O system and should not be used by any other drivers.
		//

		PFILE_OBJECT OriginalFileObject;

		} Overlay;

		//
		// APC - This APC control block is used for the special kernel APC as
		// well as for the caller's APC, if one was specified in the original
		// argument list.  If so, then the APC is reused for the normal APC for
		// whatever mode the caller was in and the "special" routine that is
		// invoked before the APC gets control simply deallocates the IRP.
		//

		KAPC Apc;

		//
		// CompletionKey - This is the key that is used to distinguish
		// individual I/O operations initiated on a single file handle.
		//

		PVOID CompletionKey;
	} Tail;

} IRP;

typedef IRP *PIRP;

typedef struct _DEVICE_OBJECT {
	CSHORT                      Type; // 00
	USHORT                      Size; // 02
	LONG                        ReferenceCount; // 04
	struct _DRIVER_OBJECT  *DriverObject; // 08
	struct _DEVICE_OBJECT  *NextDevice; // 0c
	struct _DEVICE_OBJECT  *AttachedDevice; // 10
	struct _IRP  *CurrentIrp; // 14
	PIO_TIMER                   Timer; // 18
	ULONG                       Flags; // 1c
	ULONG                       Characteristics; // 20
	__volatile PVPB             Vpb; // 24
	PVOID                       DeviceExtension; // 28
	DEVICE_TYPE                 DeviceType;
	CCHAR                       StackSize;
	union {
		LIST_ENTRY         ListEntry;
		WAIT_CONTEXT_BLOCK Wcb;
	} Queue;
	ULONG                       AlignmentRequirement;
	KDEVICE_QUEUE               DeviceQueue;
	KDPC                        Dpc;
	ULONG                       ActiveThreadCount;
	PSECURITY_DESCRIPTOR        SecurityDescriptor;
	KEVENT                      DeviceLock;
	USHORT                      SectorSize;
	USHORT                      Spare1;
	struct _DEVOBJ_EXTENSION  *  DeviceObjectExtension;
	PVOID                       Reserved;
} DEVICE_OBJECT, *PDEVICE_OBJECT;


// IOCTL handler

NTSTATUS BulkUsb_ProcessIOCTL(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp)
{
	//arg_0 = dword ptr  8   .. DeviceObject
	//arg_4 = dword ptr  0Ch .. Irp

	//push    ebp
	//mov     ebp, esp
	//push    ebx
	//push    esi
	//push    edi
	//mov     edi, [ebp + arg_0]
	//push    edi
	//call    _BulkUsb_IncrementIoCount@4; BulkUsb_IncrementIoCount(x)
	//mov     esi, [edi + 28h]
	//push    edi
	//call    _BulkUsb_CanAcceptIoRequests@4; BulkUsb_CanAcceptIoRequests(x)
	//test    al, al
	//jnz     short loc_11120

	// edi := DeviceObject;
	BulkUsb_IncrementIoCount(DeviceObject);
	// esi := DeviceObject->DeviceExtension;
	PVOID pExt = DeviceObject->DeviceExtension;
	if (!BulkUsb_CanAcceptIoRequests(DeviceObject))
	{
		//mov     ebx, [ebp + arg_4]
		//mov     eax, 0C0000056h
		//and     dword ptr[ebx + 1Ch], 0
		//mov[ebx + 18h], eax
		//jmp     loc_11242

		// ebx := Irp
		// eax := 0C0000056h
		Irp->IoStatus.Information = 0;
		Irp->IoStatus.Status = 0xC0000056;

		//loc_11244:
		//xor     dl, dl
		//mov     ecx, ebx
		//call    __imp_@IofCompleteRequest@8; IofCompleteRequest(x, x)
		//push[ebp + arg_0]
		//call    _BulkUsb_DecrementIoCount@4; BulkUsb_DecrementIoCount(x)
		//mov     eax, edi
		//pop     edi
		//pop     esi
		//pop     ebx
		//pop     ebp
		//retn    8
		IofCompleteRequest(Irp, 0);
		BulkUsb_DecrementIoCount(DeviceObject);
		return Irp->IoStatus.Status;
	}

	//loc_11120:
	//mov     ebx, [ebp + arg_4]
	//xor     eax, eax
	//mov     ecx, [ebx + 60h]
	//mov     edi, [ebx + 0Ch]
	//mov[ebx + 18h], eax
	//mov[ebx + 1Ch], eax
	//mov     ecx, [ecx + 0Ch]
	//sub     ecx, 220000h
	//jz      loc_11212

	// ebx := Irp
	// eax := 0
	PIO_STACK_LOCATION pStackLoc = Irp->Tail.Overlay.CurrentStackLocation; // ecx
	PVOID pSysBuffer = Irp->AssociatedIrp.SystemBuffer; // edi
	Irp->IoStatus.Information = 0;
	Irp->IoStatus.Status = 0;

	ULONG uCtrlCode = pStackLoc->Parameters.DeviceIoControl.IoControlCode; // ecx
	uCtrlCode -= 0x220000;
	if (uCtrlCode == 0)
	{
		// IRRELEVANT
	}
	else
	{
		uCtrlCode -= 0x4;
		if (uCtrlCode == 0)
		{
			// IRRELEVANT
		}
		else
		{
			uCtrlCode -= 0x8;
			if (uCtrlCode == 0) // ioctl code was 0x22000C
			{
				//loc_11158:
				//mov     ecx, [edi + 8]
				//mov[ebp + arg_4], eax
				//cmp     ecx, eax
				//jz      short loc_11189

				Irp = NULL;
				if (((DWORD*)pSysBuffer)[2] != 0) // ecx == eax
				{
					//mov     dx, [edi + 6]
					//cmp     dx, ax
					//jz      short loc_11189

					if (((WORD*)pSysBuffer)[3] != 0) // dx == ax
					{
						//push    eax
						//push    eax
						//push    eax
						//movzx   eax, dx
						//push    eax
						//push    ecx
						//call    __imp__IoAllocateMdl@20; IoAllocateMdl(x, x, x, x, x)
						//push    2
						//push    1
						//push    eax
						//mov[ebp + arg_4], eax
						//call    __imp__MmProbeAndLockPages@12; MmProbeAndLockPages(x, x, x)
						//xor     eax, eax

						PMDL pMdl = IoAllocateMdl(((DWORD*)pSysBuffer)[2], ((WORD*)pSysBuffer)[3], FALSE, FALSE, NULL);
						//Irp = (PIRP)pMdl; <- just a stack slot reuse
						MmProbeAndLockPages(pMdl, (KPROCESSOR_MODE)WdfKernelMode, IoModifyAccess);
					}
				}

				//loc_11189:
				//push    206D6457h
				//push    50h
				//push    eax
				//call    __imp__ExAllocatePoolWithTag@12; ExAllocatePoolWithTag(x, x, x)
				//mov     esi, eax

				PURB pUrb = (PURB)ExAllocatePoolWithTag(NonPagedPool, 0x50, 0x206D6457); // esi

				//push    esi
				//mov     word ptr[esi + 2], 17h
				//mov     word ptr[esi], 50h
				//movzx   eax, word ptr[edi + 6]
				//and     dword ptr[esi + 1Ch], 0
				//and     byte ptr[esi + 48h], 0
				//mov[esi + 18h], eax
				//mov     eax, [ebp + arg_4]
				//mov[esi + 20h], eax
				//mov     al, [edi + 1]
				//mov[esi + 49h], al
				//mov     ax, [edi + 2]
				//mov[esi + 4Ah], ax
				//mov     ax, [edi + 4]
				//mov[esi + 4Ch], ax

				//struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST {
				//	struct _URB_HEADER Hdr;     // 00
				//	PVOID Reserved;             // 10
				//	ULONG TransferFlags;        // 14
				//	ULONG TransferBufferLength; // 18
				//	PVOID TransferBuffer;       // 1c
				//	PMDL TransferBufferMDL;     // 20
				//	struct _URB *UrbLink; // Reserved     // 24
				//	struct _URB_HCD_AREA hca; // Reserved // 28
				//	UCHAR RequestTypeReservedBits;  // 48
				//	UCHAR Request;    // 49
				//	USHORT Value;     // 4a
				//	USHORT Index;     // 4c
				//	USHORT Reserved1; // 4e
				//};
				pUrb->UrbControlVendorClassRequest.Hdr.Length = 0x50;
				pUrb->UrbControlVendorClassRequest.Hdr.Function = URB_FUNCTION_VENDOR_DEVICE;
				pUrb->UrbControlVendorClassRequest.TransferBuffer = NULL;
				pUrb->UrbControlVendorClassRequest.RequestTypeReservedBits = 0x00;
				pUrb->UrbControlVendorClassRequest.TransferBufferLength = (DWORD)((WORD*)pSysBuffer)[3];
				pUrb->UrbControlVendorClassRequest.TransferBufferMDL = pMdl;
				pUrb->UrbControlVendorClassRequest.Request = ((BYTE*)pSysBuffer)[1];
				pUrb->UrbControlVendorClassRequest.Value = ((WORD*)pSysBuffer)[1];
				pUrb->UrbControlVendorClassRequest.Index = ((WORD*)pSysBuffer)[2];

				//push[ebp + arg_0]
				//movzx   eax, byte ptr[edi]
				//and     dword ptr[esi + 24h], 0
				//shr     eax, 7
				//mov[esi + 14h], eax
				//call    _BulkUsb_CallUSBD@8; BulkUsb_CallUSBD(x, x)

				pUrb->UrbControlVendorClassRequest.TransferFlags = (DWORD)((BYTE*)pSysBuffer)[0] >> 7;
				pUrb->UrbControlVendorClassRequest.UrbLink = NULL;
				NTSTATUS ret = BulkUsb_CallUSBD(DeviceObject, pUrb); // edi

				//push    esi
				//mov     edi, eax
				//call    __imp__ExFreePool@4; ExFreePool(x)

				ExFreePool(pAlloc);

				//cmp[ebp + arg_4], 0
				//jz      short loc_11244

				if (pMdl != NULL)
				{
					//push[ebp + arg_4]
					//call    __imp__MmUnlockPages@4; MmUnlockPages(x)
					//push[ebp + arg_4]
					//call    __imp__IoFreeMdl@4; IoFreeMdl(x)
					//jmp     short loc_11244

					MmUnlockPages(pMdl);
					IoFreeMdl(pMdl);
				}
			}
		}
	}

	//loc_11244:
	//xor     dl, dl
	//mov     ecx, ebx
	//call    __imp_@IofCompleteRequest@8; IofCompleteRequest(x, x)
	//push[ebp + arg_0]
	//call    _BulkUsb_DecrementIoCount@4; BulkUsb_DecrementIoCount(x)
	//mov     eax, edi
	//pop     edi
	//pop     esi
	//pop     ebx
	//pop     ebp
	//retn    8

	IofCompleteRequest(Irp, 0);
	BulkUsb_DecrementIoCount(DeviceObject);
	return ret;
}

NTSTATUS BulkUsb_CallUSBD(IN PDEVICE_OBJECT DeviceObject, IN PURB Urb)
{
	//var_18 = dword ptr - 18h
	//var_8 = dword ptr - 8
	//arg_0 = dword ptr  8    DeviceObject
	//arg_4 = dword ptr  0Ch  Urb

	//push    ebp
	//mov     ebp, esp
	//sub     esp, 18h
	//mov     eax, [ebp + arg_0]

	// eax := DeviceObject

	//push    esi
	//push    edi
	//xor     edi, edi
	//mov     esi, [eax + 28h]

	// edi := NULL
	// esi := DeviceObject->DeviceExtension

	//push    edi
	//lea     eax, [ebp + var_18]

	// eax := &var_18

	//push    edi
	//push    eax
	//call    __imp__KeInitializeEvent@12; KeInitializeEvent(x, x, x)

	KEVENT Event; // var_18
	KeInitializeEvent(&Event, NotificationEvent, FALSE);

	//lea     eax, [ebp + var_8]

	// eax := &var_8

	//push    eax
	//lea     eax, [ebp + var_18]

	// eax := &var_18

	//push    eax
	//push    1
	//push    edi
	//push    edi
	//push    edi
	//push    edi
	//push    dword ptr[esi]
	//push    220003h
	//call    __imp__IoBuildDeviceIoControlRequest@36; IoBuildDeviceIoControlRequest(x, x, x, x, x, x, x, x, x)

	IO_STATUS_BLOCK StatusBlock; // var_8
	PIRP Irp = IoBuildDeviceIoControlRequest(
		IOCTL_INTERNAL_USB_SUBMIT_URB,
		DeviceObject->DeviceExtension->TopOfStackDeviceObject,
		NULL,
		NULL,
		NULL,
		NULL,
		TRUE,
		&Event,
		&StatusBlock,
	);

	//mov     ecx, [eax + 60h]
	//mov     edx, [ebp + arg_4]

	// ecx := Irp->Tail.Overlay.CurrentStackLocation
	// edx := Urb

	//mov[ecx - 20h], edx

	PIO_STACK_LOCATION nextStack = IoGetNextIrpStackLocation(Irp);
	nextStack->Parameters.Others.Argument1 = Urb;

	//mov     ecx, [esi]
	//mov     edx, eax
	//call    __imp_@IofCallDriver@8; IofCallDriver(x, x)
	//cmp     eax, 103h
	//jnz     short loc_10580

	NTSTATUS status = IofCallDriver(DeviceObject->DeviceExtension->TopOfStackDeviceObject, Irp);
	if (status == STATUS_PENDING)
	{
		//push    edi
		//push    edi
		//push    edi
		//lea     eax, [ebp + var_18]
		//push    5
		//push    eax
		//call    __imp__KeWaitForSingleObject@20; KeWaitForSingleObject(x, x, x, x, x)
		//jmp     short loc_10583

		KeWaitForSingleObject(
			&Event,
			(KWAIT_REASON)Suspended,
			(KPROCESSOR_MODE)KernelMode,
			FALSE,
			0,
		);
	}

	//loc_10583:
	//mov     eax, [ebp + var_8]
	//pop     edi
	//pop     esi
	//leave
	//retn    8

	return StatusBlock.Status;
}

#endif // _JUST_NOTES
