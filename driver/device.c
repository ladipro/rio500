/*++

    THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY
    KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A PARTICULAR
    PURPOSE.

Module Name:

    device.c

Abstract:

    USB device driver for the Diamond Rio 500 mp3 player.
    Plug and Play module. This file contains routines to handle pnp requests.

Environment:

    Kernel mode

--*/

#include "private.h"

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, Rio500_EvtDeviceAdd)
#pragma alloc_text(PAGE, Rio500_EvtDevicePrepareHardware)
#pragma alloc_text(PAGE, Rio500_EvtDeviceContextCleanup)
#pragma alloc_text(PAGE, ReadAndSelectDescriptors)
#pragma alloc_text(PAGE, ConfigureDevice)
#pragma alloc_text(PAGE, SelectInterfaces)
#pragma alloc_text(PAGE, SetPowerPolicy)
#pragma alloc_text(PAGE, ReadFdoRegistryKeyValue)
#pragma alloc_text(PAGE, RetrieveDeviceInformation)
#pragma alloc_text(PAGE, Rio500_ValidateConfigurationDescriptor)
#endif

NTSTATUS
Rio500_EvtDeviceAdd(
  WDFDRIVER       Driver,
  PWDFDEVICE_INIT DeviceInit
)
/*++
Routine Description:

    EvtDeviceAdd is called by the framework in response to AddDevice
    call from the PnP manager. We create and initialize a device object to
    represent a new instance of the device. All the software resources
    should be allocated in this callback.

Arguments:

    Driver - Handle to a framework driver object created in DriverEntry

    DeviceInit - Pointer to a framework-allocated WDFDEVICE_INIT structure.

Return Value:

    NTSTATUS

--*/
{
  WDF_FILEOBJECT_CONFIG        fileConfig;
  WDF_PNPPOWER_EVENT_CALLBACKS pnpPowerCallbacks;
  WDF_OBJECT_ATTRIBUTES        attributes;
  NTSTATUS                     status;
  WDFDEVICE                    device;
  WDF_DEVICE_PNP_CAPABILITIES  pnpCaps;
  WDF_IO_QUEUE_CONFIG          ioQueueConfig;
  PDEVICE_CONTEXT              pDevContext;
  WDFQUEUE                     queue;
  ULONG                        maximumTransferSize;

  UNREFERENCED_PARAMETER(Driver);

  Rio500_DbgPrint (3, ("Rio500_EvtDeviceAdd routine\n"));

  PAGED_CODE();

  //
  // Initialize the pnpPowerCallbacks structure.  Callback events for PNP
  // and Power are specified here.  If you don't supply any callbacks,
  // the Framework will take appropriate default actions based on whether
  // DeviceInit is initialized to be an FDO, a PDO or a filter device
  // object.
  //
  WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpPowerCallbacks);

  pnpPowerCallbacks.EvtDevicePrepareHardware = Rio500_EvtDevicePrepareHardware;

  WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpPowerCallbacks);

  //
  // Initialize the request attributes to specify the context size and type
  // for every request created by framework for this device.
  //
  WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, REQUEST_CONTEXT);

  WdfDeviceInitSetRequestAttributes(DeviceInit, &attributes);

  //
  // Initialize WDF_FILEOBJECT_CONFIG_INIT struct to tell the
  // framework whether you are interested in handle Create, Close and
  // Cleanup requests that gets genereate when an application or another
  // kernel component opens an handle to the device. If you don't register
  // the framework default behaviour would be complete these requests
  // with STATUS_SUCCESS. A driver might be interested in registering these
  // events if it wants to do security validation and also wants to maintain
  // per handle (fileobject) context.
  //
  WDF_FILEOBJECT_CONFIG_INIT(
    &fileConfig,
    Rio500_EvtDeviceFileCreate,
    WDF_NO_EVENT_CALLBACK,
    WDF_NO_EVENT_CALLBACK
  );

  //
  // Specify a context for FileObject. If you register FILE_EVENT callbacks,
  // the framework by default creates a framework FILEOBJECT corresponding
  // to the WDM fileobject. If you want to track any per handle context,
  // use the context for FileObject. Driver that typically use FsContext
  // field should instead use Framework FileObject context.
  //
  WDF_OBJECT_ATTRIBUTES_INIT(&attributes);

  WdfDeviceInitSetFileObjectConfig(DeviceInit, &fileConfig, &attributes);

  //
  // I/O type is Buffered by default. We want to do direct I/O for Reads
  // and Writes so set it explicitly.
  //
  WdfDeviceInitSetIoType(DeviceInit, WdfDeviceIoDirect);

  //
  // Now specify the size of device extension where we track per device
  // context.DeviceInit is completely initialized. So call the framework
  // to create the device and attach it to the lower stack.
  //
  WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, DEVICE_CONTEXT);
  attributes.EvtCleanupCallback = Rio500_EvtDeviceContextCleanup;

  status = WdfDeviceCreate(&DeviceInit, &attributes, &device);
  if (!NT_SUCCESS(status)) {
    Rio500_DbgPrint(1, ("WdfDeviceCreate failed with Status code 0x%x\n", status));
    return status;
  }

  //
  // Get the DeviceObject context by using accessor function specified in
  // the WDF_DECLARE_CONTEXT_TYPE_WITH_NAME macro for DEVICE_CONTEXT.
  //
  pDevContext = GetDeviceContext(device);

  //
  // Get MaximumTransferSize from registry
  //
  maximumTransferSize = 0;

  ReadFdoRegistryKeyValue(
    Driver,
    L"MaximumTransferSize",
    &maximumTransferSize
  );

  if (maximumTransferSize){
    pDevContext->MaximumTransferSize = maximumTransferSize;
  } else {
    pDevContext->MaximumTransferSize = DEFAULT_REGISTRY_TRANSFER_SIZE;
  }

  //
  // Tell the framework to set the SurpriseRemovalOK in the DeviceCaps so
  // that you don't get the popup in usermode (on Win2K) when you surprise
  // remove the device.
  //
  WDF_DEVICE_PNP_CAPABILITIES_INIT(&pnpCaps);
  pnpCaps.SurpriseRemovalOK = WdfTrue;

  WdfDeviceSetPnpCapabilities(device, &pnpCaps);

  //
  // Register I/O callbacks to tell the framework that you are interested
  // in handling WdfRequestTypeRead, WdfRequestTypeWrite, and 
  // IRP_MJ_DEVICE_CONTROL requests.
  // WdfIoQueueDispatchParallel means that we are capable of handling
  // all the I/O request simultaneously and we are responsible for protecting
  // data that could be accessed by these callbacks simultaneously.
  // This queue will be,  by default,  automanaged by the framework with
  // respect to PNP and Power events. That is, framework will take care
  // of queuing, failing, dispatching incoming requests based on the current
  // pnp/power state of the device.
  //
  WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&ioQueueConfig, WdfIoQueueDispatchParallel);

  ioQueueConfig.EvtIoRead = Rio500_EvtIoRead;
  ioQueueConfig.EvtIoWrite = Rio500_EvtIoWrite;
  ioQueueConfig.EvtIoDeviceControl = Rio500_EvtIoDeviceControl;
  ioQueueConfig.EvtIoStop = Rio500_EvtIoStop;

  status = WdfIoQueueCreate(
    device,
    &ioQueueConfig,
    WDF_NO_OBJECT_ATTRIBUTES,
    &queue// pointer to default queue
  );
  if (!NT_SUCCESS(status)) {
    Rio500_DbgPrint(1, ("WdfIoQueueCreate failed  for Default Queue 0x%x\n", status));
    return status;
  }

  //
  // Create a synchronized manual queue so we can retrieve one read request at a
  // time and dispatch it to the lower driver with the right StartFrame number.
  //
  WDF_IO_QUEUE_CONFIG_INIT(&ioQueueConfig, WdfIoQueueDispatchManual);
    
  WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
  attributes.SynchronizationScope = WdfSynchronizationScopeQueue;
     
  ioQueueConfig.EvtIoStop = Rio500_EvtIoStop;
    
  WDF_IO_QUEUE_CONFIG_INIT(&ioQueueConfig, WdfIoQueueDispatchManual);
    
  WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
  attributes.SynchronizationScope = WdfSynchronizationScopeQueue;
     
  ioQueueConfig.EvtIoStop = Rio500_EvtIoStop;
     
  //
  // Register a device interface so that app can find our device and talk to it.
  //
  status = WdfDeviceCreateDeviceInterface(
    device,
    (LPGUID)&GUID_CLASS_RIO500_USB,
    NULL // Reference String
  );
  if (!NT_SUCCESS(status)) {
    Rio500_DbgPrint(1, ("WdfDeviceCreateDeviceInterface failed  0x%x\n", status));
    return status;
  }

  status = USBD_CreateHandle(
    WdfDeviceWdmGetDeviceObject(device),    
    WdfDeviceWdmGetAttachedDevice(device),   
    USBD_CLIENT_CONTRACT_VERSION_602,   
    POOL_TAG,   
    &pDevContext->UsbdHandle
  );
  if (!NT_SUCCESS(status)) {
    Rio500_DbgPrint(1, ("USBD_CreateHandle failed 0x%x", status));
    return status;
  }

  Rio500_DbgPrint(3, ("EvtDriverDeviceAdd - ends\n"));
  return status;
}

