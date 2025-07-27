/*
 * Copyright (C) 2023 Mohamad Al-Jaf
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#define COBJMACROS
#include "initguid.h"
#include <stdarg.h>

#include "windef.h"
#include "winbase.h"
#include "winstring.h"

#include "roapi.h"
#define WIDL_using_Windows_Storage_Streams
#include "robuffer.h"

#define WIDL_using_Windows_Foundation
#define WIDL_using_Windows_Foundation_Collections
#include "windows.foundation.h"
#include "windows.storage.streams.h"
#define WIDL_using_Windows_Networking
#include "windows.networking.connectivity.h"
#include "windows.networking.h"
#define WIDL_using_Windows_Devices_Bluetooth_Advertisement
#define WIDL_using_Windows_Devices_Bluetooth
#include "windows.devices.bluetooth.advertisement.h"
#include "windows.devices.bluetooth.rfcomm.h"
#include "windows.devices.bluetooth.h"

#include "wine/test.h"

#define check_interface( obj, iid ) check_interface_( __LINE__, obj, iid )
static void check_interface_( unsigned int line, void *obj, const IID *iid )
{
    IUnknown *iface = obj;
    IUnknown *unk;
    HRESULT hr;

    hr = IUnknown_QueryInterface( iface, iid, (void **)&unk );
    ok_(__FILE__, line)( hr == S_OK, "got hr %#lx.\n", hr );
    IUnknown_Release( unk );
}

struct inspectable_async_handler
{
    IAsyncOperationCompletedHandler_IInspectable iface;
    const GUID *iid;
    IAsyncOperation_IInspectable *async;
    AsyncStatus status;
    BOOL invoked;
    HANDLE event;
    LONG ref;
};

static inline struct inspectable_async_handler *impl_from_IAsyncOperationCompletedHandler_IInspectable( IAsyncOperationCompletedHandler_IInspectable *iface )
{
    return CONTAINING_RECORD( iface, struct inspectable_async_handler, iface );
}

static HRESULT WINAPI inspectable_async_handler_QueryInterface( IAsyncOperationCompletedHandler_IInspectable *iface, REFIID iid, void **out )
{
    struct inspectable_async_handler *impl = impl_from_IAsyncOperationCompletedHandler_IInspectable( iface );
    if (IsEqualGUID( iid, &IID_IUnknown ) || IsEqualGUID( iid, &IID_IAgileObject ) || IsEqualGUID( iid, impl->iid ))
    {
        IUnknown_AddRef( iface );
        *out = iface;
        return S_OK;
    }

    if (winetest_debug > 1)
        trace( "%s not implemented, returning E_NOINTERFACE.\n", debugstr_guid( iid ) );
    *out = NULL;
    return E_NOINTERFACE;
}

static ULONG WINAPI inspectable_async_handler_AddRef( IAsyncOperationCompletedHandler_IInspectable *iface )
{
    struct inspectable_async_handler *impl = impl_from_IAsyncOperationCompletedHandler_IInspectable( iface );
    return InterlockedIncrement( &impl->ref );
}

static ULONG WINAPI inspectable_async_handler_Release( IAsyncOperationCompletedHandler_IInspectable *iface )
{
    struct inspectable_async_handler *impl = impl_from_IAsyncOperationCompletedHandler_IInspectable( iface );
    ULONG ref = InterlockedDecrement( &impl->ref );
    if (!ref) free( impl );
    return ref;
}

static HRESULT WINAPI inspectable_async_handler_Invoke( IAsyncOperationCompletedHandler_IInspectable *iface, IAsyncOperation_IInspectable *async,
                                                        AsyncStatus status )
{
    struct inspectable_async_handler *impl = impl_from_IAsyncOperationCompletedHandler_IInspectable( iface );
    ok( !impl->invoked, "invoked twice\n" );
    impl->async = async;
    impl->status = status;
    if (impl->event) SetEvent( impl->event );
    return S_OK;
}

static const IAsyncOperationCompletedHandler_IInspectableVtbl inspectable_async_handler_vtbl =
{
    /* IUnknown */
    inspectable_async_handler_QueryInterface,
    inspectable_async_handler_AddRef,
    inspectable_async_handler_Release,
    /* IAsyncOperationCompletedHandler<IInspectable> */
    inspectable_async_handler_Invoke
};

