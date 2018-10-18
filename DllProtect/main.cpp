#include <cstdio>
#include <iostream>
#include <sstream>
#include <windows.h>
#include <vector>
#include "AES.h"
#include "CRC32.h"
#include <Nb30.h>
#include "ntdll.h"
#include <shlwapi.h>
#include <string.h>
#include <time.h>
#include <immintrin.h>
#include <memory>
#pragma comment(lib,"netapi32.lib")
#pragma comment(lib,"Shlwapi.lib")

using namespace std;

#define uchar unsigned char
#define uint unsigned int
#define uint64 unsigned long long

#define BLOCK_SIZE 65536
#define BLOCK_NUM 4
#define UNKNOWN_BLOCK_NUM 5

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

DATA_INFO DataInfoStruct;

char InputVector[16] = { 0x05, 0x84, 0x63, 0x75, 0x74, 0x96, 0x75, 0x89, 0x77, 0x63, 0x59, 0x66, 0xA9, 0xF6, 0x7C, 0xFE };

/*�����в���

�ļ�·����DLL�����ļ�����������
[Drive:][Path]Filename [/d Name] [/m unsigned 64-bit integer HEX String]
DLL�����ļ���������.dll(�����ִ�Сд)��β���ܳ���>=5
����ָ����������Ϊ��ʹ�û����룬�������ʽΪʮ������
/?��ʾ����

*/

char *DefaultDLLVirtualName = "view.dll";

bool ValidNumRange(int n, int min, int max)
{
	return (n >= min) && (n <= max);
}

bool DLLVirtualNameValid(char *name)
{
	int len = strlen(name);

	if (len <= 4)
	{
		return false;
	}

	return ((name[len - 4] == '.') && ((name[len - 3] | 0x20) == 'd') && ((name[len - 2] | 0x20) == 'l') && ((name[len - 1] | 0x20) == 'l'));
}

uint64 HexCharTouint64(char ch)
{
	if (ValidNumRange(ch, '0', '9'))
	{
		return ch - '0';
	}
	else if (ValidNumRange(ch | 0x20, 'a', 'f'))
	{
		return (ch | 0x20) - 'a' + 0x0A;
	}
	else
	{
		return 0xFF;
	}
}

uint64 HexStrTouint64(char *str)
{
	int len = strlen(str);
	int i;
	uint64 r = 0L;

	for (i = 0; i < len; i++)
	{
		r <<= 4;
		r |= HexCharTouint64(str[i]);
	}

	return r;
}

bool CheckRandomInstruction()
{
	DWORD result;

	__asm
	{
		PUSHAD
			MOV EAX, 1
			MOV ECX, 0
			CPUID
			MOV result, ECX
			POPAD
	}

	srand((unsigned)time(NULL));
	return (result & 0x80000000) > 0;
}

bool CheckRandomInstruction_Nosrand()
{
	DWORD result;

	__asm
	{
		PUSHAD
			MOV EAX, 1
			MOV ECX, 0
			CPUID
			MOV result, ECX
			POPAD
	}

	return (result & 0x80000000) > 0;
}

uint GetTrueRandom()
{
	if (CheckRandomInstruction_Nosrand())
	{
		uint t = 0;
		_rdseed32_step(&t);
		return t;
	}
	else
	{
		double t = rand();
		t * (double)0xFFFFFFFFL / (double)(RAND_MAX + 1);
		return (uint64)t;
	}
}

void GetTrueRandom128Bits(char *buf)
{
	*((uint *)buf) = GetTrueRandom();
	*((uint *)(buf + 4)) = GetTrueRandom();
	*((uint *)(buf + 8)) = GetTrueRandom();
	*((uint *)(buf + 12)) = GetTrueRandom();
}

char Normalization(uint n)
{
	return n % 256;
}