NTSTATUS
Rio500_EvtDevicePrepareHardware(
  _In_ WDFDEVICE    Device,
  _In_ WDFCMRESLIST ResourceList,
  _In_ WDFCMRESLIST ResourceListTranslated
)
/*++

Routine Description:

    In this callback, the driver does whatever is necessary to make the
    hardware ready to use.  In the case of a USB device, this involves
    reading and selecting descriptors.

Arguments:

    Device - handle to a device

Return Value:

    NT status value

--*/
{
  NTSTATUS        status;
  PDEVICE_CONTEXT pDeviceContext;

  UNREFERENCED_PARAMETER(ResourceList);
  UNREFERENCED_PARAMETER(ResourceListTranslated);

  Rio500_DbgPrint(3, ("EvtDevicePrepareHardware - begins\n"));

  PAGED_CODE();

  pDeviceContext = GetDeviceContext(Device);

  //
  // Read the device descriptor, configuration descriptor
  // and select the interface descriptors
  //
  status = ReadAndSelectDescriptors(Device);

  if (!NT_SUCCESS(status)) {
    Rio500_DbgPrint(1, ("ReadandSelectDescriptors failed\n"));
    return status;
  }

  //
  // Enable wait-wake and idle timeout if the device supports it
  //
  if (pDeviceContext->WaitWakeEnable) {
    status = SetPowerPolicy(Device);
    if (!NT_SUCCESS(status)) {
      Rio500_DbgPrint(3, ("Rio500SetPowerPolicy failed\n"));
      return status;
    }
  }

  Rio500_DbgPrint(3, ("EvtDevicePrepareHardware - ends\n"));
  return status;
}