static WINAPI IAsyncOperationCompletedHandler_IInspectable *inspectable_async_handler_create( HANDLE event, const GUID *iid )
{
    struct inspectable_async_handler *impl;

    if (!(impl = calloc( 1, sizeof(*impl) ))) return NULL;
    impl->iface.lpVtbl = &inspectable_async_handler_vtbl;
    impl->iid = iid;
    impl->event = event;
    impl->ref = 1;
    return &impl->iface;
}

static void await_bluetoothledevice( int line, IAsyncOperation_BluetoothLEDevice *async )
{
    IAsyncOperationCompletedHandler_IInspectable *handler;
    HANDLE event;
    HRESULT hr;
    DWORD ret;

    event = CreateEventW( NULL, FALSE, FALSE, NULL );
    ok_(__FILE__, line)( !!event, "CreateEventW failed, error %lu\n", GetLastError() );

    handler = inspectable_async_handler_create( event, &IID_IAsyncOperationCompletedHandler_BluetoothLEDevice );
    ok_( __FILE__, line )( !!handler, "inspectable_async_handler_create failed\n" );
    hr = IAsyncOperation_BluetoothLEDevice_put_Completed( async, (IAsyncOperationCompletedHandler_BluetoothLEDevice *)handler );
    ok_(__FILE__, line)( hr == S_OK, "put_Completed returned %#lx\n", hr );
    IAsyncOperationCompletedHandler_IInspectable_Release( handler );

    ret = WaitForSingleObject( event, 5000 );
    ok_(__FILE__, line)( !ret, "WaitForSingleObject returned %#lx\n", ret );
    ret = CloseHandle( event );
    ok_(__FILE__, line)( ret, "CloseHandle failed, error %lu\n", GetLastError() );
}

static void check_bluetoothledevice_async( int line, IAsyncOperation_BluetoothLEDevice *async,
                                           UINT32 expect_id, AsyncStatus expect_status,
                                           HRESULT expect_hr, IBluetoothLEDevice **result )
{
    AsyncStatus async_status;
    IAsyncInfo *async_info;
    HRESULT hr, async_hr;
    UINT32 async_id;

    hr = IAsyncOperation_BluetoothLEDevice_QueryInterface( async, &IID_IAsyncInfo, (void **)&async_info );
    ok_(__FILE__, line)( hr == S_OK, "QueryInterface returned %#lx\n", hr );

    async_id = 0xdeadbeef;
    hr = IAsyncInfo_get_Id( async_info, &async_id );
    if (expect_status < 4) ok_(__FILE__, line)( hr == S_OK, "get_Id returned %#lx\n", hr );
    else ok_(__FILE__, line)( hr == E_ILLEGAL_METHOD_CALL, "get_Id returned %#lx\n", hr );
    ok_(__FILE__, line)( async_id == expect_id, "got id %u\n", async_id );

    async_status = 0xdeadbeef;
    hr = IAsyncInfo_get_Status( async_info, &async_status );
    if (expect_status < 4) ok_(__FILE__, line)( hr == S_OK, "get_Status returned %#lx\n", hr );
    else ok_(__FILE__, line)( hr == E_ILLEGAL_METHOD_CALL, "get_Status returned %#lx\n", hr );
    ok_(__FILE__, line)( async_status == expect_status, "got status %u\n", async_status );

    async_hr = 0xdeadbeef;
    hr = IAsyncInfo_get_ErrorCode( async_info, &async_hr );
    if (expect_status < 4) ok_(__FILE__, line)( hr == S_OK, "get_ErrorCode returned %#lx\n", hr );
    else ok_(__FILE__, line)( hr == E_ILLEGAL_METHOD_CALL, "get_ErrorCode returned %#lx\n", hr );
    if (expect_status < 4) todo_wine_if( FAILED(expect_hr))
    ok_(__FILE__, line)( async_hr == expect_hr, "got error %#lx\n", async_hr );
    else ok_(__FILE__, line)( async_hr == E_ILLEGAL_METHOD_CALL, "got error %#lx\n", async_hr );

    IAsyncInfo_Release( async_info );

    hr = IAsyncOperation_BluetoothLEDevice_GetResults( async, result );
    switch (expect_status)
    {
    case Completed:
    case Error:
        todo_wine_if( FAILED(expect_hr))
        ok_(__FILE__, line)( hr == expect_hr, "GetResults returned %#lx\n", hr );
        break;
    case Canceled:
    case Started:
    default:
        ok_(__FILE__, line)( hr == E_ILLEGAL_METHOD_CALL, "GetResults returned %#lx\n", hr );
        break;
    }
}

