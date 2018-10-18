#include <windows.h>
#include "DLLLoader.h"

//ϵͳ�ṹ������

typedef struct _PEB { // Size: 0x1D8  
	/*000*/ UCHAR InheritedAddressSpace;  
	/*001*/ UCHAR ReadImageFileExecOptions;  
	/*002*/ UCHAR BeingDebugged;  
	/*003*/ UCHAR SpareBool; // Allocation size  
	/*004*/ HANDLE Mutant;  
	/*008*/ DWORD ImageBaseAddress; // Instance  
	/*00C*/ DWORD DllList;  
	/*010*/ DWORD ProcessParameters;  
	/*014*/ ULONG SubSystemData;  
	/*018*/ HANDLE DefaultHeap;  
	/*01C*/ KSPIN_LOCK FastPebLock;  
	/*020*/ ULONG FastPebLockRoutine;  
	/*024*/ ULONG FastPebUnlockRoutine;  
	/*028*/ ULONG EnvironmentUpdateCount;  
	/*02C*/ ULONG KernelCallbackTable;  
	/*030*/ LARGE_INTEGER SystemReserved;  
	/*038*/ ULONG FreeList;  
	/*03C*/ ULONG TlsExpansionCounter;  
	/*040*/ ULONG TlsBitmap;  
	/*044*/ LARGE_INTEGER TlsBitmapBits;  
	/*04C*/ ULONG ReadOnlySharedMemoryBase;  
	/*050*/ ULONG ReadOnlySharedMemoryHeap;  
	/*054*/ ULONG ReadOnlyStaticServerData;  
	/*058*/ ULONG AnsiCodePageData;  
	/*05C*/ ULONG OemCodePageData;  
	/*060*/ ULONG UnicodeCaseTableData;  
	/*064*/ ULONG NumberOfProcessors;  
	/*068*/ LARGE_INTEGER NtGlobalFlag; // Address of a local copy  
	/*070*/ LARGE_INTEGER CriticalSectionTimeout;  
	/*078*/ ULONG HeapSegmentReserve;  
	/*07C*/ ULONG HeapSegmentCommit;  
	/*080*/ ULONG HeapDeCommitTotalFreeThreshold;  
	/*084*/ ULONG HeapDeCommitFreeBlockThreshold;  
	/*088*/ ULONG NumberOfHeaps;  
	/*08C*/ ULONG MaximumNumberOfHeaps;  
	/*090*/ ULONG ProcessHeaps;  
	/*094*/ ULONG GdiSharedHandleTable;  
	/*098*/ ULONG ProcessStarterHelper;  
	/*09C*/ ULONG GdiDCAttributeList;  
	/*0A0*/ KSPIN_LOCK LoaderLock;  
	/*0A4*/ ULONG OSMajorVersion;  
	/*0A8*/ ULONG OSMinorVersion;  
	/*0AC*/ USHORT OSBuildNumber;  
	/*0AE*/ USHORT OSCSDVersion;  
	/*0B0*/ ULONG OSPlatformId;  
	/*0B4*/ ULONG ImageSubsystem;  
	/*0B8*/ ULONG ImageSubsystemMajorVersion;  
	/*0BC*/ ULONG ImageSubsystemMinorVersion;  
	/*0C0*/ ULONG ImageProcessAffinityMask;  
	/*0C4*/ ULONG GdiHandleBuffer[0x22];  
	/*14C*/ ULONG PostProcessInitRoutine;  
	/*150*/ ULONG TlsExpansionBitmap;  
	/*154*/ UCHAR TlsExpansionBitmapBits[0x80];  
	/*1D4*/ ULONG SessionId;  
} PEB, *PPEB;

typedef struct _PEB_LDR_DATA  
{  
	ULONG Length; // +0x00  
	BOOLEAN Initialized; // +0x04  
	PVOID SsHandle; // +0x08  
	LIST_ENTRY InLoadOrderModuleList; // +0x0c  
	LIST_ENTRY InMemoryOrderModuleList; // +0x14  
	LIST_ENTRY InInitializationOrderModuleList;// +0x1c  
} PEB_LDR_DATA,*PPEB_LDR_DATA; // +0x24

