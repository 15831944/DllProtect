// TestDLL.cpp : ���� DLL Ӧ�ó���ĵ���������
//

#include "stdafx.h"
#include "TestDLL.h"


// ���ǵ���������һ��ʾ��
TESTDLL_API int nTestDLL=0;

// ���ǵ���������һ��ʾ����
TESTDLL_API int fnTestDLL(void)
{
    return 42;
}

// �����ѵ�����Ĺ��캯����
// �й��ඨ�����Ϣ������� TestDLL.h
CTestDLL::CTestDLL()
{
    return;
}