NTSTATUS
SetPowerPolicy(
  _In_ WDFDEVICE Device
)
{
  WDF_DEVICE_POWER_POLICY_IDLE_SETTINGS idleSettings;
  WDF_DEVICE_POWER_POLICY_WAKE_SETTINGS wakeSettings;
  NTSTATUS status = STATUS_SUCCESS;

  PAGED_CODE();

  //
  // Init the idle policy structure.
  //
  WDF_DEVICE_POWER_POLICY_IDLE_SETTINGS_INIT(&idleSettings, IDLE_CAPS_TYPE);
  idleSettings.IdleTimeout = 10000; // 10-sec

  status = WdfDeviceAssignS0IdleSettings(Device, &idleSettings);
  if (!NT_SUCCESS(status)) {
    Rio500_DbgPrint(3, ("WdfDeviceSetPowerPolicyS0IdlePolicy failed  0x%x\n", status));
    return status;
  }

  //
  // Init wait-wake policy structure.
  //
  WDF_DEVICE_POWER_POLICY_WAKE_SETTINGS_INIT(&wakeSettings);

  status = WdfDeviceAssignSxWakeSettings(Device, &wakeSettings);
  if (!NT_SUCCESS(status)) {
    Rio500_DbgPrint(3, ("WdfDeviceAssignSxWakeSettings failed  0x%x\n", status));
    return status;
  }

  return status;
}