void GetTrueRandomSequence(char *revbuf, int len)
{
	char pKey[16];
	char pIndata[16];
	char pA[16];
	char iv[16];
	int i, j;
	uint tg;

	GetTrueRandom128Bits(pKey);

	for (i = 0; i < len; i += 4)
	{
		GetTrueRandom128Bits(pIndata);
		memcpy(iv, InputVector, 16);
		AES_CBC_encrypt_buffer((uint8_t *)pA, (uint8_t *)pIndata, 16, (uint8_t *)pKey, (uint8_t *)iv);
		memcpy(pKey, pA, 16);

		for (j = 0; j < 4; j++)
		{
			if ((i + j) == len)
			{
				goto exit;
			}

			memcpy(&tg, pA + (j * 4), 4);
			revbuf[i + j] = Normalization(tg);
		}
	}

exit:
	return;
}

char * GetBlockAddress(char *data, DWORD len, char *flagstr)
{
	DWORD i;
	DWORD j;
	bool flag;
	DWORD len2 = strlen(flagstr);

	for (i = 0; i < len; i++)
	{
		flag = true;

		for (j = 0; j < len2; j++)
		{
			if (data[i + j] != flagstr[j])
			{
				flag = false;
				break;
			}
		}

		if (flag == true)
		{
			return (char *)(data + i);
		}
	}

	return (char *)0xFFFFFFFF;
}