static void test_BluetoothAdapterStatics(void)
{
    static const WCHAR *default_res = L"System.Devices.InterfaceClassGuid:=\"{92383B0E-F90E-4AC9-8D44-8C2D0D0EBDA2}\" "
                                      L"AND System.Devices.InterfaceEnabled:=System.StructuredQueryType.Boolean#True";
    static const WCHAR *bluetoothadapter_statics_name = L"Windows.Devices.Bluetooth.BluetoothAdapter";
    IBluetoothAdapterStatics *bluetoothadapter_statics;
    IActivationFactory *factory;
    HSTRING str, default_str;
    HRESULT hr;
    INT32 res;
    LONG ref;

    hr = WindowsCreateString( bluetoothadapter_statics_name, wcslen( bluetoothadapter_statics_name ), &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( str );
    ok( hr == S_OK || broken( hr == REGDB_E_CLASSNOTREG ), "got hr %#lx.\n", hr );
    if (hr == REGDB_E_CLASSNOTREG)
    {
        win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( bluetoothadapter_statics_name ) );
        return;
    }

    check_interface( factory, &IID_IUnknown );
    check_interface( factory, &IID_IInspectable );
    check_interface( factory, &IID_IAgileObject );

    hr = IActivationFactory_QueryInterface( factory, &IID_IBluetoothAdapterStatics, (void **)&bluetoothadapter_statics );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = IBluetoothAdapterStatics_GetDeviceSelector( bluetoothadapter_statics, NULL );
    ok( hr == E_POINTER, "got hr %#lx.\n", hr );
    hr = IBluetoothAdapterStatics_GetDeviceSelector( bluetoothadapter_statics, &str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = WindowsCreateString( default_res, wcslen(default_res), &default_str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = WindowsCompareStringOrdinal( str, default_str, &res );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( !res, "got unexpected string %s.\n", debugstr_hstring(str) );

    WindowsDeleteString( str );
    WindowsDeleteString( default_str );
    ref = IBluetoothAdapterStatics_Release( bluetoothadapter_statics );
    ok( ref == 2, "got ref %ld.\n", ref );
    ref = IActivationFactory_Release( factory );
    ok( ref == 1, "got ref %ld.\n", ref );
}

static void test_BluetoothDeviceStatics( void )
{
    static const WCHAR *class_name = RuntimeClass_Windows_Devices_Bluetooth_BluetoothDevice;
    IBluetoothDeviceStatics *statics;
    IActivationFactory *factory;
    HSTRING str;
    HRESULT hr;

    WindowsCreateString( class_name, wcslen( class_name ), &str );
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void *)&factory );
    WindowsDeleteString( str );
    ok( hr == S_OK || broken( hr == REGDB_E_CLASSNOTREG ), "got hr %#lx.\n", hr );
    if (hr != S_OK)
    {
        win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( class_name ) );
        return;
    }
    check_interface( factory, &IID_IUnknown );
    check_interface( factory, &IID_IInspectable );
    check_interface( factory, &IID_IAgileObject );

    hr = IActivationFactory_QueryInterface( factory, &IID_IBluetoothDeviceStatics, (void **)&statics );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IActivationFactory_Release( factory );
    if (FAILED( hr ))
    {
        skip( "BluetoothDeviceStatics not available.\n" );
        return;
    }

    str = NULL;
    hr = IBluetoothDeviceStatics_GetDeviceSelector( statics, &str );
    todo_wine ok( hr == S_OK, "got hr %#lx.\n", hr );
    todo_wine ok( !WindowsIsStringEmpty( str ), "got empty device selector string.\n" );
    WindowsDeleteString( str );

    IBluetoothDeviceStatics_Release( statics );
}