NTSTATUS
ReadAndSelectDescriptors(
  _In_ WDFDEVICE Device
)
/*++

Routine Description:

    This routine configures the USB device.
    In this routines we get the device descriptor,
    the configuration descriptor and select the
    configuration.

Arguments:

    Device - Handle to a framework device

Return Value:

    NTSTATUS - NT status value.

--*/
{
  NTSTATUS               status;
  PDEVICE_CONTEXT        pDeviceContext;

  PAGED_CODE();

  //
  // initialize variables
  //
  pDeviceContext = GetDeviceContext(Device);

  //
  // Create a USB device handle so that we can communicate with the
  // underlying USB stack. The WDFUSBDEVICE handle is used to query,
  // configure, and manage all aspects of the USB device.
  // These aspects include device properties, bus properties,
  // and I/O creation and synchronization. We only create device the first
  // the PrepareHardware is called. If the device is restarted by pnp manager
  // for resource rebalance, we will use the same device handle but then select
  // the interfaces again because the USB stack could reconfigure the device on
  // restart.
  //
  if (pDeviceContext->WdfUsbTargetDevice == NULL) {
    WDF_USB_DEVICE_CREATE_CONFIG config;
    WDF_USB_DEVICE_CREATE_CONFIG_INIT(&config, USBD_CLIENT_CONTRACT_VERSION_602);
        
    status = WdfUsbTargetDeviceCreateWithParameters(
      Device,
      &config,
      WDF_NO_OBJECT_ATTRIBUTES,
      &pDeviceContext->WdfUsbTargetDevice
    );
    if (!NT_SUCCESS(status)) {
      Rio500_DbgPrint(1, ("WdfUsbTargetDeviceCreateWithParameters failed with Status code %x\n", status));
      return status;
    }
  }

  WdfUsbTargetDeviceGetDeviceDescriptor(
    pDeviceContext->WdfUsbTargetDevice,
    &pDeviceContext->UsbDeviceDescriptor
  );

  NT_ASSERT(pDeviceContext->UsbDeviceDescriptor.bNumConfigurations);

  status = ConfigureDevice(Device);
  return status;
}

NTSTATUS
ConfigureDevice(
  _In_ WDFDEVICE Device
)
/*++

Routine Description:

    This helper routine reads the configuration descriptor
    for the device in couple of steps.

Arguments:

    Device - Handle to a framework device

Return Value:

    NTSTATUS - NT status value

--*/
{
  USHORT                        size = 0;
  NTSTATUS                      status;
  PDEVICE_CONTEXT               pDeviceContext;
  PUSB_CONFIGURATION_DESCRIPTOR configurationDescriptor;
  WDF_OBJECT_ATTRIBUTES         attributes;
  WDFMEMORY                     memory;
  PUCHAR                        Offset = NULL;

  PAGED_CODE();

  //
  // initialize the variables
  //
  configurationDescriptor = NULL;
  pDeviceContext = GetDeviceContext(Device);

  //
  // Read the first configuration descriptor
  // This requires two steps:
  // 1. Ask the WDFUSBDEVICE how big it is
  // 2. Allocate it and get it from the WDFUSBDEVICE
  //
  status = WdfUsbTargetDeviceRetrieveConfigDescriptor(
    pDeviceContext->WdfUsbTargetDevice,
    NULL,
    &size
  );

  if (status != STATUS_BUFFER_TOO_SMALL || size == 0) {
    return status;
  }

  //
  // Create a memory object and specify usbdevice as the parent so that
  // it will be freed automatically.
  //
  WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
  attributes.ParentObject = pDeviceContext->WdfUsbTargetDevice;

  status = WdfMemoryCreate(
    &attributes,
    NonPagedPool,
    POOL_TAG,
    size,
    &memory,
    &configurationDescriptor
  );
  if (!NT_SUCCESS(status)) {
    return status;
  }

  status = WdfUsbTargetDeviceRetrieveConfigDescriptor(
    pDeviceContext->WdfUsbTargetDevice,
    configurationDescriptor,
    &size
  );
  if (!NT_SUCCESS(status)) {
    return status;
  }

  //
  // Check if the descriptors are valid
  // 
  status = Rio500_ValidateConfigurationDescriptor(configurationDescriptor, size, &Offset);
    
  if (!NT_SUCCESS(status)) {
    Rio500_DbgPrint(1, ("Descriptor validation failed with Status code %x and at the offset %p\n", status, Offset));
    return status;
  }

  pDeviceContext->UsbConfigurationDescriptor = configurationDescriptor;

  status = SelectInterfaces(Device);
  return status;
}

