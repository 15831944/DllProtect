#ifndef __DLLLOADER_H__
#define __DLLLOADER_H__

	char* DLLMemLoad(char* DLLFileBuf, DWORD DLLFileSize, char* MemDLLBaseName, char* MemDLLFullName);//DLL�ڴ���غ�����ע��Ϊ�˱�֤һЩDLLģ����������У��뱣֤MemDLLBaseName������MemDLLFullName�����Ľ�β�ǡ�.dll���������ִ�Сд��
	void DLLMemFree(char* DLLMemBaseAddress);//DLL�ڴ��ͷź��������ڳ������֮ǰ�������ͷż��ص�DLL�����������ܻ��쳣�˳�
#endif