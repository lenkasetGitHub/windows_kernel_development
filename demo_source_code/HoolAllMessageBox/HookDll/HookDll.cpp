// HookDll.cpp : ���� DLL �ĳ�ʼ�����̡�
//

#include "stdafx.h"
#include "HookDll.h"
#include <Windows.h>
#include <psapi.h>


#ifdef _DEBUG
#define new DEBUG_NEW
#endif

/*
	 ȫ�ֹ�������
*/
#pragma data_seg("Share")
HWND g_hWnd = NULL ;			// �����ھ��
HINSTANCE g_hInstance = NULL;	// ��dllʵ�����
HHOOK hhk = NULL;				// ��깳�Ӿ��
#pragma data_seg()
#pragma comment(linker,"/section:Share,rws")

HANDLE hProcess = NULL;				//	��ǰ����
BOOL bIsInJected = FALSE;			//	�Ƿ���ע����
TCHAR* msgToMain = new TCHAR[200];	//	���������������Ϣ


/*
	ԭ��������
*/
typedef int (WINAPI *TypeMsgBoxA)(HWND hWnd,LPCSTR lpText, LPCSTR lpCaption,UINT uType);
typedef int (WINAPI *TypeMsgBoxW)(HWND hWnd,LPCWSTR lpText,LPCWSTR lpCaption,UINT uType);

TypeMsgBoxA oldMsgBoxA = NULL;	// ���ڱ���ԭ������ַ
TypeMsgBoxW oldMsgBoxW = NULL;	// ���ڱ���ԭ�����ַ
FARPROC pfMsgBoxA = NULL;		// ָ��ԭ������ַ��Զָ��
FARPROC pfMsgBoxW = NULL;		// ָ��ԭ������ַ��Զָ��

#define CODE_LENGTH 5
BYTE oldCodeA[CODE_LENGTH];	// ����ԭ��API��ڴ���
BYTE oldCodeW[CODE_LENGTH];	// ����ԭ��API��ڴ���
BYTE newCodeA[CODE_LENGTH];	// ������API��ڴ��룬jmp xxxx
BYTE newCodeW[CODE_LENGTH];	// ������API��ڴ��룬jmp xxxx

/* 
	�Լ���д��API
*/
int WINAPI MyMessageBoxA(HWND hWnd,LPCSTR lpText,LPCSTR lpCation,UINT uType);
int WINAPI MyMessageBoxW(HWND hWnd,LPCWSTR lpText,LPCWSTR lpCation,UINT uType);

/* 
	��������ԭ������
*/
void HookOn();			//	��ʼHOOK
void HookOff();			//	�ر�HOOK
void Inject();			//	ע��
BOOL WINAPI StartHook(HWND hWnd);	// ���ع���
BOOL WINAPI StopHook();				// ж�ع���
bool AdjustPrivileges();			// ����Ȩ��



//
//TODO: ����� DLL ����� MFC DLL �Ƕ�̬���ӵģ�
//		��Ӵ� DLL �������κε���
//		MFC �ĺ������뽫 AFX_MANAGE_STATE �����ӵ�
//		�ú�������ǰ�档
//
//		����:
//
//		extern "C" BOOL PASCAL EXPORT ExportedFunction()
//		{
//			AFX_MANAGE_STATE(AfxGetStaticModuleState());
//			// �˴�Ϊ��ͨ������
//		}
//
//		�˺������κ� MFC ����
//		������ÿ��������ʮ����Ҫ������ζ��
//		��������Ϊ�����еĵ�һ�����
//		���֣������������ж������������
//		������Ϊ���ǵĹ��캯���������� MFC
//		DLL ���á�
//
//		�й�������ϸ��Ϣ��
//		����� MFC ����˵�� 33 �� 58��
//

// CHookDllApp

BEGIN_MESSAGE_MAP(CHookDllApp, CWinApp)
END_MESSAGE_MAP()


// CHookDllApp ����

CHookDllApp::CHookDllApp()
{
	// TODO: �ڴ˴����ӹ�����룬
	// ��������Ҫ�ĳ�ʼ�������� InitInstance ��
}


// Ψһ��һ�� CHookDllApp ����

CHookDllApp theApp;


