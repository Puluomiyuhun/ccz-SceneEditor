#pragma once
#include <afxstr.h>
#include <string>
#include "CMyComboBox.h"

inline int CString2Int(CString str)
{
	if (str.GetLength() == 0)return 0;
	if (str[0] == 'x'|| str[0] == 'X') {
		str.Delete(0);
		unsigned int sum = 0;
		for (int i = 0; i < str.GetLength(); i++) {
			sum *= 16;
			if (str[i] >= '0' && str[i] <= '9')sum += str[i] - '0';
			else if (str[i] >= 'a' && str[i] <= 'f')sum += str[i] - 'a' + 10;
			else if (str[i] >= 'A' && str[i] <= 'F')sum += str[i] - 'A' + 10;
		}
		return (int)sum;
	}
	else {
		int sum = 0;
		bool sign = false;
		if (str[0] == '-') {
			sign = true;
			str.Delete(0, 1);
		}
		for (int i = 0; i < str.GetLength(); i++)
		{
			sum *= 10;
			sum += str[i] - '0';
		}
		return sign ? -sum : sum;
	}
}

// 整数转换成大/小写形式的16进制，返回值类型为偶数位
inline CString Int2HexStr(int mask) {
	// 固定16进制字符串长度为偶数
	int hexLen = 8;
	//16进制字符数组
	CString hexes[16] = { L"0",L"1",L"2",L"3",L"4",L"5",L"6",L"7",L"8",L"9",L"A",L"B",L"C",L"D",L"E",L"F" };
	CString hexstring = L"";
	for (int i = 0; i < hexLen; i++) {
		int j = hexLen - i - 1;
		// 逆顺序取4bit值
		int number = (mask >> 4 * j) & 0xf;
		hexstring += hexes[number];
	}
	while (hexstring[0] == L'0') {
		hexstring.Delete(0, 1);
	}
	hexstring.Insert(0, L'x');
	return hexstring;
}

inline void ComboAddPer(CMyComboBox &combo)
{
	wchar_t show[10];
	for (int i = 0; i < 1024; i++)
	{
		wcscpy_s(show, L"");
		wcscat_s(show, std::to_wstring(i).c_str());
		wcscat_s(show, L":");
		combo.AddString(show);
	}
	for (int i = 0; i < 4096; i++)
	{
		wcscpy_s(show, L"Var");
		wcscat_s(show, std::to_wstring(i).c_str());
		combo.AddString(show);
	}
	wcscpy_s(show, L"无");
	combo.AddString(show);
}

inline int Per1Code2List(int k)
{
	if (k >= 0)return k;
	else
	{
		if (k == -1)return 5120;
		else return 1022 - k;
	}
}

inline int Per1List2Code(int k)
{
	if (k < 1024)return k;
	else
	{
		if (k == 5120)return -1;
		else return 1022 - k;
	}
}

inline void ComboAddPer2(CMyComboBox& combo)
{
	wchar_t show[10];
	for (int i = 0; i < 1024; i++)
	{
		wcscpy_s(show, L"");
		wcscat_s(show, std::to_wstring(i).c_str());
		wcscat_s(show, L":");
		combo.AddString(show);
	}
	combo.AddString(L"任何部队");
	combo.AddString(L"我军或友军");
	combo.AddString(L"敌军");
	combo.AddString(L"我军当前人物");

	for (int i = 0; i < 4096; i++)
	{
		wcscpy_s(show, L"Var");
		wcscat_s(show, std::to_wstring(i).c_str());
		combo.AddString(show);
	}
	wcscpy_s(show, L"无");
	combo.AddString(show);
}

inline int Per2Code2List(int k)
{
	if (k >= 0)return k;
	else
	{
		if (k == -1)return 5124 + 250;
		else return 1026 - k + 250;
	}
}

inline int Per2List2Code(int k)
{
	if (k < 1028 + 250)return k;
	else
	{
		if (k == 5124 + 250)return -1;
		else return 1026 - k + 250;
	}
}

inline int DebuffCode2List(int k)
{
	if (k < 0)return 30;
	else if (k <= 30) return k / 2 - 1;
	else return (k - 128) / 2 + 14;
}

inline int DebuffList2Code(int k)
{
	if (k >= 30)return -1;
	else if (k < 15) return k * 2 + 2;
	else return (k - 14) * 2 + 128;
}

inline int ch2int(unsigned char* p, int num)
{
	int sum = 0;
	int quan = 1;
	for (int i = 0; i < num; i++)
	{
		sum += int(p[i]) * quan;
		quan *= 256;
	}
	if (sum > 60000 && sum <= 65536) sum = sum - 65536;
	return sum;
}

inline int ch2int(unsigned char* p, int num, bool sign)
{
	int sum = 0;
	int quan = 1;
	for (int i = 0; i < num; i++)
	{
		sum += int(p[i]) * quan;
		quan *= 256;
	}
	if (sum > 60000 && sign)sum = sum - 65536;
	return sum;
}

inline int ch2int(unsigned char* p, int num, int cnt)
{
	int sum = 0;
	int quan = 1;
	for (int i = 0; i < num; i++)
	{
		sum += int(p[i]) * quan;
		quan *= 256;
	}
	if (sum > 60000 && cnt == 2)sum = sum - 65536;
	if (sum > 4200000000 && cnt == 4) sum = sum - 4294967296;
	return sum;
}