typedef struct _UNICODE_STRING {
	USHORT Length;
	USHORT MaximumLength;
	PWSTR Buffer;
} UNICODE_STRING,*PUNICODE_STRING;

/*+0x000 InLoadOrderLinks : _LIST_ENTRY
+0x008 InMemoryOrderLinks : _LIST_ENTRY
+0x010 InInitializationOrderLinks : _LIST_ENTRY
+0x018 DllBase          : Ptr32 Void
+0x01c EntryPoint       : Ptr32 Void
+0x020 SizeOfImage      : Uint4B
+0x024 FullDllName      : _UNICODE_STRING
+0x02c BaseDllName      : _UNICODE_STRING
+0x034 Flags            : Uint4B
+0x038 LoadCount        : Uint2B
+0x03a TlsIndex         : Uint2B
+0x03c HashLinks        : _LIST_ENTRY
+0x03c SectionPointer   : Ptr32 Void
+0x040 CheckSum         : Uint4B
+0x044 TimeDateStamp    : Uint4B
+0x044 LoadedImports    : Ptr32 Void
+0x048 EntryPointActivationContext : Ptr32 Void
+0x04c PatchInformation : Ptr32 Void*/
typedef struct _LDR_DATA_TABLE_ENTRY
{
	LIST_ENTRY InLoadOrderLinks;
	LIST_ENTRY InMemoryOrderLinks;
	LIST_ENTRY InInitializationOrderLinks;
	DWORD  DllBase;
	DWORD EntryPoint;
	DWORD SizeOfImage;
	UNICODE_STRING FullDllName;
	UNICODE_STRING BaseDllName;
	DWORD Flags;
	WORD LoadCount;
	WORD TlsIndex;

	union{
		LIST_ENTRY HashLinks;
		DWORD SectionPointer;
	};

	DWORD CheckSum;

	union{
		DWORD TimeDateStamp;
		DWORD LoadedImports;
	};

	DWORD EntryPointActivationContext;
	DWORD PatchInformation; 
	
} LDR_DATA_TABLE_ENTRY,*PLDR_DATA_TABLE_ENTRY;

typedef BOOL (WINAPI *FuncDLLMain)(HINSTANCE,DWORD,LPVOID);//DLLMain��������

char* FileBuf;//DLL�ļ�������
DWORD FileBufSize;//DLL�ļ���������С
char* MemBuf;//DLL�ڴ滺����
DWORD MemBufSize;//DLL�ڴ滺������С

//PE�ļ��ṹ��ָ���������
IMAGE_DOS_HEADER *File_DOS_Header,*Mem_DOS_Header;//DOSͷ
IMAGE_NT_HEADERS *File_NT_Headers,*Mem_NT_Headers;//NTͷ
IMAGE_SECTION_HEADER *File_Section_Header,*Mem_Section_Header;//��ͷ
IMAGE_IMPORT_DESCRIPTOR *Mem_Import_Descriptor;//����������
IMAGE_BASE_RELOCATION *Mem_Base_Relocation;//�ض����

DWORD Mem_Import_Descriptorn;//�ض��������

FuncDLLMain pDLLMain = NULL;//DLLMain����ָ��

LDR_DATA_TABLE_ENTRY *Mem_LDR_Data_Table_Entry;//PEB��LDR��ָ�Ľṹ��ָ�����

LPWSTR Mem_DLLBaseName = NULL;//DLL��������Unicode��
LPWSTR Mem_DLLFullName = NULL;//DLLȫ����Unicode��

