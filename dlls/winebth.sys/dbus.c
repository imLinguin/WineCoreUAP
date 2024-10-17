/*
 * Support for communicating with BlueZ over DBus.
 *
 * Copyright 2024 Vibhav Pant
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


#if 0
#pragma makedep unix
#endif

#include "config.h"

#include <stdlib.h>
#include <dlfcn.h>
#include <assert.h>
#include <pthread.h>

#ifdef SONAME_LIBDBUS_1
#include <dbus/dbus.h>
#endif

#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <windef.h>
#include <winternl.h>
#include <winbase.h>
#include <bthsdpdef.h>
#include <bluetoothapis.h>

#include <wine/debug.h>

#include "winebth_priv.h"

#include "unixlib.h"
#include "unixlib_priv.h"
#include "dbus.h"

#ifdef SONAME_LIBDBUS_1

WINE_DEFAULT_DEBUG_CHANNEL( winebth );
WINE_DECLARE_DEBUG_CHANNEL( dbus );

const int bluez_timeout = -1;

#define DBUS_INTERFACE_OBJECTMANAGER "org.freedesktop.DBus.ObjectManager"
#define DBUS_OBJECTMANAGER_SIGNAL_INTERFACESADDED "InterfacesAdded"

#define DBUS_INTERFACES_ADDED_SIGNATURE                                                            \
    DBUS_TYPE_OBJECT_PATH_AS_STRING                                                                \
    DBUS_TYPE_ARRAY_AS_STRING                                                                      \
    DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING                                                           \
    DBUS_TYPE_STRING_AS_STRING                                                                     \
    DBUS_TYPE_ARRAY_AS_STRING                                                                      \
    DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING DBUS_TYPE_STRING_AS_STRING DBUS_TYPE_VARIANT_AS_STRING    \
    DBUS_DICT_ENTRY_END_CHAR_AS_STRING                                                             \
    DBUS_DICT_ENTRY_END_CHAR_AS_STRING

#define BLUEZ_DEST "org.bluez"
#define BLUEZ_INTERFACE_ADAPTER "org.bluez.Adapter1"

#define DO_FUNC( f ) typeof( f ) (*p_##f)
DBUS_FUNCS;
#undef DO_FUNC

static BOOL load_dbus_functions( void )
{
    void *handle = dlopen( SONAME_LIBDBUS_1, RTLD_NOW );

    if (handle == NULL) goto failed;

#define DO_FUNC( f )                                                                               \
    if (!( p_##f = dlsym( handle, #f ) ))                                                          \
    {                                                                                              \
    ERR( "failed to load symbol %s: %s\n", #f, dlerror() );                                        \
        goto failed;                                                                               \
    }
    DBUS_FUNCS;
#undef DO_FUNC
    return TRUE;

failed:
    WARN( "failed to load DBus support: %s\n", dlerror() );
    return FALSE;
}

static NTSTATUS bluez_dbus_error_to_ntstatus( const DBusError *error )
{

#define DBUS_ERROR_CASE(n, s) if (p_dbus_error_has_name( error, (n)) ) return (s)

    DBUS_ERROR_CASE( "org.bluez.Error.Failed", STATUS_INTERNAL_ERROR);
    DBUS_ERROR_CASE( "org.bluez.Error.NotReady", STATUS_DEVICE_NOT_READY );
    DBUS_ERROR_CASE( "org.bluez.Error.NotAuthorized", STATUS_ACCESS_DENIED );
    DBUS_ERROR_CASE( "org.bluez.Error.InvalidArguments", STATUS_INVALID_PARAMETER );
    DBUS_ERROR_CASE( "org.bluez.Error.AlreadyExists", STATUS_NO_MORE_ENTRIES );
    DBUS_ERROR_CASE( "org.bluez.Error.AuthenticationCanceled", STATUS_CANCELLED );
    DBUS_ERROR_CASE( "org.bluez.Error.AuthenticationFailed", STATUS_INTERNAL_ERROR );
    DBUS_ERROR_CASE( "org.bluez.Error.AuthenticationRejected", STATUS_INTERNAL_ERROR );
    DBUS_ERROR_CASE( "org.bluez.Error.AuthenticationTimeout", STATUS_TIMEOUT );
    DBUS_ERROR_CASE( "org.bluez.Error.ConnectionAttemptFailed", STATUS_DEVICE_NOT_CONNECTED);
    DBUS_ERROR_CASE( "org.bluez.Error.NotConnected", STATUS_DEVICE_NOT_CONNECTED );
    DBUS_ERROR_CASE( "org.bluez.Error.InProgress", STATUS_OPERATION_IN_PROGRESS );
    DBUS_ERROR_CASE( DBUS_ERROR_UNKNOWN_OBJECT, STATUS_INVALID_PARAMETER );
    DBUS_ERROR_CASE( DBUS_ERROR_NO_MEMORY, STATUS_NO_MEMORY );
    DBUS_ERROR_CASE( DBUS_ERROR_NOT_SUPPORTED, STATUS_NOT_SUPPORTED );
    DBUS_ERROR_CASE( DBUS_ERROR_ACCESS_DENIED, STATUS_ACCESS_DENIED );
    return STATUS_INTERNAL_ERROR;
#undef DBUS_ERROR_CASE
}

static const char *bluez_next_dict_entry( DBusMessageIter *iter, DBusMessageIter *variant )
{
    DBusMessageIter sub;
    const char *name;

    if (p_dbus_message_iter_get_arg_type( iter ) != DBUS_TYPE_DICT_ENTRY)
        return NULL;

    p_dbus_message_iter_recurse( iter, &sub );
    p_dbus_message_iter_next( iter );
    p_dbus_message_iter_get_basic( &sub, &name );
    p_dbus_message_iter_next( &sub );
    p_dbus_message_iter_recurse( &sub, variant );
    return name;
}

static const char *dbgstr_dbus_message( DBusMessage *message )
{
    const char *interface;
    const char *member;
    const char *path;
    const char *sender;
    const char *signature;
    int type;

    interface = p_dbus_message_get_interface( message );
    member = p_dbus_message_get_member( message );
    path = p_dbus_message_get_path( message );
    sender = p_dbus_message_get_sender( message );
    type = p_dbus_message_get_type( message );
    signature = p_dbus_message_get_signature( message );

    switch (type)
    {
    case DBUS_MESSAGE_TYPE_METHOD_CALL:
        return wine_dbg_sprintf( "{method_call sender=%s interface=%s member=%s path=%s signature=%s}",
                                 debugstr_a( sender ), debugstr_a( interface ), debugstr_a( member ),
                                 debugstr_a( path ), debugstr_a( signature ) );
    case DBUS_MESSAGE_TYPE_SIGNAL:
        return wine_dbg_sprintf( "{signal sender=%s interface=%s member=%s path=%s signature=%s}",
                                 debugstr_a( sender ), debugstr_a( interface ), debugstr_a( member ),
                                 debugstr_a( path ), debugstr_a( signature ) );
    default:
        return wine_dbg_sprintf( "%p", message );
    }
}

static inline const char *dbgstr_dbus_connection( DBusConnection *connection )
{
    return wine_dbg_sprintf( "{%p connected=%d}", connection,
                             p_dbus_connection_get_is_connected( connection ) );
}

static NTSTATUS bluez_get_objects_async( DBusConnection *connection, DBusPendingCall **call )
{
    DBusMessage *request;
    dbus_bool_t success;

    TRACE( "Getting managed objects under '/' at service '%s'\n", BLUEZ_DEST );
    request = p_dbus_message_new_method_call(
        BLUEZ_DEST, "/", DBUS_INTERFACE_OBJECTMANAGER, "GetManagedObjects" );
    if (!request)
    {
        return STATUS_NO_MEMORY;
    }

    success = p_dbus_connection_send_with_reply( connection, request, call, -1 );
    p_dbus_message_unref( request );
    if (!success)
        return STATUS_NO_MEMORY;

    if (*call == NULL)
        return STATUS_INVALID_PARAMETER;

    return STATUS_SUCCESS;
}

#define DBUS_OBJECTMANAGER_METHOD_GETMANAGEDOBJECTS_RETURN_SIGNATURE                               \
    DBUS_TYPE_ARRAY_AS_STRING                                                                      \
    DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING                                                           \
    DBUS_TYPE_OBJECT_PATH_AS_STRING                                                                \
    DBUS_TYPE_ARRAY_AS_STRING                                                                      \
    DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING                                                           \
    DBUS_TYPE_STRING_AS_STRING                                                                     \
    DBUS_TYPE_ARRAY_AS_STRING                                                                      \
    DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING                                                           \
    DBUS_TYPE_STRING_AS_STRING                                                                     \
    DBUS_TYPE_VARIANT_AS_STRING                                                                    \
    DBUS_DICT_ENTRY_END_CHAR_AS_STRING                                                             \
    DBUS_DICT_ENTRY_END_CHAR_AS_STRING                                                             \
    DBUS_DICT_ENTRY_END_CHAR_AS_STRING


static void parse_mac_address( const char *addr_str, BYTE dest[6] )
{
    int addr[6], i;

    sscanf( addr_str, "%x:%x:%x:%x:%x:%x", &addr[0], &addr[1], &addr[2], &addr[3], &addr[4],
            &addr[5] );
    for (i = 0 ; i < 6; i++)
        dest[i] = addr[i];
}

static void bluez_radio_prop_from_dict_entry( const char *prop_name, DBusMessageIter *variant,
                                              struct winebluetooth_radio_properties *props,
                                              winebluetooth_radio_props_mask_t *props_mask,
                                              winebluetooth_radio_props_mask_t wanted_props_mask )
{
    TRACE_(dbus)( "(%s, %p, %p, %p, %#x)\n", debugstr_a( prop_name ), variant, props, props_mask,
                  wanted_props_mask );

    if (wanted_props_mask & WINEBLUETOOTH_RADIO_PROPERTY_ADDRESS &&
        !strcmp( prop_name, "Address" ) &&
        p_dbus_message_iter_get_arg_type( variant ) == DBUS_TYPE_STRING)
    {
        const char *addr_str;
        p_dbus_message_iter_get_basic( variant, &addr_str );
        parse_mac_address( addr_str, props->address.rgBytes );
        *props_mask |= WINEBLUETOOTH_RADIO_PROPERTY_ADDRESS;
    }
    else if (wanted_props_mask & WINEBLUETOOTH_RADIO_PROPERTY_CLASS &&
             !strcmp( prop_name, "Class" ) &&
             p_dbus_message_iter_get_arg_type( variant ) == DBUS_TYPE_UINT32)
    {
        dbus_uint32_t class;
        p_dbus_message_iter_get_basic( variant, &class );
        props->class = class;
        *props_mask |= WINEBLUETOOTH_RADIO_PROPERTY_CLASS;
    }
    else if (wanted_props_mask & WINEBLUETOOTH_RADIO_PROPERTY_MANUFACTURER &&
             !strcmp( prop_name, "Manufacturer" ) &&
             p_dbus_message_iter_get_arg_type( variant ) == DBUS_TYPE_UINT16)
    {
        dbus_uint16_t manufacturer;
        p_dbus_message_iter_get_basic( variant, &manufacturer );
        props->manufacturer = manufacturer;
        *props_mask |= WINEBLUETOOTH_RADIO_PROPERTY_MANUFACTURER;
    }
    else if (wanted_props_mask & WINEBLUETOOTH_RADIO_PROPERTY_CONNECTABLE &&
             !strcmp( prop_name, "Connectable" ) &&
             p_dbus_message_iter_get_arg_type( variant ) == DBUS_TYPE_BOOLEAN)
    {
        dbus_bool_t connectable;
        p_dbus_message_iter_get_basic( variant, &connectable );
        props->connectable = connectable != 0;
        *props_mask |= WINEBLUETOOTH_RADIO_PROPERTY_CONNECTABLE;
    }
    else if (wanted_props_mask & WINEBLUETOOTH_RADIO_PROPERTY_DISCOVERABLE &&
             !strcmp( prop_name, "Discoverable" ) &&
             p_dbus_message_iter_get_arg_type( variant ) == DBUS_TYPE_BOOLEAN)
    {
        dbus_bool_t discoverable;
        p_dbus_message_iter_get_basic( variant, &discoverable );
        props->discoverable = discoverable != 0;
        *props_mask |= WINEBLUETOOTH_RADIO_PROPERTY_DISCOVERABLE;
    }
    else if (wanted_props_mask & WINEBLUETOOTH_RADIO_PROPERTY_DISCOVERING &&
             !strcmp( prop_name, "Discovering") &&
             p_dbus_message_iter_get_arg_type( variant ) == DBUS_TYPE_BOOLEAN)
    {
        dbus_bool_t discovering;
        p_dbus_message_iter_get_basic( variant, &discovering );
        props->discovering = discovering != 0;
        *props_mask |= WINEBLUETOOTH_RADIO_PROPERTY_DISCOVERING;
    }
    else if (wanted_props_mask & WINEBLUETOOTH_RADIO_PROPERTY_PAIRABLE &&
             !strcmp( prop_name, "Pairable" ) &&
             p_dbus_message_iter_get_arg_type( variant ) == DBUS_TYPE_BOOLEAN)
    {
        dbus_bool_t pairable;
        p_dbus_message_iter_get_basic( variant, &pairable );
        props->pairable = pairable != 0;
        *props_mask |= WINEBLUETOOTH_RADIO_PROPERTY_PAIRABLE;
    }
    else if (wanted_props_mask & WINEBLUETOOTH_RADIO_PROPERTY_VERSION &&
             !strcmp( prop_name, "Version" ) &&
             p_dbus_message_iter_get_arg_type( variant ) == DBUS_TYPE_BYTE)
    {
        p_dbus_message_iter_get_basic( variant, &props->version );
        *props_mask |= WINEBLUETOOTH_RADIO_PROPERTY_VERSION;
    }
}

struct bluez_watcher_ctx
{
    void *init_device_list_call;

    /* struct bluez_init_entry */
    struct list initial_radio_list;

    /* struct bluez_watcher_event */
    struct list event_list;
};

