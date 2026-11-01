/*
 * Minimal freestanding Win32 / NT declarations for the in-exe Xbox kernel
 * with clang --target=i686-w64-windows-gnu and no SDK headers.
 */
#pragma once

typedef unsigned char      u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef unsigned long long u64;
typedef signed char        s8;
typedef short              s16;
typedef int                s32;
typedef long long          s64;
typedef u32                size_t;
typedef s32                NTSTATUS;
typedef void*              HANDLE;
typedef int                BOOL;

#define WINAPI   __attribute__((stdcall))
#define FASTCALL __attribute__((fastcall))
#define NULL     ((void*)0)
#define TRUE     1
#define FALSE    0

#define STATUS_SUCCESS                0x00000000
#define STATUS_PENDING                0x00000103
#define STATUS_TIMEOUT                0x00000102
#define STATUS_UNSUCCESSFUL           0xC0000001
#define STATUS_NOT_IMPLEMENTED        0xC0000002
#define STATUS_INVALID_HANDLE         0xC0000008
#define STATUS_INVALID_PARAMETER      0xC000000D
#define STATUS_NO_MEMORY              0xC0000017
#define STATUS_OBJECT_NAME_NOT_FOUND  0xC0000034
#define STATUS_INVALID_DEVICE_REQUEST 0xC0000010
#define STATUS_BUFFER_TOO_SMALL       0xC0000023
#define NT_SUCCESS(s) ((NTSTATUS)(s) >= 0)

#define MEM_COMMIT        0x1000
#define MEM_RESERVE       0x2000
#define MEM_RELEASE       0x8000
#define PAGE_NOACCESS     0x01
#define PAGE_READONLY     0x02
#define PAGE_READWRITE    0x04
#define PAGE_EXECUTE_READWRITE 0x40
#define PAGE_NOCACHE      0x200
#define PAGE_WRITECOMBINE 0x400

#define INFINITE 0xFFFFFFFF
#define WAIT_TIMEOUT 0x102
#define DUPLICATE_SAME_ACCESS 2
#define STD_OUTPUT_HANDLE ((u32)-11)
#define GENERIC_READ  0x80000000
#define GENERIC_WRITE 0x40000000
#define OPEN_EXISTING 3
#define CREATE_ALWAYS 2
#define FILE_SHARE_READ 1
#define INVALID_HANDLE_VALUE ((HANDLE)-1)
#define CREATE_SUSPENDED 4

typedef struct { u16 Length, MaximumLength; char* Buffer; } ANSI_STRING;
typedef struct { u16 Length, MaximumLength; u16* Buffer; } UNICODE_STRING;
typedef struct {
    u32 Length; HANDLE RootDirectory; UNICODE_STRING* ObjectName;
    u32 Attributes; void* SecurityDescriptor; void* SecurityQualityOfService;
} NT_OBJECT_ATTRIBUTES;
typedef struct { union { NTSTATUS Status; void* Pointer; }; u32 Information; } IO_STATUS_BLOCK;
typedef struct {
    void* BaseAddress; void* AllocationBase; u32 AllocationProtect;
    u32 RegionSize; u32 State; u32 Protect; u32 Type;
} MEMORY_BASIC_INFORMATION;
typedef struct { void* DebugInfo; s32 LockCount, RecursionCount; HANDLE Owner, Sem; u32 Spin; } CRITICAL_SECTION;
typedef struct { void* Ptr; } CONDITION_VARIABLE;
typedef struct { u32 lo, hi; } FILETIME;

#define IMPORT __declspec(dllimport) WINAPI