void LoadPEHeader()//����PEͷ
{
	File_DOS_Header = (PIMAGE_DOS_HEADER)FileBuf;//��ȡDOSͷ��ַ
	File_NT_Headers = (PIMAGE_NT_HEADERS)((DWORD)FileBuf + File_DOS_Header -> e_lfanew);//��ȡNTͷ��ַ
	MemBufSize = File_NT_Headers -> OptionalHeader.SizeOfImage;//��ȡDLL�ڴ�ӳ���С
	MemBuf = (char *)VirtualAlloc(NULL,MemBufSize,MEM_COMMIT,PAGE_EXECUTE_READWRITE);//����DLL�ڴ�
	Mem_DOS_Header = (PIMAGE_DOS_HEADER)MemBuf;//��ȡDLL�ڴ���DOSͷ��ַ
	CopyMemory(Mem_DOS_Header,File_DOS_Header,File_NT_Headers -> OptionalHeader.SizeOfHeaders);//��PEͷ���ؽ��ڴ�
	Mem_NT_Headers = (PIMAGE_NT_HEADERS)((DWORD)MemBuf + Mem_DOS_Header -> e_lfanew);//��ȡDLL�ڴ���NTͷ��ַ
	File_Section_Header =  (PIMAGE_SECTION_HEADER)((DWORD)File_NT_Headers + sizeof(IMAGE_NT_HEADERS) - sizeof(IMAGE_OPTIONAL_HEADER32) + File_NT_Headers -> FileHeader.SizeOfOptionalHeader);//��ȡ��ͷ��ַ
	Mem_Section_Header = (PIMAGE_SECTION_HEADER)((DWORD)Mem_NT_Headers + sizeof(IMAGE_NT_HEADERS) - sizeof(IMAGE_OPTIONAL_HEADER32) + Mem_NT_Headers -> FileHeader.SizeOfOptionalHeader);//��ȡDLL�ڴ��н�ͷ��ַ
}

void LoadSectionData()//���ؽ�����
{
	int i;

	for(i = 0;i < Mem_NT_Headers -> FileHeader.NumberOfSections;i++)//���ļ��г��Ȳ�Ϊ0�Ľ��е����ݿ�����DLL�ڴ���
	{
		if(Mem_Section_Header[i].SizeOfRawData > 0)
		{
			CopyMemory((LPVOID)((DWORD)MemBuf + Mem_Section_Header[i].VirtualAddress), (LPVOID)((DWORD)FileBuf + ((File_Section_Header[i].PointerToRawData % File_NT_Headers -> OptionalHeader.FileAlignment == 0) ? File_Section_Header[i].PointerToRawData : 0)), File_Section_Header[i].SizeOfRawData);
		}
	}
}

void RepairIAT()//�޸������
{
	int i;
	PIMAGE_THUNK_DATA32 INT;//INT��ַ
	LPDWORD IAT;//IAT��ַ
	HMODULE hMod;//DLL���
	LPCSTR LibraryName;//������
	PIMAGE_IMPORT_BY_NAME IIN;//�������ƽṹ��
	LPVOID FuncAddress;//������ַ

	Mem_Import_Descriptor = (PIMAGE_IMPORT_DESCRIPTOR)((DWORD)MemBuf + Mem_NT_Headers -> OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);//��ȡDLL�ڴ��е�����������ַ
	Mem_Import_Descriptorn = Mem_NT_Headers -> OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].Size / sizeof(IMAGE_IMPORT_DESCRIPTOR);//��ȡ��������������

	for(i = 0;i < Mem_Import_Descriptorn;i++)//��������������
	{
		INT = (PIMAGE_THUNK_DATA32)((DWORD)MemBuf + Mem_Import_Descriptor[i].OriginalFirstThunk);//��ȡDLL�ڴ���INT��ַ
		IAT = (LPDWORD)((DWORD)MemBuf + Mem_Import_Descriptor[i].FirstThunk);//��ȡDLL�ڴ���IAT��ַ

		if(Mem_Import_Descriptor[i].OriginalFirstThunk == NULL)//��INT��ַΪNULL������ΪINT�ĵ�ַ��IAT�ĵ�ַ���
		{
			INT = (PIMAGE_THUNK_DATA32)IAT;
		}

		if(Mem_Import_Descriptor[i].FirstThunk != NULL)//��IAT�ĵ�ַ��ΪNULL������Ч������
		{
			LibraryName = (LPCSTR)((DWORD)MemBuf + Mem_Import_Descriptor[i].Name);//��ȡ���ļ���
			hMod = GetModuleHandle(LibraryName);//��ȡ����

			if(hMod == NULL)//����δ�����أ�����ؿ�
			{
				hMod = LoadLibrary(LibraryName);
			}

			while(INT -> u1.AddressOfData != NULL)//����INT��ֱ������NULL��
			{
				if((INT -> u1.AddressOfData & 0x80000000) == NULL)//��Ҫʹ�����ƻ�ȡ������ַ
				{
					IIN = (PIMAGE_IMPORT_BY_NAME)((DWORD)MemBuf + INT -> u1.AddressOfData);//��ȡ�������ƽṹ��
					FuncAddress = GetProcAddress(hMod, (LPCSTR)IIN->Name);
				}
				else//��Ҫʹ����Ż�ȡ������ַ
				{
					FuncAddress = GetProcAddress(hMod,(LPCSTR)(INT -> u1.Ordinal & 0x000000FF));
				}

				*IAT = (DWORD)FuncAddress;//��������ĺ�����ַд��IAT

				//��INT��IATָ����һ��
				INT = (PIMAGE_THUNK_DATA32)((DWORD)INT + sizeof(IMAGE_THUNK_DATA32));
				IAT = (LPDWORD)((DWORD)IAT + sizeof(DWORD));
			}
		}
	}
}