void *bluez_dbus_init( void )
{
    DBusError error;
    DBusConnection *connection;

    if (!load_dbus_functions()) return NULL;

    p_dbus_threads_init_default();
    p_dbus_error_init ( &error );

    connection = p_dbus_bus_get_private ( DBUS_BUS_SYSTEM, &error );
    if (!connection)
    {
        ERR( "Failed to get system dbus connection: %s: %s\n", debugstr_a( error.name ), debugstr_a( error.message ) );
        p_dbus_error_free( &error );
        return NULL;
    }

    return connection;
}

void bluez_dbus_close( void *connection )
{
    TRACE_(dbus)( "(%s)\n", dbgstr_dbus_connection( connection ) );

    p_dbus_connection_flush( connection );
    p_dbus_connection_close( connection );
}

void bluez_dbus_free( void *connection )
{
    TRACE_(dbus)( "(%s)\n", dbgstr_dbus_connection( connection ) );

    p_dbus_connection_unref( connection );
}

struct bluez_watcher_event
{
    struct list entry;
    enum winebluetooth_watcher_event_type event_type;
    union winebluetooth_watcher_event_data event;
};

static BOOL bluez_event_list_queue_new_event( struct list *event_list,
                                              enum winebluetooth_watcher_event_type event_type,
                                              union winebluetooth_watcher_event_data event )
{
    struct bluez_watcher_event *event_entry;