void Shell(char *filename)
{
	char *DLLFileBuf = NULL;
	char *EncryptedData = NULL;
	int xorptr;
	char xorcode;
	DWORD crccode;
	DWORD i;
	char *DataInfo, *DataInfo2;
	char *Data[BLOCK_NUM];
	char *UnknownData[UNKNOWN_BLOCK_NUM];

	char iv[16];

	char *pShellData;
	DWORD nShellFileSize;

	DWORD WrittenDataSize;

	char *SimpleShellFilePath = NULL;
	char *OutputFilePath = NULL;
	char szFilePath[MAX_PATH] = { 0 }, szDrive[MAX_PATH] = { 0 }, szDir[MAX_PATH] = { 0 }, szFileName[MAX_PATH] = { 0 }, szExt[MAX_PATH] = { 0 };

	//ƴ���������ļ�·��

	GetModuleFileNameA(NULL, szFilePath, sizeof(szFilePath));
	_splitpath(szFilePath, szDrive, szDir, szFileName, szExt);
	string str(szDrive);
	str.append(szDir);
	str.append("Shell.exe");
	SimpleShellFilePath = (char *)str.c_str();

	//ƴ������ļ�·��

	string str2(szDrive);
	str2.append(szDir);
	str2.append(szFileName);
	str2.append("_Encrypted.exe");
	OutputFilePath = (char *)str2.c_str();

	if (!CheckRandomInstruction())
	{
		printf("Your CPU don't support RDSEED Instruction,it's only supported by Intel CPU!\r\nProgram Will use srand and rand function to produce False Random!\r\n");
	}

	//��ȡDLL�ļ�

	HANDLE DLLFile = CreateFile((LPCSTR)filename, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

	if ((DLLFile == NULL) || ((DWORD)DLLFile == 0xFFFFFFFF))
	{
		printf("File Open Fail!\r\n");
		return;
	}

	DataInfoStruct.FileOriginSize = GetFileSize(DLLFile, NULL);//��ȡDLL�ļ���С
	DLLFileBuf = new char[DataInfoStruct.FileOriginSize];

	if (ReadFile(DLLFile, DLLFileBuf, DataInfoStruct.FileOriginSize, &DataInfoStruct.FileOriginSize, NULL) == NULL)//����DLL�ļ�
	{
		printf("File Read Fail!\r\n");
		CloseHandle(DLLFile);
		return;//�ļ���ȡʧ��
	}

	CloseHandle(DLLFile);

	//����AES��XOR��Կ

	DataInfoStruct.FileAESPassword = new char[16];
	DataInfoStruct.FileXORPassword = new char[XORPASSWORDLEN];
	GetTrueRandomSequence(DataInfoStruct.FileAESPassword, 16);
	GetTrueRandomSequence(DataInfoStruct.FileXORPassword, XORPASSWORDLEN);

	//AES����

	DataInfoStruct.FileSize = DataInfoStruct.FileOriginSize + DataInfoStruct.FileOriginSize % 16;
	EncryptedData = new char[DataInfoStruct.FileSize];
	memcpy(iv, InputVector, 16);
	AES_CBC_encrypt_buffer((uint8_t *)EncryptedData, (uint8_t *)DLLFileBuf, DataInfoStruct.FileOriginSize, (uint8_t *)DataInfoStruct.FileAESPassword, (uint8_t *)iv);
	AES_CBC_decrypt_buffer((uint8_t *)DLLFileBuf, (uint8_t *)EncryptedData, DataInfoStruct.FileSize, (uint8_t *)DataInfoStruct.FileAESPassword, (uint8_t *)iv);

	/*DataInfoStruct.FileSize = DataInfoStruct.FileOriginSize;
	EncryptedData = new char[DataInfoStruct.FileSize];
	memcpy(EncryptedData,DLLFileBuf,DataInfoStruct.FileOriginSize);*/

	//XOR����

	xorptr = 0;

	for (i = 0; i < DataInfoStruct.FileSize; i++)
	{
		((char *)EncryptedData)[i] ^= DataInfoStruct.FileXORPassword[xorptr++];

		if (xorptr == XORPASSWORDLEN)
		{
			xorptr = 0;
		}
	}

	//��ȡ����
	if (!PathFileExists(SimpleShellFilePath))
	{
		printf("Can't find SimpleShellFile!\r\n");
		return;
	}

	HANDLE hShellFile = CreateFile((LPCSTR)SimpleShellFilePath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

	if ((hShellFile == NULL) || ((DWORD)hShellFile == 0xFFFFFFFF))
	{
		printf("SimpleShellFile Open Fail!\r\n");
		return;
	}

	nShellFileSize = GetFileSize(hShellFile, NULL);

	int nFileSize = nShellFileSize + ((DataInfoStruct.FileSize > BLOCK_NUM * BLOCK_SIZE) ? (DataInfoStruct.FileSize - BLOCK_NUM * BLOCK_SIZE) : 0);
	std::shared_ptr<char> pFileData = std::shared_ptr<char>(new char[nFileSize], [](char* pData)
	{
		if (pData){
			OutputDebugStringA("Delete\n");
			delete[] pData; pData = NULL;
		}});

	pShellData = pFileData.get();
	if (ReadFile(hShellFile, pShellData, nShellFileSize, &nShellFileSize, NULL) == NULL)//���������ļ�
	{
		printf("File Read Fail!\r\n");
		CloseHandle(hShellFile);
		return;//�ļ���ȡʧ��
	}
	CloseHandle(hShellFile);

	//��λ���ݿ�

	Data[0] = GetBlockAddress(pShellData, nShellFileSize, "DLLLoaderLZRData1");
	Data[1] = GetBlockAddress(pShellData, nShellFileSize, "DLLLoaderLZRData2");
	Data[2] = GetBlockAddress(pShellData, nShellFileSize, "DLLLoaderLZRData3");
	Data[3] = GetBlockAddress(pShellData, nShellFileSize, "DLLLoaderLZRData4");
	UnknownData[0] = GetBlockAddress(pShellData, nShellFileSize, "DLLLoaderLZRUnknownData1");
	UnknownData[1] = GetBlockAddress(pShellData, nShellFileSize, "DLLLoaderLZRUnknownData2");
	UnknownData[2] = GetBlockAddress(pShellData, nShellFileSize, "DLLLoaderLZRUnknownData3");
	UnknownData[3] = GetBlockAddress(pShellData, nShellFileSize, "DLLLoaderLZRUnknownData4");
	UnknownData[4] = GetBlockAddress(pShellData, nShellFileSize, "DLLLoaderLZRUnknownData5");
	DataInfo = GetBlockAddress(pShellData, nShellFileSize, "DLLoaderLZRDataInfo1");
	DataInfo2 = GetBlockAddress(pShellData, nShellFileSize, "DLLoaderLZRDataInfo2");

	//��ַ���

	for (i = 0; i < BLOCK_NUM; i++)
	{
		if ((DWORD)Data[i] == 0xFFFFFFFF)
		{
			goto fail;
		}
	}

	for (i = 0; i < UNKNOWN_BLOCK_NUM; i++)
	{
		if ((DWORD)UnknownData[i] == 0xFFFFFFFF)
		{
			goto fail;
		}
	}

	if (((DWORD)DataInfo[i] == 0xFFFFFFFF) || ((DWORD)DataInfo2[i] == 0xFFFFFFFF))
	{
		goto fail;
	}

	goto succ;

fail:
	printf("SimpleShellFile's Content Error!\r\n");
	return;

succ:

	//���������ݿ������������

	for (i = 0; i < BLOCK_NUM; i++)
	{
		GetTrueRandomSequence(Data[i], BLOCK_SIZE);
	}

	for (i = 0; i < UNKNOWN_BLOCK_NUM; i++)
	{
		GetTrueRandomSequence(UnknownData[i], BLOCK_SIZE);
	}

	GetTrueRandomSequence(DataInfo, BLOCK_SIZE);//DataInfo2������䣬��ΪDataInfo2��DataInfo���ݱ���ȼ�

	//д��DLL��������

	for (i = 0; i < min(BLOCK_NUM, max(DataInfoStruct.FileSize / BLOCK_SIZE, 1)); i++)
	{
		memcpy(Data[i], EncryptedData + BLOCK_SIZE * i, min(BLOCK_SIZE, DataInfoStruct.FileSize - BLOCK_SIZE * i));
	}

	if (DataInfoStruct.FileSize > BLOCK_NUM * BLOCK_SIZE)
	{
		memcpy(pShellData + nShellFileSize, EncryptedData + BLOCK_NUM * BLOCK_SIZE, DataInfoStruct.FileSize - BLOCK_NUM * BLOCK_SIZE);
		nShellFileSize += DataInfoStruct.FileSize - BLOCK_NUM * BLOCK_SIZE;
	}

	//д��DataInfo

	*((DWORD *)(DataInfo + DATAINFO_FILESIZE)) = DataInfoStruct.FileSize;
	*((DWORD *)(DataInfo + DATAINFO_FILEORIGINSIZE)) = DataInfoStruct.FileOriginSize;
	memcpy(DataInfo + DATAINFO_FILEAESPASSWORD, DataInfoStruct.FileAESPassword, 16);
	memcpy(DataInfo + DATAINFO_FILEXORPASSWORD, DataInfoStruct.FileXORPassword, XORPASSWORDLEN);
	memcpy(DataInfo + DATAINFO_DLLVIRTUALNAME, DataInfoStruct.DLLVirtualName, strlen(DataInfoStruct.DLLVirtualName) + 1);
	DataInfo[DATAINFO_VERIFYMACHINECODE] = DataInfoStruct.VerifyMachineCode ? 0x41 : 0xAF;
	*((uint64 *)(DataInfo + DATAINFO_MACHINECODE)) = DataInfoStruct.MachineCode;

	//���ݿ�XOR����

	xorcode = 0x7C;

	for (i = 0; i < BLOCK_SIZE; i++)
	{
		if ((i != DATAINFO_XORDATAVERIFYCODE) && (!ValidNumRange(i, DATAINFO_CRC32DATAVERIFYCODE, DATAINFO_CRC32DATAVERIFYCODE + 3)))
		{
			xorcode ^= DataInfo[i];
		}
	}

	DataInfo[DATAINFO_XORDATAVERIFYCODE] = xorcode;

	//���ݿ�CRC32���� ��CRC32CODE 4�ֽ���������Ϊ0x418A2E3D

	InitCRCTable();
	crccode = 0xA582ECB6;
	*((DWORD *)(DataInfo + DATAINFO_CRC32DATAVERIFYCODE)) = 0x418A2E3D;
	crccode = CRC32(crccode, (uchar *)DataInfo, BLOCK_SIZE);
	*((DWORD *)(DataInfo + DATAINFO_CRC32DATAVERIFYCODE)) = crccode;

	//��DataInfo������DataInfo2��

	memcpy(DataInfo2, DataInfo, BLOCK_SIZE);

	//�����ܺ������д��

	HANDLE OutputFile = CreateFile((LPCSTR)OutputFilePath, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

	if ((OutputFile == NULL) || ((DWORD)OutputFile == 0xFFFFFFFF))
	{
		printf("Output File \"%s\" can't be created!\r\n", OutputFilePath);
		return;
	}

	if (WriteFile(OutputFile, pShellData, nShellFileSize, &WrittenDataSize, NULL) == FALSE)
	{
		printf("Write Output File \"%s\" Fail!\r\n", OutputFilePath);
		CloseHandle(OutputFile);
		return;
	}
	
	printf("DLL�ӿǳɹ�������ļ�·����\"%s\"\r\n", OutputFilePath);
	CloseHandle(OutputFile);
}

int main(int argc, char *argv[])
{
	int i, j;
	bool pathgetflag = false;
	char *filename = NULL;

	memset(&DataInfoStruct, 0, sizeof(DataInfoStruct));

	for (i = 1; i < argc; i++)
	{
		for (j = i + 1; j < argc; j++)
		{
			if ((argv[i][0] == '/') && (argv[j][0] == '/'))
			{
				if (strlen(argv[i]) == strlen(argv[j]))
				{
					if (strcmp(argv[i], argv[j]) == 0)
					{
						printf("�ظ��������п���\"%s\"�����������ʹ�ò���/?\r\n", argv[i]);
						return 0;
					}
				}
			}
		}
	}

	for (i = 1; i < argc; i++)
	{
		if (argv[i][0] == '/')//�����п���
		{
			if ((i == (argc - 1)) && (argv[i][1] != '?'))
			{
				printf("�����п���\"%s\"�Ҳ������������������ʹ�ò���/?\r\n", argv[i]);
				return 0;
			}

			if (argv[i][2] != 0x00)
			{
				printf("��Ч�������п���\"%s\"�����������ʹ�ò���/?\r\n", argv[i]);
				return 0;
			}

			switch (argv[i][1])
			{
			case 'd':
			case 'D':

				if (!DLLVirtualNameValid(argv[i + 1]))
				{
					printf("�����п���\"%s\"�������Ϸ�����β��Ϊ.dll(�����ִ�Сд)���ܳ���С��5�����������ʹ�ò���/?\r\n", argv[i]);
					return 0;
				}

				break;

			case 'm':
			case 'M':

				if (strlen(argv[i + 1]) > 16)
				{
					printf("�����п���\"%s\"�������Ϸ�����ֵ�����Ӧ����0000000000000000-FFFFFFFFFFFFFFFF��Χ�ڣ����������ʹ�ò���/?\r\n", argv[i]);
					return 0;
				}

				DataInfoStruct.MachineCode = HexStrTouint64(argv[i + 1]);
				DataInfoStruct.VerifyMachineCode = true;
				break;

			case '?':

				if (argc != 2)
				{
					printf("�����п���/?���ܺ����������п��غ��ã����������ʹ�ò���/?\r\n");
					return 0;
				}

				printf("%s [Drive:][Path]Filename [/d Name] [/m unsigned 64-bit integer HEX String]\r\n", argv[0]);
				printf("/d Name DLL�����ļ�����������.dll(�����ִ�Сд)��β���ܳ���>=5\r\n");
				printf("/m unsigned 64-bit integer HEX String������ָ����������Ϊ��ʹ�û����룬�������ʽΪʮ�����ƣ���ΧΪ0000000000000000-FFFFFFFFFFFFFFFF");
				printf("/? ����");
				break;

			default:
				printf("��Ч�������п���\"%s\"�����������ʹ�ò���/?\r\n", argv[i]);
				return 0;
				break;
			}

			i++;
		}
		else if (pathgetflag)
		{
			printf("�ļ���ֻ��ָ��һ�Σ����������ʹ�ò���/?\r\n");
			return 0;
		}
		else if (PathFileExists(argv[i]) == FALSE)
		{
			printf("�Ҳ����ļ�\"%s\"�����������ʹ�ò���/?\r\n", argv[i]);
			return 0;
		}
		else
		{
			filename = argv[i];
		}
	}

	if (filename == NULL)
	{
		printf("ȱ���ļ����������������ʹ�ò���/?\r\n");
		return 0;
	}

	if (DataInfoStruct.DLLVirtualName == NULL)
	{
		DataInfoStruct.DLLVirtualName = DefaultDLLVirtualName;
	}

	Shell(filename);
	return 0;
}