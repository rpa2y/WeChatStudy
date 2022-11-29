#include "InlineHook.h"
#include "..\BeaEngine\beaengine.h"
#include <stdio.h>

#define BEA_ENGINE_STATIC
#define BEA_USE_STDCALL

InlineHook::InlineHook()
{
	memset(m_OldCode, 0x90, sizeof(m_OldCode));
}

InlineHook::~InlineHook()
{

}

bool InlineHook::AddHook(LPVOID pTarget, InlineFunc userFunction)
{
	unsigned char ShellCode[0x40] = { 
		0x60,	//pushad
		0x9C,	//pushfd
		0x54,	//push esp
		0xB8, 0x90, 0x90, 0x90, 0x90,  //mov eax,hookFunc
		0xFF, 0xD0,	 //call eax
		0x9D, //popfd
		0x61, //popad
	};

	memcpy(&ShellCode[0x4], &userFunction, 4);
	//����ԭʼ������
	DWORD dwOldProtect = 0;
	BOOL bRet = VirtualProtect(pTarget, 0x20, PAGE_EXECUTE_READWRITE, &dwOldProtect);
	if (!bRet)
	{
		return false;
	}

	memcpy(m_OldCode, pTarget, 5);//����hook��ԭ
	m_dwVirtualAddr = (DWORD)VirtualAlloc(NULL, 0x100, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
	if (m_dwVirtualAddr == 0)
	{
		return false;
	}

	//��ʼ����ԭʼָ��,ͨ����������棬ʶ��ҪHook�ĵ�ַ����ָ��Լ��ƻ���ָ���
	DWORD trampoline = m_dwVirtualAddr + 0xC;
	unsigned int offset;
	unsigned char op;

	DISASM MyDisasm;
	memset(&MyDisasm, 0, sizeof(DISASM));
	MyDisasm.EIP = (UIntPtr)pTarget;

	unsigned char* cmdbuf = (unsigned char*)pTarget;			//ԭʼ�����ַ
	int sum = 0;		//ԭʼָ���
	int CmdLen = 0;		//���ָ���

	unsigned char NewCmd[32] = { 0 };							//���������

	//����õ��ķ��ص�ַ
	DWORD dwRetAddr = 0x90909090;
	while (1)
	{
		int oplen = Disasm(&MyDisasm);
		if (oplen < 1)
		{
			return false;
		}

		if (MyDisasm.Instruction.BranchType != 0)			//�����ת
		{
			//call eax
			if (MyDisasm.Instruction.Opcode == 0xFF) {
				memcpy(NewCmd + CmdLen, cmdbuf + sum, oplen);
				CmdLen += oplen;
			}
			else if (MyDisasm.Instruction.BranchType == RetType)
			{
				memcpy(NewCmd + CmdLen, cmdbuf + sum, oplen);
				CmdLen += oplen;
			}
			else if (oplen == 2)			//��ת��
			{
				op = cmdbuf[sum];
				offset = cmdbuf[sum + 1];
				if (offset>127)		// offset 8λ��չ��32λ
				{	
					offset = 0xFFFFFF00 + cmdbuf[sum + 1];
				}

				if (MyDisasm.Instruction.Opcode == 235)	// EB short jmp
				{
					offset = MyDisasm.Instruction.AddrValue - (trampoline + CmdLen) - 5;
					BYTE JMPCMD[5] = { 233, 0x90, 0x90, 0x90, 0x90 };
					*(DWORD*)(JMPCMD + 1) = offset;
					memcpy(NewCmd + CmdLen, JMPCMD, sizeof(JMPCMD));
					CmdLen += sizeof(JMPCMD);
				}
				else if (MyDisasm.Instruction.Opcode >= 0x70 && MyDisasm.Instruction.Opcode <= 0x7F)	// 70 - 7F short jcc'
				{
					offset = MyDisasm.Instruction.AddrValue - (trampoline + CmdLen) - 6;
					BYTE JccCMD[6] = { 0x0F, op + 16, 0x90, 0x90, 0x90, 0x90 };
					*(DWORD*)(JccCMD + 2) = offset;
					memcpy(NewCmd + CmdLen, JccCMD, sizeof(JccCMD));
					CmdLen += sizeof(JccCMD);
				}
				else
				{
					sum = 0;
					break;
				}
			}
			else if (oplen == 5)	//Զת��,	Jmp����CALL
			{
				offset = MyDisasm.Instruction.AddrValue - (trampoline + CmdLen) - 5;
				BYTE LongCmd[5] = { MyDisasm.Instruction.Opcode, 0x90, 0x90, 0x90, 0x90 };
				*(DWORD*)(LongCmd + 1) = offset;
				memcpy(NewCmd + CmdLen, LongCmd, sizeof(LongCmd));
				CmdLen += sizeof(LongCmd);
			}
		}
		else
		{
			memcpy(NewCmd + CmdLen, cmdbuf + sum, oplen);
			CmdLen += oplen;
		}
	
		
		sum += oplen;
		MyDisasm.EIP = MyDisasm.EIP + oplen;
		if (sum >= 5)
		{
			dwRetAddr = MyDisasm.EIP;
			m_dwHookAddr = pTarget;
			break;
		}
	}

	memcpy(&ShellCode[0xC], NewCmd, CmdLen);
	BYTE PushRet[6] = { 0x68, 0x90, 0x90, 0x90, 0x90, 0xC3 };
	*(DWORD*)(PushRet + 1) = dwRetAddr;

	memcpy(&ShellCode[0xC + CmdLen], PushRet, sizeof(PushRet));

	//ShellCode������ϣ�д��ShellCode
	memcpy((LPVOID)m_dwVirtualAddr, ShellCode, sizeof(ShellCode));

	//��дJmp������ת���հ��ڴ�
	BYTE jmpcode[5] = { 0xE9, 0xFF, 0xFF, 0xFF, 0xFF };
	*(DWORD*)(jmpcode + 1) = m_dwVirtualAddr - (DWORD)pTarget - 5;
	memcpy(pTarget, jmpcode, sizeof(jmpcode) / sizeof(jmpcode[0]));

	//��ԭ�ڴ�����
	VirtualProtect(pTarget, 5, dwOldProtect, &dwOldProtect);
	return true;
}


bool InlineHook::RemoveHook(void)
{
	if (m_dwHookAddr == 0)
	{
		return false;
	}

	DWORD dwOldProtect = 0;
	VirtualProtect((LPVOID)m_dwHookAddr, 0x20, PAGE_EXECUTE_READWRITE, &dwOldProtect);
	memcpy((LPVOID)m_dwHookAddr, m_OldCode, 5);
	VirtualProtect((LPVOID)m_dwHookAddr, 5, dwOldProtect, &dwOldProtect);
	return true;
}