NTSTATUS
SelectInterfaces(
  _In_ WDFDEVICE Device
)
/*++

Routine Description:

    This helper routine selects the configuration, interface and
    creates a context for every pipe (end point) in that interface.

Arguments:

    Device - Handle to a framework device

Return Value:

    NT status value

--*/
{
  WDF_USB_DEVICE_SELECT_CONFIG_PARAMS     configParams;
  NTSTATUS                                status;
  PDEVICE_CONTEXT                         pDeviceContext;
  UCHAR                                   i;
  WDF_OBJECT_ATTRIBUTES                   pipeAttributes;
  WDF_USB_INTERFACE_SELECT_SETTING_PARAMS selectSettingParams;
  UCHAR                                   numberAlternateSettings = 0;
  UCHAR                                   numberConfiguredPipes;

  PAGED_CODE();

  pDeviceContext = GetDeviceContext(Device);

  //
  // The device has only one interface and the interface may have multiple
  // alternate settings. It will try to use alternate setting zero if it has
  // non-zero endpoints, otherwise it will try to search an alternate 
  // setting with non-zero endpoints.
  // 
  WDF_USB_DEVICE_SELECT_CONFIG_PARAMS_INIT_SINGLE_INTERFACE(&configParams);

  WDF_OBJECT_ATTRIBUTES_INIT(&pipeAttributes);

  status = WdfUsbTargetDeviceSelectConfig(
    pDeviceContext->WdfUsbTargetDevice,
    &pipeAttributes,
    &configParams
  );

  if (NT_SUCCESS(status) &&
      WdfUsbTargetDeviceGetNumInterfaces(pDeviceContext->WdfUsbTargetDevice) > 0) {
    status = RetrieveDeviceInformation(Device);
    if (!NT_SUCCESS(status)) {
      Rio500_DbgPrint(1, ("RetrieveDeviceInformation failed %x\n", status));
      return status;
    }

    pDeviceContext->UsbInterface = configParams.Types.SingleInterface.ConfiguredUsbInterface;

    //
    // This is written to work with Intel 82930 board, OSRUSBFX2, FX2 MUTT and FX3 MUTT
    // devices. The alternate setting zero of MUTT devices don't have any endpoints. So
    // in the code below, we will walk through the list of alternate settings until we
    // find one that has non-zero endpoints.
    //
    numberAlternateSettings = WdfUsbInterfaceGetNumSettings(pDeviceContext->UsbInterface);
    NT_ASSERT(numberAlternateSettings > 0);

    numberConfiguredPipes = 0;
    for (i = 0; i < numberAlternateSettings && numberConfiguredPipes == 0; i++) {
      WDF_USB_INTERFACE_SELECT_SETTING_PARAMS_INIT_SETTING(&selectSettingParams, i);

      WDF_OBJECT_ATTRIBUTES_INIT(&pipeAttributes);
      status = WdfUsbInterfaceSelectSetting(
        pDeviceContext->UsbInterface,
        &pipeAttributes,
        &selectSettingParams
      );
 
      if (NT_SUCCESS(status)) {
        numberConfiguredPipes = WdfUsbInterfaceGetNumConfiguredPipes(pDeviceContext->UsbInterface);
        if (numberConfiguredPipes > 0) {
          pDeviceContext->SelectedAlternateSetting = i;
        }
      }
    }

    pDeviceContext->NumberConfiguredPipes = numberConfiguredPipes;
    for (i = 0; i < pDeviceContext->NumberConfiguredPipes; i++) {
      WDFUSBPIPE pipe;
      WDF_USB_PIPE_INFORMATION pipeInfo;

      pipe = WdfUsbInterfaceGetConfiguredPipe(
        pDeviceContext->UsbInterface,
        i, // PipeIndex,
        NULL
      );

      WDF_USB_PIPE_INFORMATION_INIT(&pipeInfo);
      WdfUsbTargetPipeGetInformation(pipe, &pipeInfo);

      if (pipeInfo.PipeType == WdfUsbPipeTypeBulk) {
        // The first bulk pipe we find is "read", the second one is "write"
        if (pDeviceContext->ReadPipe == NULL) {
          WdfUsbTargetPipeSetNoMaximumPacketSizeCheck(pipe);
          pDeviceContext->ReadPipe = pipe;
        } else if (pDeviceContext->WritePipe == NULL) {
          WdfUsbTargetPipeSetNoMaximumPacketSizeCheck(pipe);
          pDeviceContext->WritePipe = pipe;
        }
      }
    }
  }

  return status;
}

