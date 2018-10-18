#include <cstdio>
#include <windows.h>
#include <wincon.h>
#include <stdlib.h>
#include <vector>
#include "DLLLoader.h"
#include "AES.h"
#include "CRC32.h"
#include <Nb30.h>
#include "ntdll.h"
#pragma comment(lib,"netapi32.lib")  

using namespace std;


#define uchar unsigned char
#define uint unsigned int
#define uint64 unsigned long long

typedef void (WINAPI* FUNC_RUN)(uint64, uint64);
FUNC_RUN Run = NULL;

#define BLOCK_SIZE 65536
#define BLOCK_NUM 4



#ifdef _DEBUG
#define  IS_DEBUG  1
#endif // _DEBUG

// ���Լ��
#ifndef IS_DEBUG
#define DEBUGCHECK
#endif // !IS_DEBUG

//#define DEBUG

char Data1[BLOCK_SIZE] = "DLLLoaderLZRData1";
char UnknownData1[BLOCK_SIZE] = "DLLLoaderLZRUnknownData1";
char Data2[BLOCK_SIZE] = "DLLLoaderLZRData2";
char UnknownData2[BLOCK_SIZE] = "DLLLoaderLZRUnknownData2";
char Data3[BLOCK_SIZE] = "DLLLoaderLZRData3";
char UnknownData3[BLOCK_SIZE] = "DLLLoaderLZRUnknownData3";
char Data4[BLOCK_SIZE] = "DLLLoaderLZRData4";
char UnknownData4[BLOCK_SIZE] = "DLLLoaderLZRUnknownData4";
char DataInfo[BLOCK_SIZE] = "DLLoaderLZRDataInfo1";
char UnknownData5[BLOCK_SIZE] = "DLLLoaderLZRUnknownData5";
char DataInfo2[BLOCK_SIZE] = "DLLoaderLZRDataInfo2";
char CurPath[MAX_PATH];
//����4*64K=256K������һ�ɷŵ�EXEβ���������������ݾ�����AES�㷨���ܣ�������XOR���μ���

char InputVector[16] = { 0x05, 0x84, 0x63, 0x75, 0x74, 0x96, 0x75, 0x89, 0x77, 0x63, 0x59, 0x66, 0xA9, 0xF6, 0x7C, 0xFE };

#define DATAINFO_FILESIZE 1000 //4Bytes
#define DATAINFO_FILEORIGINSIZE 3564 //4Bytes
#define DATAINFO_FILEAESPASSWORD 5236 //16Bytes
#define DATAINFO_FILEXORPASSWORD 15832 //1000Bytes
#define DATAINFO_DLLVIRTUALNAME 23856 //NULL��ֹ ����Ϊ.dll��β
#define DATAINFO_VERIFYMACHINECODE 25675 //1Byte
#define DATAINFO_MACHINECODE 34125 //8Bytes
#define DATAINFO_XORDATAVERIFYCODE 42578 //����XORУ���� 1Byte
#define DATAINFO_CRC32DATAVERIFYCODE 52714 //����CRC32У���� 4Bytes

#define XORPASSWORDLEN 1000

DWORD ExitAddress = 0xFFFFFFFF;
DWORD MainESP = 0xFFFFFFFF;
DWORD MainEBP = 0xFFFFFFFF;

typedef struct DATA_INFO
{
	DWORD FileSize;
	DWORD FileOriginSize;
	char *FileAESPassword;
	char *FileXORPassword;
	char *DLLVirtualName;
	bool VerifyMachineCode;
	uint64 MachineCode;
	char XORDataVerifyCode;
	DWORD CRC32DataVerifyCode;
}DATA_INFO;

typedef DWORD(NTAPI *Csr)(void);

Csr CsrGetProcessId;

DATA_INFO DataInfoStruct;
vector<uint64> mac;
DWORD tEAX, tEDX;
DWORD pAllocatedMem;
DWORD dwOldProtect;

int ShellExit();