void RepairOperateAddress()//�޸��ض����ַ
{
	int i;
	int RelocDatan;//�ض��������
	WORD Offset;//�ض���ƫ��
	BYTE Type;//�ض�������
	DWORD AddValue;//��ǰImageBase��ԭImageBase��ֵ
	DWORD BaseAddress;//�ض����Ļ�ַ
	LPDWORD pDest;//ָ����Ҫ�ض����ַ�ĵط�
	LPWORD pRelocData;//��ǰ�ض�����ض�������ַ
	
	Mem_Base_Relocation = (PIMAGE_BASE_RELOCATION)((DWORD)MemBuf + Mem_NT_Headers -> OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress);
	
	while((DWORD)Mem_Base_Relocation < ((DWORD)MemBuf + Mem_NT_Headers -> OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress + Mem_NT_Headers -> OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].Size))
	{
		pRelocData = (LPWORD)((DWORD)Mem_Base_Relocation + sizeof(IMAGE_BASE_RELOCATION));//��ȡ��ǰ�ض�����ض�������ַ
		RelocDatan = (Mem_Base_Relocation->SizeOfBlock - sizeof(IMAGE_BASE_RELOCATION)) / sizeof(WORD);//��ȡ�ض��������
		AddValue = (DWORD)MemBuf - Mem_NT_Headers -> OptionalHeader.ImageBase;//��ȡ��ǰImageBase��ԭImageBase��ֵ
		BaseAddress = (DWORD)MemBuf + Mem_Base_Relocation -> VirtualAddress;//��ȡ�ض����Ļ�ַ
		
		for (i = 0; i < RelocDatan; i++)//�����ض������
		{
			Offset = pRelocData[i] & 0x0FFF;//��ȡ�ض���ƫ��
			Type = (BYTE)(pRelocData[i] >> 12);//��ȡ�ض�������
			pDest = (DWORD *)(BaseAddress + Offset);//��ȡ��Ҫ�ض����ַ�ĵط�

			//��ַ�ض���
			switch (Type)
			{
				case IMAGE_REL_BASED_ABSOLUTE:
					break;

				case IMAGE_REL_BASED_HIGH:		
					*pDest = (((AddValue & 0xFFFF0000) + ((*pDest) & 0xFFFF0000)) & 0xFFFF0000) | ((*pDest) & 0x0000FFFF);
					break;

				case IMAGE_REL_BASED_LOW:
					*pDest += (((AddValue & 0x0000FFFF) + ((*pDest) & 0x0000FFFF)) & 0x0000FFFF) | ((*pDest) & 0xFFFF0000);
					break;

				case IMAGE_REL_BASED_HIGHLOW:
					*pDest += AddValue;
					break;

				case IMAGE_REL_BASED_HIGHADJ:
					*pDest = (((AddValue & 0xFFFF0000) + ((*pDest) & 0xFFFF0000)) & 0xFFFF0000) | ((*pDest) & 0x0000FFFF);
					break;

				default:
					break;
			}
		}

		Mem_Base_Relocation = (PIMAGE_BASE_RELOCATION)((DWORD)Mem_Base_Relocation + Mem_Base_Relocation -> SizeOfBlock);//ָ����һ���ض����
	}
}