NTSTATUS
RetrieveDeviceInformation(
  _In_ WDFDEVICE Device
)
{
  PDEVICE_CONTEXT            pDeviceContext;
  WDF_USB_DEVICE_INFORMATION info;
  NTSTATUS                   status;

  PAGED_CODE();

  pDeviceContext = GetDeviceContext(Device);

  WDF_USB_DEVICE_INFORMATION_INIT(&info);

  //
  // Retrieve USBD version information, port driver capabilites and device
  // capabilites such as speed, power, etc.
  //
  status = WdfUsbTargetDeviceRetrieveInformation(
    pDeviceContext->WdfUsbTargetDevice,
    &info
  );
  if (!NT_SUCCESS(status)) {
    return status;
  }

  pDeviceContext->IsDeviceHighSpeed =
      (info.Traits & WDF_USB_DEVICE_TRAIT_AT_HIGH_SPEED) ? TRUE : FALSE;

  Rio500_DbgPrint(
    3,
    (
      "DeviceIsHighSpeed: %s\n",
      pDeviceContext->IsDeviceHighSpeed ? "TRUE" : "FALSE"
    )
  );
  Rio500_DbgPrint(
    3,
    (
      "IsDeviceSelfPowered: %s\n",
      (info.Traits & WDF_USB_DEVICE_TRAIT_SELF_POWERED) ? "TRUE" : "FALSE"
    )
  );

  pDeviceContext->WaitWakeEnable = info.Traits & WDF_USB_DEVICE_TRAIT_REMOTE_WAKE_CAPABLE;

  Rio500_DbgPrint(
    3,
    (
      "IsDeviceRemoteWakeable: %s\n",
      (info.Traits & WDF_USB_DEVICE_TRAIT_REMOTE_WAKE_CAPABLE) ? "TRUE" : "FALSE"
    )
  );

  return STATUS_SUCCESS;
}

    
NTSTATUS
ReadFdoRegistryKeyValue(
  _In_  WDFDRIVER Driver,
  _In_  LPWSTR    Name,
  _Out_ PULONG    Value
)
/*++

Routine Description:

    Can be used to read any REG_DWORD registry value stored
    under Device Parameter.

Arguments:

    Driver - pointer to the device object
    Name - Name of the registry value
    Value -


Return Value:

    NTSTATUS 

--*/
{
  WDFKEY         hKey = NULL;
  NTSTATUS       status;
  UNICODE_STRING valueName;

  UNREFERENCED_PARAMETER(Driver);

  PAGED_CODE();

  *Value = 0;

  status = WdfDriverOpenParametersRegistryKey(
    WdfGetDriver(),
    KEY_READ,
    WDF_NO_OBJECT_ATTRIBUTES,
    &hKey
  );
  if (NT_SUCCESS(status)) {
      RtlInitUnicodeString(&valueName, Name);
      status = WdfRegistryQueryULong(
        hKey,
        &valueName,
        Value
      );

      WdfRegistryClose(hKey);
  }

  return status;
}

VOID
Rio500_EvtDeviceContextCleanup(
  _In_ WDFOBJECT WdfDevice
)
/*++

Routine Description:

    In this callback, it cleans up device context.

Arguments:

    WdfDevice - WDF device object

Return Value:

    VOID

--*/
{
  WDFDEVICE device;
  PDEVICE_CONTEXT  pDevContext;

  PAGED_CODE();

  device = (WDFDEVICE)WdfDevice;

  pDevContext = GetDeviceContext(device);

  if (pDevContext->UsbdHandle != NULL) {
    USBD_CloseHandle(pDevContext->UsbdHandle);
  }
}

USBD_STATUS
Rio500_ValidateConfigurationDescriptor(  
  _In_reads_bytes_(BufferLength) PUSB_CONFIGURATION_DESCRIPTOR ConfigDesc,
  _In_    ULONG  BufferLength,
  _Inout_ PUCHAR *Offset
)
/*++

Routine Description:

    Validates a USB Configuration Descriptor

Parameters:

    ConfigDesc: Pointer to the entire USB Configuration descriptor returned by 
        the device

    BufferLength: Known size of buffer pointed to by ConfigDesc (Not wTotalLength)

    Offset: if the USBD_STATUS returned is not USBD_STATUS_SUCCESS, offet will
        be set to the address within the ConfigDesc buffer where the failure 
        occured.

Return Value:

    USBD_STATUS
    Success implies the configuration descriptor is valid.

--*/
{
  USBD_STATUS status = USBD_STATUS_SUCCESS;
  USHORT ValidationLevel = 3;

  PAGED_CODE();

  //
  // Call USBD_ValidateConfigurationDescriptor to validate the descriptors which are
  // present in this supplied configuration descriptor. USBD_ValidateConfigurationDescriptor
  // validates that all descriptors are completely contained within the configuration
  // descriptor buffer. It also checks for interface numbers, number of endpoints in an
  // interface etc. Please refer to msdn documentation for this function for more information.
  //
  status = USBD_ValidateConfigurationDescriptor(ConfigDesc, BufferLength, ValidationLevel, Offset, POOL_TAG);
  if (!NT_SUCCESS(status)) {
    return status;
  }

  *Offset = NULL;
  return status;
}