/* kernel32 */
IMPORT void*  VirtualAlloc(void*, u32, u32, u32);
IMPORT BOOL   VirtualFree(void*, u32, u32);
IMPORT u32    VirtualQuery(const void*, MEMORY_BASIC_INFORMATION*, u32);
IMPORT BOOL   VirtualProtect(void*, u32, u32, u32*);
IMPORT HANDLE GetProcessHeap(void);
IMPORT void*  HeapAlloc(HANDLE, u32, u32);
IMPORT BOOL   HeapFree(HANDLE, u32, void*);
IMPORT u32    HeapSize(HANDLE, u32, const void*);
IMPORT HANDLE CreateThread(void*, u32, u32 (WINAPI*)(void*), void*, u32, u32*);
IMPORT void   ExitThread(u32);
IMPORT void   ExitProcess(u32);
IMPORT void   Sleep(u32);
IMPORT BOOL   SwitchToThread(void);
IMPORT u32    SuspendThread(HANDLE);
IMPORT u32    ResumeThread(HANDLE);
IMPORT HANDLE CreateEventA(void*, BOOL, BOOL, const char*);
IMPORT HANDLE CreateSemaphoreA(void*, s32, s32, const char*);
IMPORT HANDLE CreateMutexA(void*, BOOL, const char*);
IMPORT BOOL   CloseHandle(HANDLE);
IMPORT BOOL   DuplicateHandle(HANDLE, HANDLE, HANDLE, HANDLE*, u32, BOOL, u32);
IMPORT HANDLE GetCurrentProcess(void);
IMPORT HANDLE GetCurrentThread(void);
IMPORT u32    GetCurrentThreadId(void);
IMPORT HANDLE GetStdHandle(u32);
IMPORT BOOL   WriteFile(HANDLE, const void*, u32, u32*, void*);
IMPORT BOOL   ReadFile(HANDLE, void*, u32, u32*, void*);
IMPORT HANDLE CreateFileA(const char*, u32, u32, void*, u32, u32, HANDLE);
IMPORT BOOL   CreateDirectoryA(const char*, void*);
IMPORT BOOL   QueryPerformanceCounter(s64*);
IMPORT BOOL   QueryPerformanceFrequency(s64*);
IMPORT void   GetSystemTimeAsFileTime(FILETIME*);
IMPORT u32    GetModuleFileNameA(HANDLE, char*, u32);
IMPORT void   InitializeCriticalSection(CRITICAL_SECTION*);
IMPORT void   EnterCriticalSection(CRITICAL_SECTION*);
IMPORT void   LeaveCriticalSection(CRITICAL_SECTION*);
IMPORT void   InitializeConditionVariable(CONDITION_VARIABLE*);
IMPORT BOOL   SleepConditionVariableCS(CONDITION_VARIABLE*, CRITICAL_SECTION*, u32);
IMPORT void   WakeAllConditionVariable(CONDITION_VARIABLE*);
IMPORT u32    GetEnvironmentVariableA(const char*, char*, u32);
IMPORT s32    InterlockedIncrement(volatile s32*);
IMPORT s32    InterlockedDecrement(volatile s32*);
IMPORT s32    InterlockedCompareExchange(volatile s32*, s32, s32);
IMPORT void*  AddVectoredExceptionHandler(u32, s32 (WINAPI*)(void*));
IMPORT BOOL   GetThreadContext(HANDLE, void*);

/* ntdll */
IMPORT NTSTATUS NtCreateFile(HANDLE*, u32, NT_OBJECT_ATTRIBUTES*, IO_STATUS_BLOCK*, s64*, u32, u32, u32, u32, void*, u32);
IMPORT NTSTATUS NtOpenFile(HANDLE*, u32, NT_OBJECT_ATTRIBUTES*, IO_STATUS_BLOCK*, u32, u32);
IMPORT NTSTATUS NtReadFile(HANDLE, HANDLE, void*, void*, IO_STATUS_BLOCK*, void*, u32, s64*, u32*);
IMPORT NTSTATUS NtWriteFile(HANDLE, HANDLE, void*, void*, IO_STATUS_BLOCK*, const void*, u32, s64*, u32*);
IMPORT NTSTATUS NtClose(HANDLE);
IMPORT NTSTATUS NtQueryInformationFile(HANDLE, IO_STATUS_BLOCK*, void*, u32, u32);
IMPORT NTSTATUS NtSetInformationFile(HANDLE, IO_STATUS_BLOCK*, void*, u32, u32);
IMPORT NTSTATUS NtQueryVolumeInformationFile(HANDLE, IO_STATUS_BLOCK*, void*, u32, u32);
IMPORT NTSTATUS NtQueryFullAttributesFile(NT_OBJECT_ATTRIBUTES*, void*);
IMPORT NTSTATUS NtFlushBuffersFile(HANDLE, IO_STATUS_BLOCK*);
IMPORT NTSTATUS NtDeviceIoControlFile(HANDLE, HANDLE, void*, void*, IO_STATUS_BLOCK*, u32, void*, u32, void*, u32);
IMPORT NTSTATUS NtAllocateVirtualMemory(HANDLE, void**, u32, u32*, u32, u32);
IMPORT NTSTATUS NtFreeVirtualMemory(HANDLE, void**, u32*, u32);
IMPORT NTSTATUS NtCreateTimer(HANDLE*, u32, void*, u32);
IMPORT NTSTATUS NtSetTimer(HANDLE, s64*, void*, void*, BOOL, s32, BOOL*);
IMPORT NTSTATUS NtSetEvent(HANDLE, s32*);
IMPORT NTSTATUS NtReleaseMutant(HANDLE, s32*);
IMPORT NTSTATUS NtReleaseSemaphore(HANDLE, s32, s32*);
IMPORT NTSTATUS NtWaitForSingleObject(HANDLE, BOOL, s64*);
IMPORT NTSTATUS NtWaitForMultipleObjects(u32, HANDLE*, u32, BOOL, s64*);
IMPORT NTSTATUS NtDelayExecution(BOOL, s64*);
IMPORT u32      RtlNtStatusToDosError(NTSTATUS);
IMPORT void     RtlRaiseException(void*);
IMPORT void     RtlUnwind(void*, void*, void*, void*);
IMPORT BOOL     RtlTimeToTimeFields(s64*, void*);
IMPORT BOOL     RtlTimeFieldsToTime(void*, s64*);