// CHookDllApp ��ʼ��
/*
	dll������ڣ����������dllʱ����ִ��InitInstance()  
*/
BOOL CHookDllApp::InitInstance()
{
	CWinApp::InitInstance();
	g_hInstance = AfxGetInstanceHandle();//	��ȡ��ǰDLLʵ�����
	
	AdjustPrivileges();	//	���Ȩ��
	DWORD dwPid = ::GetCurrentProcessId();
	hProcess = ::OpenProcess(PROCESS_ALL_ACCESS,0,dwPid);
/*	
	TCHAR* procName = new TCHAR[MAX_PATH];
	GetModuleFileName(NULL,procName,MAX_PATH);  // �������ȡ�ý���������ô�Ϳ����ж��Ƿ��ǻ��ܽ���������
	CString info;
	info.Format(_T("����id = %d , ������ %s"),dwPid,procName);
	AfxMessageBox(info);
*/	
	if (hProcess == NULL)
	{
		CString str;
		str.Format(_T("OpenProcess fail�� and error code = %d"),GetLastError());
		AfxMessageBox(str);
		return FALSE;
	}

	Inject();	// ��ʼע��
	
	return TRUE;
}
int CHookDllApp::ExitInstance()
{
	/*
		dll�˳�ʱ��һ��Ҫ�ǵûָ�ԭAPI����ڣ�����
		���Ǳ�д��dll�ᱻע�뵽����Ŀ������У���dll�˳�ʱ��û�лָ�ԭAPI��ڣ�
		��ô���ҹ��ĳ����ٴε��ø�APIʱ���ᷢ������
		��Ϊ���ǵ�dll�����Ѿ��˳�����ԭAPI�������Ϊ�����������API����ڣ���
		ʱ���ҹ��ĳ����޷��ҵ�����ʵ�ֵ�API��Ȼ��ԭAPI�ĵ�ַ��û�б��ָ���Ҳ��
		���ò���ԭAPI����ʱ������Ȼ�ᷢ�������ˡ� 
	*/

	HookOff();

	return CWinApp::ExitInstance();
}

/*
	 ����Ȩ��
*/
bool AdjustPrivileges() {
	HANDLE hToken;
	TOKEN_PRIVILEGES tp;
	TOKEN_PRIVILEGES oldtp;
	DWORD dwSize=sizeof(TOKEN_PRIVILEGES);
	LUID luid;

	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
		if (GetLastError()==ERROR_CALL_NOT_IMPLEMENTED) return true;
		else return false;
	}
	if (!LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &luid)) {
		CloseHandle(hToken);
		return false;
	}
	ZeroMemory(&tp, sizeof(tp));
	tp.PrivilegeCount=1;
	tp.Privileges[0].Luid=luid;
	tp.Privileges[0].Attributes=SE_PRIVILEGE_ENABLED;
	/* Adjust Token Privileges */
	if (!AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), &oldtp, &dwSize)) {
		CloseHandle(hToken);
		return false;
	}
	// close handles
	CloseHandle(hToken);
	return true;
}


/*
	 ��깳���ӹ��̣�Ŀ���Ǽ��ر�dll��ʹ�����ĳ����С�
	 ��깳�ӵ����ã��������ĳ���򴰿���ʱ���ͻ�����������dll��
*/
LRESULT CALLBACK MouseProc(
						int nCode,	  // hook code
						WPARAM wParam,// message identifier
						LPARAM lParam // mouse coordinates
						)
{

	return CallNextHookEx(hhk,nCode,wParam,lParam);
}

/*
	������Ϊlength��pcodeд�뵽��ַlpAddress�С�
*/
void WriteMemory(LPVOID lpAddress,BYTE* pcode,int length)
{
	//
	//	��֤�����̾����ΪNULL
	//
	ASSERT(hProcess != NULL);

	DWORD dwTemp,dwOldProtect,dwRet,dwWrited;

	//
	// �޸�API���ǰlength���ֽ�Ϊ jmp xxxx
	//
	VirtualProtectEx(hProcess,lpAddress,length,PAGE_READWRITE,&dwOldProtect);
	dwRet = WriteProcessMemory(hProcess,lpAddress,pcode,length,&dwWrited);
	if ( 0 == dwRet || 0 == dwWrited)
	{
		AfxMessageBox(_T("�ޣ���д��ʧ��"));
	}
	VirtualProtectEx(hProcess,lpAddress,length,dwOldProtect,&dwTemp);
}

/*
	����API��ַ�滻ԭAPI��ַ
*/
void HookOn()
{
	ASSERT(hProcess != NULL);

	DWORD dwTemp,dwOldProtect,dwRet,dwWrited;
	
	WriteMemory(pfMsgBoxA,newCodeA,CODE_LENGTH);
	WriteMemory(pfMsgBoxW,newCodeW,CODE_LENGTH);

}

/*	
	�ָ�ԭAPI��ַ
*/
void HookOff()
{
	ASSERT(hProcess != NULL);

	DWORD dwTemp,dwOldProtect,dwRet,dwWrited;

	WriteMemory(pfMsgBoxA,oldCodeA,CODE_LENGTH);
	WriteMemory(pfMsgBoxW,oldCodeW,CODE_LENGTH);
}