void AddDLLToPEB()//��DLL��Ϣ����PEB��LDR��
{
	PPEB PEB;//PEB��ַ
	PPEB_LDR_DATA LDR;//LDR��ַ
	PLDR_DATA_TABLE_ENTRY EndModule;//����ģ���ַ
	LPDWORD PEBAddress = (LPDWORD)((DWORD)NtCurrentTeb() + 0x00000030);//����PEB��ַ
	
	PEB = (PPEB)(*PEBAddress);//��ȡPEB��ַ
	LDR = (PPEB_LDR_DATA)PEB -> DllList;//��ȡLDR��ַ

	//����LDR.InLoadOrderModuleList�Ի�ý���ģ���ַ
	EndModule = (PLDR_DATA_TABLE_ENTRY)LDR -> InLoadOrderModuleList.Flink;

	while(EndModule -> DllBase != NULL)
	{
		EndModule = (PLDR_DATA_TABLE_ENTRY) EndModule -> InLoadOrderLinks.Flink;
	}

	Mem_LDR_Data_Table_Entry = (PLDR_DATA_TABLE_ENTRY)VirtualAlloc(NULL,sizeof(LDR_DATA_TABLE_ENTRY),MEM_COMMIT,PAGE_READWRITE);//����LDR���ݱ��ڴ�

	//��DLL����InLoadOrderModuleList
	EndModule -> InLoadOrderLinks.Blink -> Flink = &Mem_LDR_Data_Table_Entry -> InLoadOrderLinks;
	Mem_LDR_Data_Table_Entry -> InLoadOrderLinks.Flink = &EndModule -> InLoadOrderLinks;
	Mem_LDR_Data_Table_Entry -> InLoadOrderLinks.Blink = EndModule -> InLoadOrderLinks.Blink;
	EndModule -> InLoadOrderLinks.Blink = &Mem_LDR_Data_Table_Entry -> InLoadOrderLinks;
	LDR -> InLoadOrderModuleList.Blink = &Mem_LDR_Data_Table_Entry -> InLoadOrderLinks;

	//��DLL����InMemoryOrderModuleList
	EndModule -> InMemoryOrderLinks.Blink -> Flink = &Mem_LDR_Data_Table_Entry -> InMemoryOrderLinks;
	Mem_LDR_Data_Table_Entry -> InMemoryOrderLinks.Flink = &EndModule -> InMemoryOrderLinks;
	Mem_LDR_Data_Table_Entry -> InMemoryOrderLinks.Blink = EndModule -> InMemoryOrderLinks.Blink;
	EndModule -> InMemoryOrderLinks.Blink = &Mem_LDR_Data_Table_Entry -> InMemoryOrderLinks;
	LDR -> InMemoryOrderModuleList.Blink = &Mem_LDR_Data_Table_Entry -> InMemoryOrderLinks;

	//��DLL����InInitializationOrderModuleList
	EndModule -> InInitializationOrderLinks.Blink -> Flink = &Mem_LDR_Data_Table_Entry -> InInitializationOrderLinks;
	Mem_LDR_Data_Table_Entry -> InInitializationOrderLinks.Flink = &EndModule -> InInitializationOrderLinks;
	Mem_LDR_Data_Table_Entry -> InInitializationOrderLinks.Blink = EndModule -> InInitializationOrderLinks.Blink;
	EndModule -> InInitializationOrderLinks.Blink = &Mem_LDR_Data_Table_Entry -> InInitializationOrderLinks;
	LDR -> InInitializationOrderModuleList.Blink = &Mem_LDR_Data_Table_Entry -> InInitializationOrderLinks;

	Mem_LDR_Data_Table_Entry -> DllBase = (DWORD)MemBuf;//д��DLL�ڴ��ַ
	Mem_LDR_Data_Table_Entry -> EntryPoint = (DWORD)(Mem_NT_Headers -> OptionalHeader.AddressOfEntryPoint + (DWORD)MemBuf);//д��DLL��ڵ��ַ
	Mem_LDR_Data_Table_Entry -> SizeOfImage = MemBufSize;//д��DLLģ���С

	//д��DLL������
	Mem_LDR_Data_Table_Entry -> BaseDllName.Buffer = (PWSTR)VirtualAlloc(NULL,wcslen(Mem_DLLBaseName) * sizeof(WCHAR) + 2,MEM_COMMIT,PAGE_READWRITE);
	Mem_LDR_Data_Table_Entry -> BaseDllName.Length = wcslen(Mem_DLLBaseName) * sizeof(WCHAR);
	Mem_LDR_Data_Table_Entry -> BaseDllName.MaximumLength = Mem_LDR_Data_Table_Entry -> BaseDllName.Length;
	CopyMemory((LPVOID)Mem_LDR_Data_Table_Entry -> BaseDllName.Buffer,(LPVOID)Mem_DLLBaseName,Mem_LDR_Data_Table_Entry -> BaseDllName.Length + 2);

	//д��DLLȫ��
	Mem_LDR_Data_Table_Entry -> FullDllName.Buffer = (PWSTR)VirtualAlloc(NULL,wcslen(Mem_DLLFullName) * sizeof(WCHAR) + 2,MEM_COMMIT,PAGE_READWRITE);
	Mem_LDR_Data_Table_Entry -> FullDllName.Length = wcslen(Mem_DLLFullName) * sizeof(WCHAR);
	Mem_LDR_Data_Table_Entry -> FullDllName.MaximumLength = Mem_LDR_Data_Table_Entry -> FullDllName.Length;
	CopyMemory((LPVOID)Mem_LDR_Data_Table_Entry -> FullDllName.Buffer,(LPVOID)Mem_DLLFullName,Mem_LDR_Data_Table_Entry -> FullDllName.Length + 2);
	
	Mem_LDR_Data_Table_Entry -> LoadCount = 1;//��DLL���ش�����1
}