inline bool ValidNumRange(int n, int min, int max)
{
	return (n >= min) && (n <= max);
}

inline bool IsInsideVMWare()
{
	bool rc = true;

	__try
	{
		__asm
		{
			push   edx
				push   ecx
				push   ebx

				mov    eax, 'VMXh'
				mov    ebx, 0  // ��ebx����Ϊ�ǻ�����VMXH��������ֵ
				mov    ecx, 10 // ָ�����ܺţ����ڻ�ȡVMWare�汾������Ϊ0x14ʱ���ڻ�ȡVMware�ڴ��С
				mov    edx, 'VX' // �˿ں�
				in     eax, dx // �Ӷ˿�dx��ȡVMware�汾��eax
				//������ָ�����ܺ�Ϊ0x14ʱ����ͨ���ж�eax�е�ֵ�Ƿ����0��������˵�������������
				cmp    ebx, 'VMXh' // �ж�ebx���Ƿ����VMware�汾��VMXh�������������������
				setz[rc] // ���÷���ֵ

				pop    ebx
				pop    ecx
				pop    edx
		}
	}
	__except (EXCEPTION_EXECUTE_HANDLER)  //���δ����VMware�У��򴥷����쳣
	{
		rc = false;
	}

	return rc;
}

inline bool IsVirtualPC_LDTCheck()
{
	unsigned short ldt_addr = 0;
	unsigned char ldtr[2];

	_asm sldt ldtr
	ldt_addr = *((unsigned short *)&ldtr);
	return ldt_addr != 0x00000000;
}

inline bool IsVirtualPC_GDTCheck()
{
	unsigned int gdt_addr = 0;
	unsigned char gdtr[6];

	_asm sgdt gdtr
	gdt_addr = *((unsigned int *)&gdtr[2]);
	return (gdt_addr >> 24) == 0xff;
}

inline bool IsVirtualPC_TSSCheck()
{
	unsigned char mem[4] = { 0 };

	__asm str mem;
	return (mem[0] == 0x00) && (mem[1] == 0x40);
}