/*
	ע��
*/
void Inject()
{
	if ( TRUE == bIsInJected)
	{
		return;
	}
	bIsInJected = TRUE;	// ��ֻ֤����һ��


	//
	// ��ȡ����
	//
	HMODULE hmodle = ::LoadLibrary(_T("User32.dll"));
	oldMsgBoxA = (TypeMsgBoxA) ::GetProcAddress(hmodle,"MessageBoxA");
	pfMsgBoxA = (FARPROC)oldMsgBoxA;

	oldMsgBoxW = (TypeMsgBoxW) ::GetProcAddress(hmodle,"MessageBoxW");
	pfMsgBoxW = (FARPROC)oldMsgBoxW;

	if (pfMsgBoxA == NULL)
	{
		AfxMessageBox(_T("��ȡ MessageBoxA ����ʧ��"));
		return;
	}
	if ( pfMsgBoxW == NULL)
	{
		AfxMessageBox(_T("��ȡ MessageBoxW ����ʧ��"));
		return;
	}

	//
	// ����ԭAPI��ַ
	//
	_asm
	{
		lea edi,oldCodeA	// ȡ�������ַ
		mov esi,pfMsgBoxA	// API��ַ
		cld					// ���÷���
		mov ecx,CODE_LENGTH
		rep movsb
	}

	_asm
	{
		lea edi,oldCodeW
		mov esi,pfMsgBoxW
		cld
		mov ecx,CODE_LENGTH
		rep movsb
	}

	//
	// ���µ�ַ���Ƶ����
	//
	newCodeA[0] = newCodeW [0] = 0xe9;	// jmp ָ������
	_asm
	{
		lea eax,MyMessageBoxA		// ��API��ַ
		mov ebx,pfMsgBoxA			// ԭAPI��ַ
		sub eax,ebx				
		sub eax,CODE_LENGTH			// ��ת��ַ = ��API��ַ - ԭAPI��ַ - ָ���
		mov dword ptr [newCodeA+1],eax // eax 32bit =  4 BYTE
	}

	_asm
	{
		lea eax,MyMessageBoxW
		mov ebx,pfMsgBoxW
		sub eax,ebx
		sub eax,CODE_LENGTH
		mov dword ptr [newCodeW + 1],eax
	}

	HookOn();	//	��ʼHOOK

}

//
// ��װ����
//
BOOL WINAPI StartHook(HWND hWnd)
{
	g_hWnd = hWnd;
	hhk = ::SetWindowsHookEx(WH_MOUSE,MouseProc,g_hInstance,0);

	if (hhk == NULL)
	{
		return FALSE;
	}
	else
	{
		
		return TRUE;
	}
}

//
// ж�ع���
//
BOOL WINAPI StopHook()
{	
	/*
		ж�ع���ʱ��һ��Ҫ�ǵûָ�ԭAPI��ڡ�
		����ָ���ֻ���������ԭAPI��ڣ����������API��ڻ�û�б��ָ���
		������Ǳ��봦��dll�˳����̣����ں���ExitInstance()�У����ûָ�  
		API��ڵĺ���HookOff(),ֻ�����������������ٴε���ԭAPIʱ���Ų�
		�ᷢ������
		������HOOK���г����ĳ��ϵͳAPIʱ��ǧ��Ҫע����ExitInstance()��
		����HookOff()����������
	*/
	HookOff();
	if (hhk!=NULL)
	{
		UnhookWindowsHookEx(hhk);
		FreeLibrary(g_hInstance);
	}

	return TRUE;
}


/*
	�Լ������滻��API
*/
int WINAPI MyMessageBoxA(HWND hWnd,LPCSTR lpText,LPCSTR lpCation,UINT uType)
{
	int nRet = 0;

	HookOff();
/*
	TCHAR* procName = new TCHAR[MAX_PATH];
	GetModuleFileName(NULL,procName,MAX_PATH);  // �������ȡ�ý���������ô�Ϳ����ж��Ƿ��ǻ��ܽ���������
	CString info;
	info.Format(_T("������ %s"),procName);
	AfxMessageBox(info);*/
	nRet = ::MessageBoxA(hWnd,"���� ^_^��MessageBoxA �� HOOK ��",lpCation,uType);
	nRet = ::MessageBoxA(hWnd,lpText,lpCation,uType);
/*	
	info.Format(_T("MyMessageBoxA �� HOOK ��"));
	//msgToMain = (TCHAR *)malloc(info.GetLength() * sizeof(TCHAR) + 1);
	memset(msgToMain,0,150);
	msgToMain = info.GetBuffer(info.GetLength());
	::SendMessage(g_hWnd,WM_HOOKMSG,(WPARAM)msgToMain,0);
	*/
	HookOn();

	return nRet;
}

int WINAPI MyMessageBoxW(HWND hWnd,LPCWSTR lpText,LPCWSTR lpCation,UINT uType)
{
	int nRet = 0;

	HookOff();
	
/*	TCHAR* procName = new TCHAR[MAX_PATH];
	GetModuleFileName(NULL,procName,MAX_PATH);  // �������ȡ�ý���������ô�Ϳ����ж��Ƿ��ǻ��ܽ���������
	CString info;
	info.Format(_T("������ %s"),procName);
	AfxMessageBox(info);*/
	nRet = ::MessageBoxW(hWnd,_T("O(��_��)O����~��MMessageBoxW �� HOOK ��"),lpCation,uType);
	nRet = ::MessageBoxW(hWnd,lpText,lpCation,uType);
/*	
	info.Format(_T("MyMessageBoxW �� HOOK ��"));
	//msgToMain = (TCHAR *)malloc(info.GetLength() * sizeof(TCHAR) + 1);
	memset(msgToMain,0,150);
	msgToMain = info.GetBuffer(info.GetLength());
	
	::SendMessage(g_hWnd,WM_HOOKMSG,(WPARAM)msgToMain,0);*/


	HookOn();

	return nRet;
}