#include "pch.h"
#include "FixFilePerms.h"

//	WARNING: This code is shamelessly stolen from Wunkolo's UWPDdumper project :|
//	https://github.com/Wunkolo/UWPDumper
//	go to code :P 
//	https://github.com/Wunkolo/UWPDumper/blob/9fb0a040e674521c1413276bcea6e4e708f34d19/UWPInjector/source/main.cpp#L226 17.10.2020


// Setting DLL access controls
#include <stdio.h>
#include <string>
#include <Aclapi.h>
#include <Sddl.h>

bool SetAccessControl(const std::wstring& ExecutableName, const wchar_t* AccessString, DWORD* win32Error)
{
	if (win32Error != nullptr)
	{
		*win32Error = 0;
	}

	PSECURITY_DESCRIPTOR SecurityDescriptor = nullptr;
	EXPLICIT_ACCESSW ExplicitAccess = { 0 };

	ACL* AccessControlCurrent = nullptr;
	ACL* AccessControlNew = nullptr;

	SECURITY_INFORMATION SecurityInfo = DACL_SECURITY_INFORMATION;
	PSID SecurityIdentifier = nullptr;
	bool success = false;

	DWORD getInfoResult = GetNamedSecurityInfoW(
			ExecutableName.c_str(),
			SE_FILE_OBJECT,
			DACL_SECURITY_INFORMATION,
			nullptr,
			nullptr,
			&AccessControlCurrent,
			nullptr,
			&SecurityDescriptor
		);
	if (getInfoResult == ERROR_SUCCESS)
	{
		if (!ConvertStringSidToSidW(AccessString, &SecurityIdentifier))
		{
			if (win32Error != nullptr)
			{
				*win32Error = GetLastError();
			}
		}
		if (SecurityIdentifier != nullptr)
		{
			ExplicitAccess.grfAccessPermissions = GENERIC_READ | GENERIC_EXECUTE;
			ExplicitAccess.grfAccessMode = SET_ACCESS;
			ExplicitAccess.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
			ExplicitAccess.Trustee.TrusteeForm = TRUSTEE_IS_SID;
			ExplicitAccess.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
			ExplicitAccess.Trustee.ptstrName = reinterpret_cast<wchar_t*>(SecurityIdentifier);

			DWORD aclResult = SetEntriesInAclW(
					1,
					&ExplicitAccess,
					AccessControlCurrent,
					&AccessControlNew
				);
			if (aclResult == ERROR_SUCCESS)
			{
				DWORD setInfoResult = SetNamedSecurityInfoW(
					const_cast<wchar_t*>(ExecutableName.c_str()),
					SE_FILE_OBJECT,
					SecurityInfo,
					nullptr,
					nullptr,
					AccessControlNew,
					nullptr
				);
				success = (setInfoResult == ERROR_SUCCESS);
				if (!success && win32Error != nullptr)
				{
					*win32Error = setInfoResult;
				}
			}
			else if (win32Error != nullptr)
			{
				*win32Error = aclResult;
			}
		}
	}
	else if (win32Error != nullptr)
	{
		*win32Error = getInfoResult;
	}

	if (SecurityDescriptor)
	{
		LocalFree(reinterpret_cast<HLOCAL>(SecurityDescriptor));
	}
	if (AccessControlNew)
	{
		LocalFree(reinterpret_cast<HLOCAL>(AccessControlNew));
	}
	if (SecurityIdentifier)
	{
		LocalFree(reinterpret_cast<HLOCAL>(SecurityIdentifier));
	}

	return success;
}
