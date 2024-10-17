/* WinRT Windows.Storage.FileIO Implementation
 *
 * Written by Weather
 *
 * This is a reverse engineered implementation of Microsoft's OneCoreUAP binaries.
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

#include "FileIOInternal.h"

HRESULT WINAPI file_io_statics_ReadText( IUnknown *invoker, IUnknown *param, PROPVARIANT *result )
{
    HRESULT status = S_OK;
    HSTRING filePath;
    HANDLE fileHandle;
    LPCSTR fileBufferChar;
    LPWSTR fileBufferWChar;
    LPWSTR outputBuffer;
    DWORD fileSize;
    DWORD bytesRead;
    ULONG i;
    BOOL readResult;

    struct storage_item *fileItem;

    struct file_io_read_text_options *read_text_options = (struct file_io_read_text_options *)param;

    //Parameters
    struct storage_file *file = impl_from_IStorageFile( read_text_options->file );
    UnicodeEncoding unicodeEncoding = read_text_options->encoding;

    fileItem = impl_from_IStorageItem( &file->IStorageItem_iface );
    WindowsDuplicateString( fileItem->Path, &filePath );

    fileHandle = CreateFileW( WindowsGetStringRawBuffer( filePath, NULL ), GENERIC_READ, 0 , NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );

    fileSize = GetFileSize( fileHandle, NULL );

    if ( fileSize == INVALID_FILE_SIZE )
    {
        CloseHandle( fileHandle );
        status = E_INVALIDARG;
    }

    if ( FAILED( status ) )
        return status;

    outputBuffer = (LPWSTR)malloc( fileSize );

    if ( !outputBuffer )
    {
        CloseHandle( fileHandle );
        status = E_OUTOFMEMORY;
    }

    if ( FAILED( status ) )
        return status;

    if ( unicodeEncoding == UnicodeEncoding_Utf8 )
    {
        fileBufferChar = (LPCSTR)malloc( fileSize );
        if ( !fileBufferChar )
        {
            CloseHandle( fileHandle );
            status = E_OUTOFMEMORY;
        }
        readResult = ReadFile( fileHandle, (LPVOID)fileBufferChar, fileSize, &bytesRead, NULL );
    } else
    {
        fileBufferWChar = (LPWSTR)malloc( fileSize );
        if ( !fileBufferWChar )
        {
            CloseHandle( fileHandle );
            status = E_OUTOFMEMORY;
        }
        readResult = ReadFile( fileHandle, (LPVOID)fileBufferWChar, fileSize, &bytesRead, NULL );
    }

    if ( !readResult || bytesRead != fileSize )
    {
        CloseHandle( fileHandle );
        status = E_UNEXPECTED;
    }

    if ( unicodeEncoding != UnicodeEncoding_Utf8 )
    {
        if ( fileSize % 2 != 0 )
        {
            CloseHandle( fileHandle );
            status = E_INVALIDARG;
        }
    }

    if ( FAILED( status ) )
        return status;

    CloseHandle( fileHandle );

    switch ( unicodeEncoding )
    {
        case UnicodeEncoding_Utf8:
            MultiByteToWideChar( CP_UTF8, 0, fileBufferChar, -1, outputBuffer, fileSize );
            if ( !outputBuffer )
                status = E_OUTOFMEMORY;
            break;

        case UnicodeEncoding_Utf16LE:
            wcscpy( outputBuffer, fileBufferWChar );
            break;

        case UnicodeEncoding_Utf16BE:
            for ( i = 0; i < fileSize / sizeof( WCHAR ); i++ )
                fileBufferWChar[i] = ( fileBufferWChar[i] >> 8 ) | ( fileBufferWChar[i] << 8 );

            wcscpy( outputBuffer, fileBufferWChar );
            break;

        default:
            status = E_INVALIDARG;
    }

    outputBuffer[ fileSize ] = '\0';

    if ( SUCCEEDED ( status ) )
    {
        result->vt = VT_LPWSTR;
        result->pwszVal = outputBuffer;
    }

    return status;
}

HRESULT WINAPI file_io_statics_WriteText( IUnknown *invoker, IUnknown *param, PROPVARIANT *result )
{
    HRESULT status = S_OK;
    HSTRING filePath;
    HSTRING contents;
    HANDLE fileHandle;
    LPSTR writeBufferChar;
    LPWSTR contentsBE;
    DWORD bytesWritten;
    ULONG i;
    BYTE UTF16LEBOM[] = { 0xFF, 0xFE };
    BYTE UTF16BEBOM[] = { 0xFE, 0xFF };

    struct storage_item *fileItem;

    struct file_io_write_text_options *write_text_options = (struct file_io_write_text_options *)param;

    //Parameters
    struct storage_file *file = impl_from_IStorageFile( write_text_options->file );
    UnicodeEncoding unicodeEncoding = write_text_options->encoding;
    WindowsDuplicateString( write_text_options->contents, &contents );

    fileItem = impl_from_IStorageItem( &file->IStorageItem_iface );
    WindowsDuplicateString( fileItem->Path, &filePath );

    fileHandle = CreateFileW( WindowsGetStringRawBuffer( filePath, NULL ), GENERIC_WRITE, 0 , NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );    
    //Clear the file
    if ( SetFilePointer( fileHandle, 0, NULL, FILE_BEGIN ) == INVALID_SET_FILE_POINTER )
    {
        CloseHandle( fileHandle );
        status = E_UNEXPECTED;
    }

    if ( !SetEndOfFile( fileHandle ) ) 
    {
        CloseHandle( fileHandle );
        status = E_UNEXPECTED;
    }

    if ( FAILED( status ) )
        return status;

    switch ( unicodeEncoding )
    {
        case UnicodeEncoding_Utf8:
            writeBufferChar = (LPSTR)malloc( wcslen( WindowsGetStringRawBuffer( contents, NULL ) ) + 1 );
            if ( !writeBufferChar )
            {
                CloseHandle( fileHandle );
                status = E_OUTOFMEMORY;
            }

            WideCharToMultiByte( CP_UTF8, 0, WindowsGetStringRawBuffer( contents, NULL ), -1, writeBufferChar, wcslen( WindowsGetStringRawBuffer( contents, NULL ) ), NULL, NULL );
            
            if ( !WriteFile( fileHandle, (LPCVOID)writeBufferChar, wcslen( WindowsGetStringRawBuffer( contents, NULL ) ), &bytesWritten, NULL ) )
            {
                CloseHandle( fileHandle );
                status = E_UNEXPECTED;
            }
            break;

        case UnicodeEncoding_Utf16LE:
            if ( !WriteFile( fileHandle, (LPCVOID)UTF16LEBOM, sizeof( UTF16LEBOM ), &bytesWritten, NULL ) )
            {
                CloseHandle( fileHandle );
               status = E_UNEXPECTED;
            }

            if ( FAILED( status ) )
                return status;

            if ( !WriteFile( fileHandle, (LPCVOID)WindowsGetStringRawBuffer( contents, NULL ), 
                        ( wcslen( WindowsGetStringRawBuffer( contents, NULL ) ) + 1 ) * sizeof( WCHAR ), &bytesWritten, NULL) )
            {
                CloseHandle( fileHandle );
                status = E_UNEXPECTED;
            }
            break;

        case UnicodeEncoding_Utf16BE:
            contentsBE = (LPWSTR)malloc( wcslen( WindowsGetStringRawBuffer( contents, NULL ) ) + 1 );
            if ( !contentsBE )
            {
                status = E_OUTOFMEMORY;
            }

            if ( !WriteFile( fileHandle, (LPCVOID)UTF16BEBOM, sizeof( UTF16BEBOM ), &bytesWritten, NULL ) )
            {
                CloseHandle( fileHandle );
                status = E_UNEXPECTED;
            }

            if ( FAILED( status ) )
                return status;

            for ( i = 0; i < wcslen( WindowsGetStringRawBuffer( contents, NULL ) ); i++ ) 
                contentsBE[i] = ( WindowsGetStringRawBuffer( contents, NULL )[i] >> 8) | ( WindowsGetStringRawBuffer( contents, NULL )[i] << 8 );

            if ( !WriteFile( fileHandle, (LPCVOID)contentsBE, 
                        ( wcslen( contentsBE ) + 1 ) * sizeof( WCHAR ), &bytesWritten, NULL) ) {
                CloseHandle( fileHandle );
                status = E_UNEXPECTED;
            }
            break;

        default:
            status = E_INVALIDARG;
    }

    CloseHandle( fileHandle );

    return status;
}

HRESULT WINAPI file_io_statics_AppendText( IUnknown *invoker, IUnknown *param, PROPVARIANT *result )
{
    HRESULT status = S_OK;
    HSTRING filePath;
    HSTRING contents;
    HANDLE fileHandle;
    LPSTR writeBufferChar;
    LPWSTR contentsBE;
    DWORD bytesWritten;
    ULONG i;
    BYTE UTF16LEBOM[] = { 0xFF, 0xFE };
    BYTE UTF16BEBOM[] = { 0xFE, 0xFF };

    struct storage_item *fileItem;

    struct file_io_write_text_options *write_text_options = (struct file_io_write_text_options *)param;

    //Parameters
    struct storage_file *file = impl_from_IStorageFile( write_text_options->file );
    UnicodeEncoding unicodeEncoding = write_text_options->encoding;
    WindowsDuplicateString( write_text_options->contents, &contents );

    fileItem = impl_from_IStorageItem( &file->IStorageItem_iface );
    WindowsDuplicateString( fileItem->Path, &filePath );

    fileHandle = CreateFileW( WindowsGetStringRawBuffer( filePath, NULL ), FILE_APPEND_DATA, 0 , NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );    

    switch ( unicodeEncoding )
    {
        case UnicodeEncoding_Utf8:
            writeBufferChar = (LPSTR)malloc( wcslen( WindowsGetStringRawBuffer( contents, NULL ) ) + 1 );
            if ( !writeBufferChar )
            {
                CloseHandle( fileHandle );
                status = E_OUTOFMEMORY;
            }

            WideCharToMultiByte( CP_UTF8, 0, WindowsGetStringRawBuffer( contents, NULL ), -1, writeBufferChar, wcslen( WindowsGetStringRawBuffer( contents, NULL ) ), NULL, NULL );
            
            if ( !WriteFile( fileHandle, (LPCVOID)writeBufferChar, wcslen( WindowsGetStringRawBuffer( contents, NULL ) ), &bytesWritten, NULL ) )
            {
                CloseHandle( fileHandle );
                status = E_UNEXPECTED;
            }
            break;

        case UnicodeEncoding_Utf16LE:
            if ( !WriteFile( fileHandle, (LPCVOID)UTF16LEBOM, sizeof( UTF16LEBOM ), &bytesWritten, NULL ) )
            {
                CloseHandle( fileHandle );
               status = E_UNEXPECTED;
            }

            if ( FAILED( status ) )
                return status;

            if ( !WriteFile( fileHandle, (LPCVOID)WindowsGetStringRawBuffer( contents, NULL ), 
                        ( wcslen( WindowsGetStringRawBuffer( contents, NULL ) ) + 1 ) * sizeof( WCHAR ), &bytesWritten, NULL) )
            {
                CloseHandle( fileHandle );
                status = E_UNEXPECTED;
            }
            break;

        case UnicodeEncoding_Utf16BE:
            contentsBE = (LPWSTR)malloc( wcslen( WindowsGetStringRawBuffer( contents, NULL ) ) + 1 );
            if ( !contentsBE )
            {
                status = E_OUTOFMEMORY;
            }

            if ( !WriteFile( fileHandle, (LPCVOID)UTF16BEBOM, sizeof( UTF16BEBOM ), &bytesWritten, NULL ) )
            {
                CloseHandle( fileHandle );
                status = E_UNEXPECTED;
            }

            if ( FAILED( status ) )
                return status;

            for ( i = 0; i < wcslen( WindowsGetStringRawBuffer( contents, NULL ) ); i++ ) 
                contentsBE[i] = ( WindowsGetStringRawBuffer( contents, NULL )[i] >> 8) | ( WindowsGetStringRawBuffer( contents, NULL )[i] << 8 );

            if ( !WriteFile( fileHandle, (LPCVOID)contentsBE, 
                        ( wcslen( contentsBE ) + 1 ) * sizeof( WCHAR ), &bytesWritten, NULL) ) {
                CloseHandle( fileHandle );
                status = E_UNEXPECTED;
            }
            break;

        default:
            status = E_INVALIDARG;
    }

    CloseHandle( fileHandle );

    return status;
}

//I only managed to hit my head against the wall twice while writing this function

HRESULT WINAPI file_io_statics_ReadLines( IUnknown *invoker, IUnknown *param, PROPVARIANT *result )
{
    HRESULT status = S_OK;
    HSTRING filePath;
    HANDLE fileHandle;
    LPCSTR fileBufferChar;
    LPWSTR fileBufferWChar;
    LPWSTR outputBuffer;
    LPWSTR tmpBuffer;
    DWORD fileSize;
    DWORD bytesRead;
    ULONG i;
    ULONG currChar;
    BOOL readResult;
    size_t INITIAL_BUFFER_SIZE = 100;

    struct storage_item *fileItem;
    struct hstring_vector *HSTRINGVector;

    struct file_io_read_text_options *read_text_options = (struct file_io_read_text_options *)param;

    //Parameters
    struct storage_file *file = impl_from_IStorageFile( read_text_options->file );
    UnicodeEncoding unicodeEncoding = read_text_options->encoding;

    if (!(HSTRINGVector = calloc( 1, sizeof(*HSTRINGVector) ))) return E_OUTOFMEMORY;
    HSTRINGVector->IVector_HSTRING_iface.lpVtbl = &hstring_vector_vtbl;
    HSTRINGVector->size = 0;

    fileItem = impl_from_IStorageItem( &file->IStorageItem_iface );
    WindowsDuplicateString( fileItem->Path, &filePath );

    fileHandle = CreateFileW( WindowsGetStringRawBuffer( filePath, NULL ), GENERIC_READ, 0 , NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );

    fileSize = GetFileSize( fileHandle, NULL );

    if ( fileSize == INVALID_FILE_SIZE )
    {
        CloseHandle( fileHandle );
        status = E_INVALIDARG;
    }

    if ( FAILED( status ) )
        return status;

    outputBuffer = (LPWSTR)malloc( fileSize );

    if ( !outputBuffer )
    {
        CloseHandle( fileHandle );
        status = E_OUTOFMEMORY;
    }

    tmpBuffer = (LPWSTR)malloc( sizeof(wchar_t) * INITIAL_BUFFER_SIZE );

    if ( !tmpBuffer )
    {
        CloseHandle( fileHandle );
        status = E_OUTOFMEMORY;
    }

    if ( FAILED( status ) )
        return status;

    if ( unicodeEncoding == UnicodeEncoding_Utf8 )
    {
        fileBufferChar = (LPCSTR)malloc( fileSize );
        if ( !fileBufferChar )
        {
            CloseHandle( fileHandle );
            status = E_OUTOFMEMORY;
        }
        readResult = ReadFile( fileHandle, (LPVOID)fileBufferChar, fileSize, &bytesRead, NULL );
    } else
    {
        fileBufferWChar = (LPWSTR)malloc( fileSize );
        if ( !fileBufferWChar )
        {
            CloseHandle( fileHandle );
            status = E_OUTOFMEMORY;
        }
        readResult = ReadFile( fileHandle, (LPVOID)fileBufferWChar, fileSize, &bytesRead, NULL );
    }

    if ( !readResult || bytesRead != fileSize )
    {
        CloseHandle( fileHandle );
        status = E_UNEXPECTED;
    }

    if ( unicodeEncoding != UnicodeEncoding_Utf8 )
    {
        if ( fileSize % 2 != 0 )
        {
            CloseHandle( fileHandle );
            status = E_INVALIDARG;
        }
    }

    if ( FAILED( status ) )
        return status;

    CloseHandle( fileHandle );

    switch ( unicodeEncoding )
    {
        case UnicodeEncoding_Utf8:
            MultiByteToWideChar( CP_UTF8, 0, fileBufferChar, -1, outputBuffer, fileSize );
            if ( !outputBuffer )
                status = E_OUTOFMEMORY;
            break;

        case UnicodeEncoding_Utf16LE:
            wcscpy( outputBuffer, fileBufferWChar );
            break;

        case UnicodeEncoding_Utf16BE:
            for ( i = 0; i < fileSize / sizeof( WCHAR ); i++ )
                fileBufferWChar[i] = ( fileBufferWChar[i] >> 8 ) | ( fileBufferWChar[i] << 8 );

            wcscpy( outputBuffer, fileBufferWChar );
            break;

        default:
            status = E_INVALIDARG;
    }

    outputBuffer[ fileSize ] = L'\0';
    tmpBuffer[0] = L'\0';

    for ( currChar = 0; currChar < wcslen( outputBuffer ); currChar++ )
    {
        if ( outputBuffer[ currChar ] == L'\n' )
        {
            HSTRINGVector->size++;
            HSTRINGVector->elements = (HSTRING*)realloc(HSTRINGVector->elements, (HSTRINGVector->size) * sizeof(HSTRING));
            WindowsCreateString( tmpBuffer, wcslen( tmpBuffer ), &HSTRINGVector->elements[ HSTRINGVector->size - 1 ] );
            tmpBuffer = (LPWSTR)malloc(sizeof(wchar_t) * INITIAL_BUFFER_SIZE);
        } else
        {
            //Append the current character to tmpBuffer. We'll use tmpBuffer once control reaches line break.
            size_t tmpBufferLen = wcslen(tmpBuffer);
            if (tmpBufferLen + 1 >= INITIAL_BUFFER_SIZE) {
                // Grow the buffer if necessary.
                INITIAL_BUFFER_SIZE *= 2;
                tmpBuffer = (LPWSTR)realloc(tmpBuffer, sizeof(wchar_t) * INITIAL_BUFFER_SIZE);
            }
            tmpBuffer[ tmpBufferLen ] = outputBuffer[ currChar ];
            tmpBuffer[ tmpBufferLen + 1 ] = L'\0';
        }
    }

    if ( tmpBuffer )
    {
        HSTRINGVector->size++;
        HSTRINGVector->elements = (HSTRING*)realloc(HSTRINGVector->elements, (HSTRINGVector->size) * sizeof(HSTRING));
        WindowsCreateString( tmpBuffer, wcslen( tmpBuffer ), &HSTRINGVector->elements[ HSTRINGVector->size - 1] );
    }

    if ( SUCCEEDED ( status ) )
    {
        result->vt = VT_UNKNOWN;
        result->ppunkVal = (IUnknown **)&HSTRINGVector->IVector_HSTRING_iface;
    }

    return status;
}

HRESULT WINAPI file_io_statics_ReadBuffer( IUnknown *invoker, IUnknown *param, PROPVARIANT *result )
{
    IBuffer *buffer;
    HRESULT status = S_OK;
    HSTRING filePath;
    HANDLE fileHandle;
    BYTE *outputBuffer;
    DWORD fileSize;
    DWORD bytesRead;
    BOOL readResult;

    struct storage_item *fileItem;

    //Parameters
    struct storage_file *file = impl_from_IStorageFile( (IStorageFile *)param );

    fileItem = impl_from_IStorageItem( &file->IStorageItem_iface );
    WindowsDuplicateString( fileItem->Path, &filePath );

    fileHandle = CreateFileW( WindowsGetStringRawBuffer( filePath, NULL ), GENERIC_READ, 0 , NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL );

    fileSize = GetFileSize( fileHandle, NULL );

    if ( fileSize == INVALID_FILE_SIZE )
    {
        CloseHandle( fileHandle );
        return E_INVALIDARG;
    }

    status = buffer_Create( fileSize, &buffer );

    if ( FAILED( status ) )
        return status;
    
    outputBuffer = impl_from_IBuffer( buffer )->Buffer;
    
    readResult = ReadFile( fileHandle, (LPVOID)outputBuffer, fileSize, &bytesRead, NULL );

    if ( !readResult || bytesRead != fileSize )
    {
        CloseHandle( fileHandle );
        status = E_UNEXPECTED;
    }

    if ( SUCCEEDED ( status ) )
    {
        result->vt = VT_UNKNOWN;
        result->ppunkVal = (IUnknown **)buffer;
    }

    CloseHandle( fileHandle );

    return status;
}

HRESULT WINAPI file_io_statics_WriteBuffer( IUnknown *invoker, IUnknown *param, PROPVARIANT *result )
{
    HRESULT status = S_OK;
    HSTRING filePath;
    BYTE *contents;
    HANDLE fileHandle;
    DWORD bytesWritten;

    struct storage_item *fileItem;

    struct file_io_write_buffer_options *write_buffer_options = (struct file_io_write_buffer_options *)param;

    //Parameters
    struct storage_file *file = impl_from_IStorageFile( write_buffer_options->file );    
    contents = impl_from_IBuffer( write_buffer_options->buffer )->Buffer;

    fileItem = impl_from_IStorageItem( &file->IStorageItem_iface );
    WindowsDuplicateString( fileItem->Path, &filePath );

    fileHandle = CreateFileW( WindowsGetStringRawBuffer( filePath, NULL ), GENERIC_WRITE, 0 , NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );    
    //Clear the file
    if ( SetFilePointer( fileHandle, 0, NULL, FILE_BEGIN ) == INVALID_SET_FILE_POINTER )
    {
        CloseHandle( fileHandle );
        status = E_UNEXPECTED;
    }

    if ( !SetEndOfFile( fileHandle ) ) 
    {
        CloseHandle( fileHandle );
        status = E_UNEXPECTED;
    }

    if ( FAILED( status ) )
        return status;

    if ( !WriteFile( fileHandle, (LPCVOID)contents, impl_from_IBuffer( write_buffer_options->buffer )->Length, &bytesWritten, NULL ) )
    {
        CloseHandle( fileHandle );
        status = E_UNEXPECTED;
    }
    
    CloseHandle( fileHandle );
    return status;
}

HRESULT WINAPI file_io_statics_WriteBytes( IUnknown *invoker, IUnknown *param, PROPVARIANT *result )
{
    HRESULT status = S_OK;
    HSTRING filePath;
    BYTE *contents;
    HANDLE fileHandle;
    DWORD bytesWritten;

    struct storage_item *fileItem;

    struct file_io_write_bytes_options *write_bytes_options = (struct file_io_write_bytes_options *)param;

    //Parameters
    struct storage_file *file = impl_from_IStorageFile( write_bytes_options->file );    
    contents = write_bytes_options->buffer;

    fileItem = impl_from_IStorageItem( &file->IStorageItem_iface );
    WindowsDuplicateString( fileItem->Path, &filePath );

    fileHandle = CreateFileW( WindowsGetStringRawBuffer( filePath, NULL ), GENERIC_WRITE, 0 , NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );    
    //Clear the file
    if ( SetFilePointer( fileHandle, 0, NULL, FILE_BEGIN ) == INVALID_SET_FILE_POINTER )
    {
        CloseHandle( fileHandle );
        status = E_UNEXPECTED;
    }

    if ( !SetEndOfFile( fileHandle ) ) 
    {
        CloseHandle( fileHandle );
        status = E_UNEXPECTED;
    }

    if ( FAILED( status ) )
        return status;

    if ( !WriteFile( fileHandle, (LPCVOID)contents, write_bytes_options->bufferSize, &bytesWritten, NULL ) )
    {
        CloseHandle( fileHandle );
        status = E_UNEXPECTED;
    }
    
    CloseHandle( fileHandle );
    return status;
}