static void test_BluetoothLEDeviceStatics( void )
{
    static const WCHAR *class_name = RuntimeClass_Windows_Devices_Bluetooth_BluetoothLEDevice;
    IAsyncOperation_BluetoothLEDevice *async_op;
    IBluetoothLEDeviceStatics *statics;
    IActivationFactory *factory;
    IBluetoothLEDevice *device = NULL;
    HSTRING str;
    HRESULT hr;

    WindowsCreateString( class_name, wcslen( class_name ), &str );
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void *)&factory );
    WindowsDeleteString( str );
    ok( hr == S_OK || broken( hr == REGDB_E_CLASSNOTREG ), "got hr %#lx.\n", hr );
    if (hr == REGDB_E_CLASSNOTREG)
    {
        win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( class_name ) );
        return;
    }
    check_interface( factory, &IID_IUnknown );
    check_interface( factory, &IID_IInspectable );
    check_interface( factory, &IID_IAgileObject );

    hr = IActivationFactory_QueryInterface( factory, &IID_IBluetoothLEDeviceStatics, (void **)&statics );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IActivationFactory_Release( factory );

    str = NULL;
    hr = IBluetoothLEDeviceStatics_GetDeviceSelector( statics, &str );
    todo_wine ok( hr == S_OK, "got hr %#lx.\n", hr );
    todo_wine ok( !WindowsIsStringEmpty( str ), "got empty device selector string.\n" );
    WindowsDeleteString( str );

    /* Use an invalid Bluetooth address. */
    hr = IBluetoothLEDeviceStatics_FromBluetoothAddressAsync( statics, 0, &async_op );
    todo_wine ok( hr == S_OK, "got hr %#lx.\n", hr );

    if (hr == S_OK)
    {
        await_bluetoothledevice( __LINE__, async_op );
        check_bluetoothledevice_async( __LINE__, async_op, 1, Completed, S_OK, &device );
        ok( !device, "got device %p != NULL\n", device );
        if (device) IBluetoothLEDevice_Release( device );
    }

    IBluetoothLEDeviceStatics_Release( statics );
}

static void test_BluetoothLEAdvertisementFilter( void )
{
    static const WCHAR *class_name = RuntimeClass_Windows_Devices_Bluetooth_Advertisement_BluetoothLEAdvertisementFilter;
    IVector_BluetoothLEAdvertisementBytePattern *patterns;
    IBluetoothLEAdvertisementFilter *filter;
    IBluetoothLEAdvertisement *adv;
    IInspectable *inspectable;
    HSTRING str;
    HRESULT hr;
    UINT32 len;

    WindowsCreateString( class_name, wcslen( class_name ), &str );
    hr = RoActivateInstance( str, &inspectable );
    WindowsDeleteString( str );
    todo_wine ok( hr == S_OK || broken( hr == REGDB_E_CLASSNOTREG ), "got hr %#lx.\n", hr );
    if (hr == REGDB_E_CLASSNOTREG || hr == CLASS_E_CLASSNOTAVAILABLE)
    {
        todo_wine win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( class_name ) );
        return;
    }

    check_interface( inspectable, &IID_IUnknown );
    check_interface( inspectable, &IID_IInspectable );
    check_interface( inspectable, &IID_IAgileObject );

    hr = IInspectable_QueryInterface( inspectable, &IID_IBluetoothLEAdvertisementFilter, (void **)&filter );
    IInspectable_Release( inspectable );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = IBluetoothLEAdvertisementFilter_get_Advertisement( filter, &adv );
    todo_wine ok( hr == S_OK, "got hr %#lx.\n", hr );
    ok( !!adv, "got adv %p\n", adv );
    if (SUCCEEDED( hr ))
    {
        IVector_BluetoothLEAdvertisementDataSection *sections;
        IReference_BluetoothLEAdvertisementFlags *flags;
        IVector_BluetoothLEManufacturerData *mfgs;
        IVector_GUID *guids;

        check_interface( adv, &IID_IAgileObject );

        /* Advertisement should be empty */
        flags = (IReference_BluetoothLEAdvertisementFlags *)0xdeadbeef;
        hr = IBluetoothLEAdvertisement_get_Flags( adv, &flags );
        todo_wine ok( hr == S_OK, "got hr %#lx.\n", hr );
        todo_wine ok( !flags, "got flags %p.\n", flags );
        str = NULL;
        hr = IBluetoothLEAdvertisement_get_LocalName( adv, &str );
        todo_wine ok( hr == S_OK, "got hr %#lx.\n", hr );
        todo_wine ok( WindowsIsStringEmpty( str ), "got str %s.\n", debugstr_hstring( str ) );

        hr = IBluetoothLEAdvertisement_get_ServiceUuids( adv, &guids );
        todo_wine ok( hr == S_OK, "got hr %#lx.\n", hr );
        len = 0xdeadbeef;
        hr = IVector_GUID_get_Size( guids, &len );
        IVector_GUID_Release( guids );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        ok( !len, "got len %u.\n", len );

        hr = IBluetoothLEAdvertisement_get_ManufacturerData( adv, &mfgs );
        todo_wine ok( hr == S_OK, "got hr %#lx.\n", hr );
        len = 0xdeadbeef;
        hr = IVector_BluetoothLEManufacturerData_get_Size( mfgs, &len );
        IVector_BluetoothLEManufacturerData_Release( mfgs );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        ok( !len, "got len %u.\n", len );

        hr = IBluetoothLEAdvertisement_get_DataSections( adv, &sections );
        todo_wine ok( hr == S_OK, "got hr %#lx.\n", hr );
        len = 0xdeadbeef;
        hr = IVector_BluetoothLEAdvertisementDataSection_get_Size( sections, &len );
        IVector_BluetoothLEAdvertisementDataSection_Release( sections );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        ok( !len, "got len %u.\n", len );

        hr = IBluetoothLEAdvertisementFilter_put_Advertisement( filter, adv );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        IBluetoothLEAdvertisement_Release( adv );
    }

    hr = IBluetoothLEAdvertisementFilter_get_BytePatterns( filter, &patterns );
    todo_wine ok( hr == S_OK, "got hr %#lx.\n", hr );
    if (SUCCEEDED( hr ))
    {
        check_interface( patterns, &IID_IAgileObject );
        check_interface( patterns, &IID_IIterable_BluetoothLEAdvertisementBytePattern );
        len = 0xdeadbeef;
        hr = IVector_BluetoothLEAdvertisementBytePattern_get_Size( patterns, &len );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        ok( !len, "got len %u.\n", len );
        IVector_BluetoothLEAdvertisementBytePattern_Release( patterns );
    }

    IBluetoothLEAdvertisementFilter_Release( filter );
}