void DLLInit()//DLL��ʼ��
{
	pDLLMain = (FuncDLLMain)(Mem_NT_Headers -> OptionalHeader.AddressOfEntryPoint + (DWORD)MemBuf);//DLL��ڵ㼴��ȡDLLMain������ַ
	pDLLMain((HINSTANCE)MemBuf,DLL_PROCESS_ATTACH,NULL);//ִ��DLLMain
}

char* DLLMemLoad(char* DLLFileBuf,DWORD DLLFileSize,char* MemDLLBaseName,char* MemDLLFullName)//DLL�ڴ���غ�����ע��Ϊ�˱�֤һЩDLLģ����������У��뱣֤MemDLLBaseName������MemDLLFullName�����Ľ�β�ǡ�.dll���������ִ�Сд��
{
	//��ʼ����ر���
	FileBuf = DLLFileBuf;
	FileBufSize = DLLFileSize;
	Mem_DLLBaseName = (LPWSTR)VirtualAlloc(NULL,strlen(MemDLLBaseName) * sizeof(WCHAR) + 2,MEM_COMMIT,PAGE_READWRITE);
	Mem_DLLFullName = (LPWSTR)VirtualAlloc(NULL,strlen(MemDLLFullName) * sizeof(WCHAR) + 2,MEM_COMMIT,PAGE_READWRITE);
	MultiByteToWideChar(CP_ACP,MB_PRECOMPOSED,MemDLLBaseName,-1,Mem_DLLBaseName,strlen(MemDLLBaseName) * sizeof(WCHAR) + 2);
	MultiByteToWideChar(CP_ACP,MB_PRECOMPOSED,MemDLLFullName,-1,Mem_DLLFullName,strlen(MemDLLFullName) * sizeof(WCHAR) + 2);

	//��ʼִ����ع���
	LoadPEHeader();
	LoadSectionData();
	RepairIAT();
	RepairOperateAddress();
	AddDLLToPEB();
	DLLInit();

	return MemBuf;//����DLL�ڴ��ַ��DLL���
}

