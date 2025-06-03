/*
 * BLE Generic Attribute Profile (GATT) APIs
 *
 * Copyright 2025 Vibhav Pant
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


#include <stdarg.h>
#include <stdint.h>
#include <assert.h>

#include <windef.h>
#include <winbase.h>

#include <bthsdpdef.h>
#include <bluetoothapis.h>
#include <bthdef.h>
#include <winioctl.h>

#include <bthledef.h>
#include <bluetoothleapis.h>

#include <wine/winebth.h>
#include "wine/debug.h"

WINE_DEFAULT_DEBUG_CHANNEL( bluetoothapis );

static const char *debugstr_BTH_LE_UUID( const BTH_LE_UUID *uuid )
{
    if (uuid->IsShortUuid)
        return wine_dbg_sprintf("{ IsShortUuid=1 {%#x} }", uuid->Value.ShortUuid );
    return wine_dbg_sprintf( "{ IsShortUuid=0 %s }", debugstr_guid( &uuid->Value.LongUuid ) );
}

static const char *debugstr_BTH_LE_GATT_SERVICE( const BTH_LE_GATT_SERVICE *svc )
{
    if (!svc)
        return wine_dbg_sprintf( "(null)" );
    return wine_dbg_sprintf( "{ %s %#x }", debugstr_BTH_LE_UUID( &svc->ServiceUuid ), svc->AttributeHandle );
}

HRESULT WINAPI BluetoothGATTGetServices( HANDLE le_device, USHORT count, BTH_LE_GATT_SERVICE *buf,
                                         USHORT *actual, ULONG flags )
{
    struct winebth_le_device_get_gatt_services_params *services;
    SIZE_T services_count = 1;

    TRACE( "(%p, %u, %p, %p, %#lx)\n", le_device, count, buf, actual, flags );

    if (!actual)
        return E_POINTER;

    if ((!buf && count) || (buf && !count))
        return E_INVALIDARG;

    for (;;)
    {
        DWORD size, bytes;

        size = offsetof( struct winebth_le_device_get_gatt_services_params, services[services_count] );
        services = calloc( 1, size );
        if (!services)
            return HRESULT_FROM_WIN32( ERROR_NO_SYSTEM_RESOURCES );
        if (!DeviceIoControl( le_device, IOCTL_WINEBTH_LE_DEVICE_GET_GATT_SERVICES, NULL, 0, services, size, &bytes, NULL )
            && GetLastError() != ERROR_MORE_DATA)
        {
            free( services );
            return HRESULT_FROM_WIN32( GetLastError() );
        }
        if (!services->count)
        {
            *actual = 0;
            free( services );
            return S_OK;
        }
        if (services_count != services->count)
        {
            services_count = services->count;
            free( services );
            continue;
        }
        break;
    }

    *actual = services_count;
    if (!buf)
    {
        free( services );
        return HRESULT_FROM_WIN32( ERROR_MORE_DATA );
    }

    memcpy( buf, services->services, min( services_count, count ) * sizeof( *buf ) );
    free( services );
    if (count < services_count)
        return HRESULT_FROM_WIN32( ERROR_INVALID_USER_BUFFER );

    return S_OK;
}

HRESULT WINAPI BluetoothGATTGetCharacteristics( HANDLE device, BTH_LE_GATT_SERVICE *service, USHORT count,
                                                BTH_LE_GATT_CHARACTERISTIC *buf, USHORT *actual, ULONG flags )
{
    struct winebth_le_device_get_gatt_characteristics_params *chars;
    DWORD size, bytes;

    TRACE( "(%p, %s, %u, %p, %p, %#lx)\n", device, debugstr_BTH_LE_GATT_SERVICE( service ), count, buf, actual, flags );

    if (flags)
        FIXME( "Unsupported flags: %#lx\n", flags );

    if (!actual)
        return E_POINTER;

     if ((buf && !count))
          return E_INVALIDARG;

     size = offsetof( struct winebth_le_device_get_gatt_characteristics_params, characteristics[count] );
     chars = calloc( 1, size );
     if (!chars)
         return HRESULT_FROM_WIN32( ERROR_NO_SYSTEM_RESOURCES );
     if (service)
         chars->service = *service;
     if (!DeviceIoControl( device, IOCTL_WINEBTH_LE_DEVICE_GET_GATT_CHARACTERISTICS, chars, size, chars,
                               size, &bytes, NULL ) && GetLastError() != ERROR_MORE_DATA)
     {
         free( chars );
         return HRESULT_FROM_WIN32( GetLastError() );
     }

     *actual = chars->count;
     if (!chars->count)
     {
         free( chars );
         return S_OK;
     }
     if (!buf)
     {
         free( chars );
         return HRESULT_FROM_WIN32( ERROR_MORE_DATA );
     }
     memcpy( buf, chars->characteristics, min( count, chars->count ) * sizeof( *buf ) );
     if (count < chars->count)
         return HRESULT_FROM_WIN32( ERROR_INVALID_USER_BUFFER );
     free( chars );
     return S_OK;
}

HRESULT WINAPI BluetoothGATTGetCharacteristicValue( HANDLE device, BTH_LE_GATT_CHARACTERISTIC *chrc, ULONG size,
                                                    BTH_LE_GATT_CHARACTERISTIC_VALUE *val, USHORT *actual, ULONG flags )
{
    struct winebth_gatt_service_read_characterisitic_value_params *params;
    DWORD bytes, outsize, err;
    OVERLAPPED ovl = {0};
    HRESULT ret;

   TRACE( "(%p, %p, %lu, %p, %p, %#lx)\n", device, chrc, size, val, actual, flags );

    if (!device)
        return E_HANDLE;
    if (!chrc || !(size || val || actual) || (size && size < sizeof( *val )))
        return E_INVALIDARG;
    if (size && !val)
        return E_POINTER;
    outsize = size ? offsetof( struct winebth_gatt_service_read_characterisitic_value_params,
                               buf[size - offsetof( BTH_LE_GATT_CHARACTERISTIC_VALUE, Data )] )
                   : sizeof( *params );

    if (!(params = calloc( 1, outsize )))
        return HRESULT_FROM_WIN32( ERROR_NO_SYSTEM_RESOURCES );

    /* I couldn't reliably find out what the behaviour when both flags are present is. */
    if (flags & BLUETOOTH_GATT_FLAG_FORCE_READ_FROM_DEVICE)
        params->from_device = 1;
    if (flags & BLUETOOTH_GATT_FLAG_FORCE_READ_FROM_CACHE)
        params->from_device = 0;

    params->uuid = chrc->CharacteristicUuid;
    params->handle = chrc->AttributeHandle;
    ovl.hEvent = CreateEventW( NULL, TRUE, FALSE, NULL );
    err = ERROR_SUCCESS;
    if (!DeviceIoControl( device, IOCTL_WINEBTH_GATT_SERVICE_READ_CHARACTERISITIC_VALUE, params, sizeof( *params ),
                          params, outsize, &bytes, &ovl ))
    {
        err = GetLastError();
        if (err == ERROR_IO_PENDING)
        {
            err = ERROR_SUCCESS;
            if (!GetOverlappedResult( device, &ovl, &bytes, TRUE ))
                err = GetLastError();
        }
    }
    CloseHandle( ovl.hEvent );

    if (err)
    {
        free( params );
        return HRESULT_FROM_WIN32( err == ERROR_PRIVILEGE_NOT_HELD ? ERROR_INVALID_ACCESS : err );
    }

    ret = S_OK;
    *actual = max( offsetof( BTH_LE_GATT_CHARACTERISTIC_VALUE, Data[params->size] ), sizeof( *val ) );
    if (val)
    {
        if (size >= params->size)
        {
            val->DataSize = params->size;
            memcpy( val->Data, params->buf, params->size );
        }
        else
            ret = HRESULT_FROM_WIN32( ERROR_INVALID_USER_BUFFER );
    }
    else
        ret = HRESULT_FROM_WIN32( ERROR_MORE_DATA );
    free( params );
    return ret;
}