static void test_BluetoothLEAdvertisementBytePattern( void )
{
    static const WCHAR *class_name = RuntimeClass_Windows_Devices_Bluetooth_Advertisement_BluetoothLEAdvertisementBytePattern;
    IBluetoothLEAdvertisementBytePattern *pattern;
    IInspectable *inspectable;
    INT16 int16 = 0;
    IBuffer *data;
    BYTE byte = 0;
    HSTRING str;
    HRESULT hr;

    WindowsCreateString( class_name, wcslen( class_name ), &str );
    hr = RoActivateInstance( str, &inspectable );
    WindowsDeleteString( str );
    todo_wine ok( hr == S_OK || broken( hr == REGDB_E_CLASSNOTREG ), "got hr %#lx.\n", hr );
    if (hr == REGDB_E_CLASSNOTREG || hr == CLASS_E_CLASSNOTAVAILABLE)
    {
        todo_wine win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( class_name ) );
        return;
    }

    check_interface( inspectable, &IID_IUnknown );
    check_interface( inspectable, &IID_IInspectable );
    check_interface( inspectable, &IID_IAgileObject );

    hr = IInspectable_QueryInterface( inspectable, &IID_IBluetoothLEAdvertisementBytePattern, (void **)&pattern );
    IInspectable_Release( inspectable );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    byte = 0xff;
    hr = IBluetoothLEAdvertisementBytePattern_get_DataType( pattern, &byte );
    todo_wine ok( hr == S_OK, "got hr %#lx.\n", hr );
    todo_wine ok( !byte, "got byte %#x\n", byte );
    hr = IBluetoothLEAdvertisementBytePattern_put_DataType( pattern, 0xff );
    todo_wine ok( hr == S_OK, "got hr %#lx.\n", hr );
    byte = 0;
    hr = IBluetoothLEAdvertisementBytePattern_get_DataType( pattern, &byte );
    todo_wine ok( hr == S_OK, "got hr %#lx.\n", hr );
    todo_wine ok( byte == 0xff, "got byte %#x\n", byte );

    int16 = 0xffff;
    hr = IBluetoothLEAdvertisementBytePattern_get_Offset( pattern, &int16 );
    todo_wine ok( hr == S_OK, "got hr %#lx.\n", hr );
    todo_wine ok( !int16, "got int16 %#x\n", int16 );
    hr = IBluetoothLEAdvertisementBytePattern_put_Offset( pattern, 0xffff );
    todo_wine ok( hr == E_INVALIDARG, "got hr %#lx.\n", hr );
    hr = IBluetoothLEAdvertisementBytePattern_put_Offset( pattern, 254 ); /* Maximum offset is 253 */
    todo_wine ok( hr == E_INVALIDARG, "got hr %#lx.\n", hr );
    hr = IBluetoothLEAdvertisementBytePattern_put_Offset( pattern, -1 );
    todo_wine ok( hr == E_INVALIDARG, "got hr %#lx.\n", hr );
    hr = IBluetoothLEAdvertisementBytePattern_put_Offset( pattern, 253 );
    todo_wine ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IBluetoothLEAdvertisementBytePattern_get_Offset( pattern, &int16 );
    todo_wine ok( hr == S_OK, "got hr %#lx.\n", hr );
    todo_wine ok( int16 == 253, "got int16 %#x\n", int16 );

    data = NULL;
    hr = IBluetoothLEAdvertisementBytePattern_get_Data( pattern, &data );
    todo_wine ok( hr == S_OK, "got hr %#lx.\n", hr );
    todo_wine ok( !!data, "got data %p\n", data );
    if (data)
    {
        check_interface( data, &IID_IAgileObject );
        hr = IBluetoothLEAdvertisementBytePattern_put_Data( pattern, data );
        todo_wine ok( hr == S_OK, "got hr %#lx.\n", hr );
        IBuffer_Release( data );
    }
    IBluetoothLEAdvertisementBytePattern_Release( pattern );
}