void DLLMemFree(char* DLLMemBaseAddress)//DLL�ڴ��ͷź��������ڳ������֮ǰ�������ͷż��ص�DLL�����������ܻ��쳣�˳�
{
	PPEB PEB;//PEB��ַ
	PPEB_LDR_DATA LDR;//LDR��ַ
	PLDR_DATA_TABLE_ENTRY CurModule;//��ǰģ���ַ
	PLDR_DATA_TABLE_ENTRY EndModule;//����ģ���ַ
	LPDWORD PEBAddress = (LPDWORD)((DWORD)NtCurrentTeb() + 0x00000030);//����PEB��ַ

	MemBuf = DLLMemBaseAddress;//��ʼ��MemBufָ�����

	PEB = (PPEB)(*PEBAddress);//��ȡPEB��ַ
	LDR = (PPEB_LDR_DATA)PEB -> DllList;//��ȡLDR��ַ

	//����LDR.InLoadOrderModuleList�Ի��DLLģ���ַ
	CurModule = (PLDR_DATA_TABLE_ENTRY)LDR -> InLoadOrderModuleList.Flink;

	while(CurModule -> DllBase != NULL)
	{
		if(CurModule -> DllBase == (DWORD)DLLMemBaseAddress)
		{
			break;
		}

		CurModule = (PLDR_DATA_TABLE_ENTRY) CurModule -> InLoadOrderLinks.Flink;
	}

	if(CurModule -> DllBase == NULL)//��DLLģ��δ�ҵ�
	{
		return;
	}

	//����LDR.InLoadOrderModuleList�Ի�ý���ģ���ַ
	EndModule = (PLDR_DATA_TABLE_ENTRY)LDR -> InLoadOrderModuleList.Flink;

	while(EndModule -> DllBase != NULL)
	{
		EndModule = (PLDR_DATA_TABLE_ENTRY) EndModule -> InLoadOrderLinks.Flink;
	}

	//��DLL��InLoadOrderModuleList��ж��
	CurModule -> InLoadOrderLinks.Flink -> Blink = CurModule -> InLoadOrderLinks.Blink;
	CurModule -> InLoadOrderLinks.Blink -> Flink = CurModule -> InLoadOrderLinks.Flink;

	//��DLL��InMemoryOrderModuleList��ж��
	CurModule -> InMemoryOrderLinks.Flink -> Blink = CurModule -> InMemoryOrderLinks.Blink;
	CurModule -> InMemoryOrderLinks.Blink -> Flink = CurModule -> InMemoryOrderLinks.Flink;

	//��DLL��InInitializationOrderModuleList��ж��
	CurModule -> InInitializationOrderLinks.Flink -> Blink = CurModule -> InInitializationOrderLinks.Blink;
	CurModule -> InInitializationOrderLinks.Blink -> Flink = CurModule -> InInitializationOrderLinks.Flink;

	//�޸�LDR���������Blink
	LDR -> InLoadOrderModuleList.Blink = EndModule -> InLoadOrderLinks.Blink;
	LDR -> InMemoryOrderModuleList.Blink = EndModule -> InLoadOrderLinks.Blink;
	LDR -> InInitializationOrderModuleList.Blink = EndModule -> InInitializationOrderLinks.Blink;

	MemBufSize = Mem_LDR_Data_Table_Entry -> SizeOfImage;//��ʼ��MemBufSize����
	VirtualFree((LPVOID)MemBuf,MemBufSize,MEM_DECOMMIT);//�ͷ�DLL�ڴ�

	//�ͷ�DLLģ�������ṹ����ռ�ڴ�ռ�
	VirtualFree((LPVOID)CurModule -> BaseDllName.Buffer,CurModule -> BaseDllName.Length + 2,MEM_DECOMMIT);
	VirtualFree((LPVOID)CurModule -> FullDllName.Buffer,CurModule -> FullDllName.Length + 2,MEM_DECOMMIT);
	VirtualFree((LPVOID)CurModule,sizeof(LDR_DATA_TABLE_ENTRY),MEM_DECOMMIT);
}