inline bool DetectVM()
{
	HKEY hKey;

	char szBuffer[64];

	unsigned long hSize = sizeof(szBuffer)-1;

	if (RegOpenKeyEx(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\BIOS\\", 0, KEY_READ, &hKey) == ERROR_SUCCESS)
	{

		RegQueryValueEx(hKey, "SystemManufacturer", NULL, NULL, (unsigned char *)szBuffer, &hSize);

		if (strstr(szBuffer, "VMWARE"))
		{
			RegCloseKey(hKey);
			return true;
		}

		RegCloseKey(hKey);
	}

	return false;
}

inline void BeginCheckTimingDebug()
{
#ifdef DEBUGCHECK
	__asm
	{
		PUSHAD
			CPUID
			RDTSC
			MOV tEAX,EAX
			MOV tEDX,EDX
			POPAD
	}

#endif
}

inline void EndCheckTimingDebug(DWORD TimeDelta)
{
#ifdef DEBUGCHECK
	__asm
	{
		PUSHAD
			CPUID
			MOV ECX,tEAX
			MOV EBX,tEDX
			RDTSC
			CMP EDX,EBX
			JA Debugger_Found
			SUB EAX,ECX
			CMP EAX,TimeDelta
			JA Debugger_Found
			JMP safe

		Debugger_Found:

		POPAD
			MOV ESP,MainESP
			JMP ExitAddress

		safe:
		POPAD
	}
#endif
}

inline void CheckDebug()
{

#ifdef DEBUGCHECK
	int debuged;
	DWORD DebugPort;
	DWORD ReturnLen;

	//��������
	(IsInsideVMWare() || IsVirtualPC_LDTCheck() || IsVirtualPC_GDTCheck() || IsVirtualPC_TSSCheck() || DetectVM()) ? ShellExit() : 0;

	__asm
	{
		PUSHAD

			;check PEB.BeingDebugged directly

			MOV EAX,DWORD PTR FS:[0x30]
			MOVZX EAX,BYTE PTR [EAX+2]
			TEST EAX,EAX
			JNZ Debugger_Found
			JMP safe

		Debugger_Found:

		POPAD
			MOV ESP,MainESP
			MOV EBP,MainEBP
			JMP ExitAddress

		safe:

		;(PEB.ProcessHeap)
			MOV EBX,DWORD PTR FS:[030H]

			;Check if PEB.NtGlobalFlag != 0
			CMP DWORD PTR [EBX+068H],0
			JNE Debugger_Found
			;query for the PID of CSRSS.EXE
			CALL [CsrGetProcessId]

			;try to open the CSRSS.EXE process
			PUSH EAX
			PUSH FALSE
			PUSH PROCESS_QUERY_INFORMATION
			CALL [OpenProcess]

			;if OpenProcess() was successful
			;process is probably being debugged
			TEST EAX,EAX
			JNZ Debugger_Found

		EXIT:
		POPAD
	}

	(CheckRemoteDebuggerPresent(GetCurrentProcess(),&debuged) == FALSE) ? ShellExit() : 0;	
	(debuged == TRUE) ? ShellExit() : 0;
	NtQueryInformationProcess(GetCurrentProcess(),ProcessDebugPort,&DebugPort,4,&ReturnLen);
	(DebugPort != 0) ? ShellExit() : 0;

#endif
}

inline int GetNetworkAdapterAddress()
{
	NCB ncb;
	uint t, p;
	int i, j;

	typedef struct _ASTAT_
	{
		ADAPTER_STATUS   adapt;
		NAME_BUFFER   NameBuff[30];
	}ASTAT, *PASTAT;

	ASTAT Adapter;

	typedef struct _LANA_ENUM
	{
		UCHAR   length;
		UCHAR   lana[MAX_LANA];
	}LANA_ENUM;

	mac.clear();
	LANA_ENUM lana_enum;
	UCHAR uRetCode;
	memset(&ncb, 0, sizeof(ncb));
	memset(&lana_enum, 0, sizeof(lana_enum));
	ncb.ncb_command = NCBENUM;
	ncb.ncb_buffer = (unsigned char *)&lana_enum;
	ncb.ncb_length = sizeof(LANA_ENUM);
	uRetCode = Netbios(&ncb);

	if (uRetCode != NRC_GOODRET)
		return uRetCode;

	for (int lana = 0; lana < lana_enum.length; lana++)
	{
		ncb.ncb_command = NCBRESET;
		ncb.ncb_lana_num = lana_enum.lana[lana];
		uRetCode = Netbios(&ncb);
		if (uRetCode == NRC_GOODRET)
			break;
	}

	if (uRetCode != NRC_GOODRET)
		return uRetCode;

	for (i = 0; i < lana_enum.length; i++)
	{
		memset(&ncb, 0, sizeof(ncb));
		ncb.ncb_command = NCBASTAT;
		ncb.ncb_lana_num = lana_enum.lana[i];
		strcpy((char*)ncb.ncb_callname, "*");
		ncb.ncb_buffer = (unsigned char *)&Adapter;
		ncb.ncb_length = sizeof(Adapter);
		uRetCode = Netbios(&ncb);

		if (uRetCode != NRC_GOODRET)
			return uRetCode;

		p = 0;

		for (j = 0; j < 6; j++)
		{
			t = Adapter.adapt.adapter_address[j];
			p |= t << ((5 - j) * 8);
		}

		mac.push_back(p);
	}

	return 0;
}

inline uint64 GetMachineCode()
{
	uint64 machinecode;
	int i;

	GetNetworkAdapterAddress();
	machinecode = 0xAD12F5E4D6A2F1D2;

	for (i = 0; i < mac.size(); i++)
	{
		machinecode ^= mac[i] * mac[i];
	}

	return machinecode;
}

inline void MachineVerify()
{
	int i;
	uint64 machinecode;

	if (DataInfoStruct.VerifyMachineCode)
	{
		machinecode = GetMachineCode();
#ifdef IS_DEBUG
		printf("��ǰ�����룺%X%X  �����ڲ������룺%X%X\r\n",(DWORD)((machinecode >> 32) & 0xFFFFFFFF),(DWORD)(machinecode & 0xFFFFFFFF),(DWORD)((DataInfoStruct.MachineCode >> 32) & 0xFFFFFFFF),(DWORD)(DataInfoStruct.MachineCode & 0xFFFFFFFF));
#endif
		(machinecode != DataInfoStruct.MachineCode) ? ShellExit() : 0;
		(machinecode != DataInfoStruct.MachineCode) ? ShellExit() : 0;
		(machinecode == DataInfoStruct.MachineCode) ? 0 : ShellExit();
		(machinecode != DataInfoStruct.MachineCode) ? ShellExit() : 0;
	}
}

inline void DataVerify()//������������֤
{
	DWORD i;
	char *p = DataInfo;
	char *q = DataInfo2;
	char xorcode;
	DWORD crccode;

	//˫���ݿ�ȶ�

	for (i = 0; i < BLOCK_SIZE; i++)
	{
		(p[i] == q[i]) ? 0 : ShellExit();
		(p[i] != q[i]) ? ShellExit() : 0;
		(p[i] == q[i]) ? 0 : ShellExit();
		(p[i] != q[i]) ? ShellExit() : 0;
	}

#ifdef IS_DEBUG
	printf("���ݿ�Ա�OK\r\n");
#endif

	//XOR���� ȥ��XOR��CRC32CODE��5���ֽ�����

	xorcode = 0x7C;

	for (i = 0; i < BLOCK_SIZE; i++)
	{
		if ((i != DATAINFO_XORDATAVERIFYCODE) && (!ValidNumRange(i, DATAINFO_CRC32DATAVERIFYCODE, DATAINFO_CRC32DATAVERIFYCODE + 3)))
		{
			xorcode ^= DataInfo[i];
		}
	}

#ifdef IS_DEBUG
	printf("XOR��������%u\r\n",(DWORD)xorcode);
#endif

	for (i = 0; i < 5; i++)
	{
		(xorcode == DataInfoStruct.XORDataVerifyCode) ? 0 : ShellExit();
	}

	(xorcode != DataInfoStruct.XORDataVerifyCode) ? ShellExit() : 0;

#ifdef IS_DEBUG
	printf("XOR����OK\r\n");
#endif

	//CRC32���� ��CRC32CODE 4�ֽ���������Ϊ0x418A2E3D

	crccode = 0xA582ECB6;
	*((DWORD *)(DataInfo2 + DATAINFO_CRC32DATAVERIFYCODE)) = 0x418A2E3D;
	crccode = CRC32(crccode, (uchar *)DataInfo2, BLOCK_SIZE);
#ifdef IS_DEBUG
	printf("CRC32��������%u\r\n",crccode);
#endif
	(crccode == DataInfoStruct.CRC32DataVerifyCode) ? 0 : ShellExit();
	(crccode != DataInfoStruct.CRC32DataVerifyCode) ? ShellExit() : 0;

#ifdef IS_DEBUG
	printf("CRC32�Ա�OK\r\n");
#endif
}

inline void DataInfoLoad()//����������Ϣ
{
	DataInfoStruct.FileSize = *((LPDWORD)(DataInfo + DATAINFO_FILESIZE));
	DataInfoStruct.FileOriginSize = *((LPDWORD)(DataInfo + DATAINFO_FILEORIGINSIZE));
	DataInfoStruct.FileAESPassword = DataInfo + DATAINFO_FILEAESPASSWORD;
	DataInfoStruct.FileXORPassword = DataInfo + DATAINFO_FILEXORPASSWORD;
	DataInfoStruct.DLLVirtualName = DataInfo + DATAINFO_DLLVIRTUALNAME;
	DataInfoStruct.VerifyMachineCode = (*((uchar *)(DataInfo2 + DATAINFO_VERIFYMACHINECODE)) != 0xAF) ? true : false;
	DataInfoStruct.MachineCode = *((uint64 *)(DataInfo2 + DATAINFO_MACHINECODE));
	DataInfoStruct.XORDataVerifyCode = *((char *)(DataInfo2 + DATAINFO_XORDATAVERIFYCODE));
	DataInfoStruct.CRC32DataVerifyCode = *((DWORD *)(DataInfo2 + DATAINFO_CRC32DATAVERIFYCODE));

#ifdef IS_DEBUG
	printf("���ݴ�С:%u\r\n����ԭʼ��С��%u\r\nDLL�������ƣ�%s\r\nXOR����У���룺%u\r\nCRC32����У���룺%u\r\n��֤����������ֵ��0x%X\r\n��������ֵ��0x%X\r\n",DataInfoStruct.FileSize,DataInfoStruct.FileOriginSize,DataInfoStruct.DLLVirtualName,(DWORD)DataInfoStruct.XORDataVerifyCode,DataInfoStruct.CRC32DataVerifyCode,(DWORD)0xAF,(DWORD)(*((uchar *)(DataInfo2 + DATAINFO_VERIFYMACHINECODE))));
#endif
}

inline char * DataDecrypt(char *CurModuleCode, DWORD Length)//���ݽ���
{
	int xorptr = 0;//xor��Կָ��
	DWORD i;
	char iv[16];
	LPVOID Data[4] = { Data1, Data2, Data3, Data4 };
	LPVOID TempDataBuf = VirtualAlloc(NULL, max(DataInfoStruct.FileSize, BLOCK_NUM * BLOCK_SIZE), MEM_COMMIT, PAGE_READWRITE);
	LPVOID ResultDataBuf = VirtualAlloc(NULL, DataInfoStruct.FileOriginSize * 2, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
	char *TempResult = new char[(DataInfoStruct.FileOriginSize + DataInfoStruct.FileOriginSize % 16) * 2];

	//���ݿ鿽���ϲ�

#ifdef IS_DEBUG
	printf("��ʼ�������ݿ鿽���ϲ�\r\n");
#endif

	for (i = 0; i < BLOCK_NUM; i++)
	{
#ifdef IS_DEBUG
		printf("��ʼ������%u��  Դ�ڴ棺0x%X-%X  Ŀ���ڴ棺0x%X\r\n",i,Data[i],Data1,((DWORD)TempDataBuf + i * BLOCK_SIZE));
#endif
		CopyMemory((LPVOID)((DWORD)TempDataBuf + i * BLOCK_SIZE), Data[i], BLOCK_SIZE);
	}

#ifdef IS_DEBUG
	printf("�����鿽�����\r\n");
#endif

	if (((BLOCK_NUM * BLOCK_SIZE) + Length) < DataInfoStruct.FileSize)
	{
		ShellExit();
	}

	if (DataInfoStruct.FileSize > (BLOCK_NUM * BLOCK_SIZE))
	{
		CopyMemory((LPVOID)((DWORD)TempDataBuf + BLOCK_NUM * BLOCK_SIZE), (LPVOID)((DWORD)CurModuleCode + (Length - (DataInfoStruct.FileSize - (BLOCK_NUM * BLOCK_SIZE)))), DataInfoStruct.FileSize - (BLOCK_NUM * BLOCK_SIZE));
	}

#ifdef IS_DEBUG
	printf("��չ�鿽�����\r\n");
#endif

	//XOR����

#ifdef IS_DEBUG
	printf("��ʼ����XOR����\r\n");
#endif

	xorptr = 0;

	for (i = 0; i < DataInfoStruct.FileSize; i++)
	{
		((char *)TempDataBuf)[i] ^= DataInfoStruct.FileXORPassword[xorptr++];

		if (xorptr == XORPASSWORDLEN)
		{
			xorptr = 0;
		}
	}

	//AES����

#ifdef IS_DEBUG
	printf("��ʼ����AES����\r\n");
#endif

	memcpy(iv, InputVector, 16);
#ifdef IS_DEBUG
	printf("iv�������\r\n");
#endif
	AES_CBC_decrypt_buffer((uint8_t *)TempResult, (uint8_t *)TempDataBuf, DataInfoStruct.FileSize, (uint8_t *)DataInfoStruct.FileAESPassword, (uint8_t *)iv);
#ifdef IS_DEBUG
	printf("AES��������ִ�����\r\n");
	printf("Ŀ�����ݵ�ַ��0x%X ���ݴ�С��%u\r\n",(DWORD)ResultDataBuf,DataInfoStruct.FileOriginSize);
#endif
	memcpy(ResultDataBuf, TempResult, DataInfoStruct.FileOriginSize);
#ifdef DEBUG
	printf("AES�������\r\n");
#endif
	return ((char *)ResultDataBuf);
	//return ((char *)TempDataBuf);
}

inline uint64 MachineCodeEncrypt(uint64 x)
{
	uint64 r;
	char iv[16];
	char key[16] = { 0x52, 0x63, 0x75, 0x82, 0x63, 0x75, 0xA5, 0x9F, 0xCC, 0x7A, 0x82, 0x6B, 0x77, 0xAF, 0xBC, 0x1A };

	memcpy(iv, InputVector, 16);
	AES_CBC_encrypt_buffer((uint8_t *)&r, (uint8_t *)&x, 8, (uint8_t *)key, (uint8_t *)iv);
	return r;
}

int ShellExit()
{
	__asm
	{
		MOV ESP, MainESP
			MOV EBP, MainEBP
			JMP ExitAddress
	}

	return 0;
}

int main()
{
	HMODULE hMod;//DLL�ڴ��ַ��DLL������
	DWORD ReadFileSize;//�������ֻ��Ϊ��ʹ��ReadFile API������ģ���û��ʲôʵ������
	char *DecryptedData;
	char *UnknownData[5] = { UnknownData1, UnknownData2, UnknownData3, UnknownData4, UnknownData5 };//��ֹUnknown�鱻�Ż�
	int i;

	__asm
	{
		MOV ExitAddress, OFFSET ExitPrg
			MOV MainESP, ESP
			MOV MainEBP, EBP
	}

	for (i = 0; i < 5; i++)//��ֹUnknown�鱻�Ż�
	{
		memset(UnknownData[i], UnknownData[i][0], 1);
	}

	CsrGetProcessId = (Csr)GetProcAddress(GetModuleHandle("ntdll"), "CsrGetProcessId");
	CheckDebug();
#ifdef IS_DEBUG
	printf("��һ��Debug�����ɣ�\r\n");
#endif

	BeginCheckTimingDebug();
	InitCRCTable();//��ʼ��CRC��
	EndCheckTimingDebug(40000000);
	BeginCheckTimingDebug();
	DataInfoLoad();//����������Ϣ
	EndCheckTimingDebug(40000000);
	BeginCheckTimingDebug();
	DataVerify();//������Ϣ��֤
	EndCheckTimingDebug(40000000);
#ifdef IS_DEBUG
	printf("������Ϣ��֤ͨ����\r\n");
#endif
	BeginCheckTimingDebug();
	MachineVerify();//������֤
#ifdef IS_DEBUG
	printf("������֤ͨ����\r\n");
#endif
	EndCheckTimingDebug(40000000);

#ifdef IS_DEBUG
	printf("׼������\r\n");
#endif

	GetModuleFileName(NULL, (LPSTR)CurPath, sizeof(CurPath));
#ifdef IS_DEBUG
	printf("׼���������ļ�\r\n");
#endif
	HANDLE DLLFile = CreateFile((LPCSTR)CurPath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);//����·����DLL�ļ�

	if ((DLLFile == NULL) || ((DWORD)DLLFile == 0xFFFFFFFF))
	{
		return 0;//�����ļ��޷���
	}
	else
	{
		CheckDebug();
		DWORD FileSize = GetFileSize(DLLFile, NULL);//��ȡDLL�ļ���С
#ifdef DEBUG
		printf("׼������洢�ռ䣺%u\r\n",FileSize);
#endif
		LPVOID DLLFileBuf = VirtualAlloc(NULL, FileSize, MEM_COMMIT, PAGE_READWRITE);//����DLL�ļ��洢�ڴ�ռ�

		CheckDebug();

		if (ReadFile(DLLFile, DLLFileBuf, FileSize, &ReadFileSize, NULL) == NULL)//����DLL�ļ�
		{
			CloseHandle(DLLFile);
			return 0;//�ļ���ȡʧ��
		}
		else
		{
			BeginCheckTimingDebug();
			CheckDebug();
			EndCheckTimingDebug(40000000);
#ifdef IS_DEBUG
			printf("��ʼ�����ļ�\r\n");
#endif
			DecryptedData = DataDecrypt((char *)DLLFileBuf, FileSize);
#ifdef IS_DEBUG
			printf("�����ļ��ɹ�\r\n");
#endif
#ifdef IS_DEBUG
			HANDLE OutputFile = CreateFile((LPCSTR)"Out.dll", GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

			if ((OutputFile == NULL) || ((DWORD)OutputFile == 0xFFFFFFFF))
			{
				printf("Output File \"%s\" can't be created!\r\n", "D:\\Temp\\Out.dll");
				CloseHandle(DLLFile);
				CloseHandle(OutputFile);
				return 0;
			}

			DWORD WrittenDataSize;

			if (WriteFile(OutputFile, DecryptedData, DataInfoStruct.FileOriginSize, &WrittenDataSize, NULL) == FALSE)
			{
				printf("Write Output File \"%s\" Fail!\r\n", "D:\\Temp\\Out.dll");
				CloseHandle(DLLFile);
				CloseHandle(OutputFile);
				return 0;
			}

			CloseHandle(DLLFile);
			CloseHandle(OutputFile);
#endif

			if (DataDecrypt == NULL)
			{
				return 0;//���ݽ���ʧ��
			}

			CheckDebug();
#ifdef IS_DEBUG
			printf("׼�������������,�ڴ��һ���ַ�%c\r\n", DecryptedData[0]);
#endif
			hMod = (HMODULE)DLLMemLoad(DecryptedData, DataInfoStruct.FileOriginSize, DataInfoStruct.DLLVirtualName, DataInfoStruct.DLLVirtualName);//DLL�ڴ���غ�����ע��Ϊ�˱�֤һЩDLLģ����������У��뱣֤MemDLLBaseName������MemDLLFullName�����Ľ�β�ǡ�.dll���������ִ�Сд��
			Run = (FUNC_RUN)GetProcAddress(hMod, "Run");//��ȡ��Run��������ַ

			if (DataInfoStruct.VerifyMachineCode)
			{
				Run(GetMachineCode(), MachineCodeEncrypt(GetMachineCode()));
			}
			else
			{
				Run(0xAF857463F6E5F3A4, MachineCodeEncrypt(0xC63F8A6E1A639E5A));
			}

#ifdef IS_DEBUG
			printf("��������\r\n");
#endif
		}
#ifdef IS_DEBUG
		printf("����ر����\r\n");
#endif
	}

	DLLMemFree((char *)hMod);//DLL�ڴ��ͷź��������ڳ������֮ǰ�������ͷż��ص�DLL�����������ܻ��쳣�˳�
#ifdef IS_DEBUG
	printf("DLL�ͷ����\r\n");
#endif
ExitPrg:
	TerminateProcess(GetCurrentProcess(), 0);
	return 0;
}