    event_entry = calloc( 1,  sizeof( *event_entry ) );
    if (!event_entry)
    {
        ERR( "Could not allocate memory for DBus event.\n" );
        return FALSE;
    }

    event_entry->event_type = event_type;
    event_entry->event = event;
    list_add_tail( event_list, &event_entry->entry );

    return TRUE;
}

static DBusHandlerResult bluez_filter( DBusConnection *conn, DBusMessage *msg, void *user_data )
{
    struct list *event_list;

    if (TRACE_ON( dbus ))
        TRACE_( dbus )( "(%s, %s, %p)\n", dbgstr_dbus_connection( conn ), dbgstr_dbus_message( msg ), user_data );

    event_list = &((struct bluez_watcher_ctx *)user_data)->event_list;

    if (p_dbus_message_is_signal( msg, DBUS_INTERFACE_OBJECTMANAGER, DBUS_OBJECTMANAGER_SIGNAL_INTERFACESADDED )
        && p_dbus_message_has_signature( msg, DBUS_INTERFACES_ADDED_SIGNATURE ))
    {
        DBusMessageIter iter, ifaces_iter;
        const char *object_path;

        p_dbus_message_iter_init( msg, &iter );
        p_dbus_message_iter_get_basic( &iter, &object_path );
        p_dbus_message_iter_next( &iter );
        p_dbus_message_iter_recurse( &iter, &ifaces_iter );
        while (p_dbus_message_iter_has_next( &ifaces_iter ))
        {
            DBusMessageIter iface_entry;
            const char *iface_name;

            p_dbus_message_iter_recurse( &ifaces_iter, &iface_entry );
            p_dbus_message_iter_get_basic( &iface_entry, &iface_name );
            if (!strcmp( iface_name, BLUEZ_INTERFACE_ADAPTER ))
            {
                struct winebluetooth_watcher_event_radio_added radio_added = {0};
                struct unix_name *radio;
                DBusMessageIter props_iter, variant;
                const char *prop_name;

                p_dbus_message_iter_next( &iface_entry );
                p_dbus_message_iter_recurse( &iface_entry, &props_iter );

                while((prop_name = bluez_next_dict_entry( &props_iter, &variant )))
                {
                    bluez_radio_prop_from_dict_entry( prop_name, &variant, &radio_added.props,
                                                      &radio_added.props_mask,
                                                      WINEBLUETOOTH_RADIO_ALL_PROPERTIES );
                }

                radio = unix_name_get_or_create( object_path );
                radio_added.radio.handle = (UINT_PTR)radio;
                if (!radio_added.radio.handle)
                {
                    ERR( "failed to allocate memory for adapter path %s\n", debugstr_a( object_path ) );
                    break;
                }
                else
                {
                    union winebluetooth_watcher_event_data event = { .radio_added = radio_added };
                    TRACE( "New BlueZ org.bluez.Adapter1 object added at %s: %p\n",
                           debugstr_a( object_path ), radio );
                    if (!bluez_event_list_queue_new_event(
                            event_list, BLUETOOTH_WATCHER_EVENT_TYPE_RADIO_ADDED, event ))
                        unix_name_free( radio );
                }
            }
            p_dbus_message_iter_next( &ifaces_iter );
        }
    }

    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static const char BLUEZ_MATCH_OBJECTMANAGER[] = "type='signal',"
                                                "interface='org.freedesktop.DBus.ObjectManager',"
                                                "sender='"BLUEZ_DEST"',"
                                                "path='/'";

static const char *BLUEZ_MATCH_RULES[] = { BLUEZ_MATCH_OBJECTMANAGER };

NTSTATUS bluez_watcher_init( void *connection, void **ctx )
{
    DBusError err;
    NTSTATUS status;
    DBusPendingCall *call;
    struct bluez_watcher_ctx *watcher_ctx =
        calloc( 1, sizeof( struct bluez_watcher_ctx ) );
    SIZE_T i;

    if (watcher_ctx == NULL) return STATUS_NO_MEMORY;
    status = bluez_get_objects_async( connection, &call );
    if (status != STATUS_SUCCESS)
    {
        free( watcher_ctx );
        ERR( "could not create async GetManagedObjects call: %#x\n", (int)status);
        return status;
    }
    watcher_ctx->init_device_list_call = call;
    list_init( &watcher_ctx->initial_radio_list );

    list_init( &watcher_ctx->event_list );

    if (!p_dbus_connection_add_filter( connection, bluez_filter, watcher_ctx, free ))
    {
        p_dbus_pending_call_cancel( call );
        p_dbus_pending_call_unref( call );
        free( watcher_ctx );
        ERR( "Could not add DBus filter\n" );
        return STATUS_NO_MEMORY;
    }
    p_dbus_error_init( &err );
    for (i = 0; i < ARRAY_SIZE( BLUEZ_MATCH_RULES ); i++)
    {
        TRACE( "Adding DBus match rule %s\n", debugstr_a( BLUEZ_MATCH_RULES[i] ) );

        p_dbus_bus_add_match( connection, BLUEZ_MATCH_RULES[i], &err );
        if (p_dbus_error_is_set( &err ))
        {
            NTSTATUS status = bluez_dbus_error_to_ntstatus( &err );
            ERR( "Could not add DBus match %s: %s: %s\n", debugstr_a( BLUEZ_MATCH_RULES[i] ), debugstr_a( err.name ),
                 debugstr_a( err.message ) );
            p_dbus_pending_call_cancel( call );
            p_dbus_pending_call_unref( call );
            p_dbus_error_free( &err );
            free( watcher_ctx );
            return status;
        }
    }
    p_dbus_error_free( &err );
    *ctx = watcher_ctx;
    TRACE( "ctx=%p\n", ctx );
    return STATUS_SUCCESS;
}

void bluez_watcher_close( void *connection, void *ctx )
{
    p_dbus_bus_remove_match( connection, BLUEZ_MATCH_OBJECTMANAGER, NULL );
    p_dbus_connection_remove_filter( connection, bluez_filter, ctx );
}

struct bluez_init_entry
{
    union {
        struct winebluetooth_watcher_event_radio_added radio;
    } object;
    struct list entry;
};

static NTSTATUS bluez_build_initial_device_lists( DBusMessage *reply, struct list *adapter_list )
{
    DBusMessageIter dict, paths_iter, iface_iter, prop_iter;
    const char *path;
    NTSTATUS status = STATUS_SUCCESS;

    if (!p_dbus_message_has_signature( reply,
                                       DBUS_OBJECTMANAGER_METHOD_GETMANAGEDOBJECTS_RETURN_SIGNATURE ))
    {
        ERR( "Unexpected signature in GetManagedObjects reply: %s\n",
             debugstr_a( p_dbus_message_get_signature( reply ) ) );
        return STATUS_INTERNAL_ERROR;
    }

    p_dbus_message_iter_init( reply, &dict );
    p_dbus_message_iter_recurse( &dict, &paths_iter );
    while((path = bluez_next_dict_entry( &paths_iter, &iface_iter )))
    {
        const char *iface;
        while ((iface = bluez_next_dict_entry ( &iface_iter, &prop_iter )))
        {
            if (!strcmp( iface, BLUEZ_INTERFACE_ADAPTER ))
            {
                const char *prop_name;
                DBusMessageIter variant;
                struct bluez_init_entry *init_device = calloc( 1, sizeof( *init_device ) );
                struct unix_name *radio_name;

                if (!init_device)
                {
                    status = STATUS_NO_MEMORY;
                    goto done;
                }
                radio_name = unix_name_get_or_create( path );
                if (!radio_name)
                {
                    free( init_device );
                    status = STATUS_NO_MEMORY;
                    goto done;
                }
                while ((prop_name = bluez_next_dict_entry( &prop_iter, &variant )))
                {
                    bluez_radio_prop_from_dict_entry(
                        prop_name, &variant, &init_device->object.radio.props,
                        &init_device->object.radio.props_mask, WINEBLUETOOTH_RADIO_ALL_PROPERTIES );
                }
                init_device->object.radio.radio.handle = (UINT_PTR)radio_name;
                list_add_tail( adapter_list, &init_device->entry );
                TRACE( "Found BlueZ org.bluez.Adapter1 object %s: %p\n",
                       debugstr_a( radio_name->str ), radio_name );
                break;
            }
        }
    }

    TRACE( "Initial device list: radios: %d\n", list_count( adapter_list ) );
 done:
    return status;
}

static BOOL bluez_watcher_event_queue_ready( struct bluez_watcher_ctx *ctx, struct winebluetooth_watcher_event *event )
{
    if (!list_empty( &ctx->initial_radio_list ))
    {
        struct bluez_init_entry *radio;

        radio = LIST_ENTRY( list_head( &ctx->initial_radio_list ), struct bluez_init_entry, entry );
        event->event_type = BLUETOOTH_WATCHER_EVENT_TYPE_RADIO_ADDED;
        event->event_data.radio_added = radio->object.radio;
        list_remove( &radio->entry );
        free( radio );
        return TRUE;
    }
    if (!list_empty( &ctx->event_list ))
    {
        struct bluez_watcher_event *event =
            LIST_ENTRY( list_head( &ctx->event_list ), struct bluez_watcher_event, entry );
        event->event_type = event->event_type;
        event->event = event->event;
        list_remove( &event->entry );
        free( event );
        return TRUE;
    }
    return FALSE;
}

NTSTATUS bluez_dbus_loop( void *c, void *watcher,
                          struct winebluetooth_event *result )
{
    DBusConnection *connection;
    struct bluez_watcher_ctx *watcher_ctx = watcher;

    TRACE( "(%p, %p, %p)\n", c, watcher, result );
    connection = p_dbus_connection_ref( c );

    while (TRUE)
    {
        if (bluez_watcher_event_queue_ready( watcher_ctx, &result->data.watcher_event ))
        {
            result->status = WINEBLUETOOTH_EVENT_WATCHER_EVENT;
            p_dbus_connection_unref( connection );
            return STATUS_PENDING;
        }
        else if (!p_dbus_connection_read_write_dispatch( connection, 100 ))
        {
            p_dbus_connection_unref( connection );
            TRACE( "Disconnected from DBus\n" );
            return STATUS_SUCCESS;
        }

        if (watcher_ctx->init_device_list_call != NULL
            && p_dbus_pending_call_get_completed( watcher_ctx->init_device_list_call ))
        {
            DBusMessage *reply = p_dbus_pending_call_steal_reply( watcher_ctx->init_device_list_call );
            DBusError error;
            NTSTATUS status;

            p_dbus_pending_call_unref( watcher_ctx->init_device_list_call );
            watcher_ctx->init_device_list_call = NULL;

            p_dbus_error_init( &error );
            if (p_dbus_set_error_from_message( &error, reply ))
            {
                ERR( "Error getting object list from BlueZ: '%s': '%s'\n", error.name,
                     error.message );
                p_dbus_error_free( &error );
                p_dbus_message_unref( reply );
                p_dbus_connection_unref( connection );
                return STATUS_NO_MEMORY;
            }
            status = bluez_build_initial_device_lists( reply, &watcher_ctx->initial_radio_list );
            p_dbus_message_unref( reply );
            if (status != STATUS_SUCCESS)
            {
                ERR( "Error building initial bluetooth devices list: %#x\n", (int)status );
                p_dbus_connection_unref( connection );
                return status;
            }
        }
    }
}
#else

void *bluez_dbus_init( void ) { return NULL; }
void bluez_dbus_close( void *connection ) {}
void bluez_dbus_free( void *connection ) {}
NTSTATUS bluez_watcher_init( void *connection, void **ctx ) { return STATUS_NOT_SUPPORTED; }
NTSTATUS bluez_dbus_loop( void *c, void *watcher, struct winebluetooth_event *result )
{
    return STATUS_NOT_SUPPORTED;
}

#endif /* SONAME_LIBDBUS_1 */