static void test_BluetoothLEAdvertisementDataSection( void )
{
    static const WCHAR *class_name = RuntimeClass_Windows_Devices_Bluetooth_Advertisement_BluetoothLEAdvertisementDataSection;
    IBluetoothLEAdvertisementDataSectionFactory *section_factory;
    IBluetoothLEAdvertisementDataSection *section;
    IBufferFactory *buffer_factory;
    static const char exp_bytes[] = "Wine is not an emulator.";
    IBufferByteAccess *byte_access;
    IActivationFactory *factory;
    IInspectable *inspectable;
    IBuffer *data;
    BYTE *bytes;
    HSTRING str;
    HRESULT hr;
    UINT32 len;
    BYTE type;

    WindowsCreateString( class_name, wcslen( class_name ), &str );
    hr = RoGetActivationFactory( str, &IID_IActivationFactory, (void **)&factory );
    WindowsDeleteString( str );
    todo_wine ok( hr == S_OK || broken( hr == REGDB_E_CLASSNOTREG ), "got hr %#lx.\n", hr );
    if (hr == REGDB_E_CLASSNOTREG || hr == CLASS_E_CLASSNOTAVAILABLE)
    {
        todo_wine win_skip( "%s runtimeclass not registered, skipping tests.\n", wine_dbgstr_w( class_name ) );
        return;
    }

    check_interface( factory, &IID_IAgileObject );
    hr = IActivationFactory_ActivateInstance( factory, &inspectable );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    check_interface( inspectable, &IID_IUnknown );
    check_interface( inspectable, &IID_IInspectable );
    check_interface( inspectable, &IID_IAgileObject );

    hr = IInspectable_QueryInterface( inspectable, &IID_IBluetoothLEAdvertisementDataSection, (void **)&section );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    IInspectable_Release( inspectable );

    type = 0xff;
    hr = IBluetoothLEAdvertisementDataSection_get_DataType( section, &type );
    todo_wine ok( hr == S_OK, "got hr %#lx.\n", hr );
    todo_wine ok( !type, "got type %#x\n", type );
    hr = IBluetoothLEAdvertisementDataSection_put_DataType( section, 0xff );
    todo_wine ok( hr == S_OK, "got hr %#lx.\n", hr );
    type = 0;
    hr = IBluetoothLEAdvertisementDataSection_get_DataType( section, &type );
    todo_wine ok( hr == S_OK, "got hr %#lx.\n", hr );
    todo_wine ok( type == 0xff, "got type %#x\n", type );

    data = NULL;
    hr = IBluetoothLEAdvertisementDataSection_get_Data( section, &data );
    todo_wine ok( hr == S_OK, "got hr %#lx.\n", hr );
    todo_wine ok( !!data, "got data %p\n", data );
    if (SUCCEEDED( hr ))
    {
        check_interface( data, &IID_IAgileObject );
        len = 0xdeadbeef;
        hr = IBuffer_get_Length( data, &len );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        ok( !len, "got len %u\n", len );

        hr = IBluetoothLEAdvertisementDataSection_put_Data( section, data );
        todo_wine ok( hr == S_OK, "got hr %#lx.\n", hr );
        IBuffer_Release( data );
    }
    IBluetoothLEAdvertisementDataSection_Release( section );

    hr = IActivationFactory_QueryInterface( factory, &IID_IBluetoothLEAdvertisementDataSectionFactory, (void **)&section_factory );
    IActivationFactory_Release( factory );
    todo_wine ok( hr == S_OK, "got hr %#lx.\n", hr );
    if (FAILED( hr ))
    {
        skip( "Could not get IBluetoothLEAdvertisementDataSectionFactory.\n" );
        return;
    }

    class_name = RuntimeClass_Windows_Storage_Streams_Buffer;
    WindowsCreateString( class_name, wcslen( class_name ), &str );
    hr = RoGetActivationFactory( str, &IID_IBufferFactory, (void **)&buffer_factory );
    WindowsDeleteString( str );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = IBufferFactory_Create( buffer_factory, sizeof( exp_bytes ), &data );
    IBufferFactory_Release( buffer_factory );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = IBuffer_put_Length( data, sizeof( exp_bytes ) );
    ok( hr == S_OK, "got hr %#lx.\n", hr );

    hr = IBuffer_QueryInterface( data, &IID_IBufferByteAccess, (void **)&byte_access );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    hr = IBufferByteAccess_Buffer( byte_access, &bytes );
    ok( hr == S_OK, "got hr %#lx.\n", hr );
    memcpy( bytes, exp_bytes, sizeof( exp_bytes ) );
    IBufferByteAccess_Release( byte_access );

    check_interface( section_factory, &IID_IAgileObject );
    hr = IBluetoothLEAdvertisementDataSectionFactory_Create( section_factory, 0xff, data, &section );
    IBuffer_Release( data );
    todo_wine ok( hr == S_OK, "got hr %#lx.\n", hr );
    if (FAILED( hr ))
    {
        skip( "Could not create IBluetoothLEAdvertisementDataSection, skipping.\n" );
        return;
    }

    type = 0;
    hr = IBluetoothLEAdvertisementDataSection_get_DataType( section, &type );
    todo_wine ok( hr == S_OK, "got hr %#lx.\n", hr );
    todo_wine ok( type == 0xff, "got type %#x\n.", type );

    hr = IBluetoothLEAdvertisementDataSection_get_Data( section, &data );
    todo_wine ok( hr == S_OK, "got hr %#lx.\n", hr );
    if (SUCCEEDED( hr ))
    {
        hr = IBuffer_get_Length( data, &len );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        ok( len == sizeof( exp_bytes ), "got len %u.\n", len );

        hr = IBuffer_QueryInterface( data, &IID_IBufferByteAccess, (void **)&byte_access );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        hr = IBufferByteAccess_Buffer( byte_access, &bytes );
        ok( hr == S_OK, "got hr %#lx.\n", hr );
        if (len == sizeof( exp_bytes ))
            ok( !memcmp( bytes, exp_bytes, len ), "got bytes %s\n", debugstr_an( (const char *)bytes, len ) );
        IBufferByteAccess_Release( byte_access );
        IBuffer_Release( data );
    }

    IBluetoothLEAdvertisementDataSection_Release( section );
}

START_TEST(bluetooth)
{
    HRESULT hr;

    hr = RoInitialize( RO_INIT_MULTITHREADED );
    ok( hr == S_OK, "RoInitialize failed, hr %#lx\n", hr );

    test_BluetoothAdapterStatics();
    test_BluetoothDeviceStatics();
    test_BluetoothLEDeviceStatics();
    test_BluetoothLEAdvertisementFilter();
    test_BluetoothLEAdvertisementBytePattern();
    test_BluetoothLEAdvertisementDataSection();

    RoUninitialize();
}
