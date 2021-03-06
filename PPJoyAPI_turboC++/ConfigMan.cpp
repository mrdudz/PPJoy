/***************************************************************************
 *   PPJoy Virtual Joystick for Microsoft Windows                          *
 *   Copyright (C) 2011 Deon van der Westhuysen                            *
 *                                                                         *
 *   This program is free software: you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation, either version 3 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>. *
 *                                                                         *
 ***************************************************************************/


#include "stdafx.h"

#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>

#include "ConfigMan.h"
#include "DeviceSetup.h"

#include <stdio.h>

#include "Debug.h"

#define	MAX_DRIVERS	128

int AddDeviceContext (HDEVINFO DeviceInfoList, DEVINST DevInst, PSP_DEVINFO_DATA DriverContext);
int GetDevices (HDEVINFO DeviceInfoList, DEVINST DevInst, PSP_DEVINFO_DATA DriverContexts, int MaxDrivers, int *NumDrivers);

int AddDeviceContext (HDEVINFO DeviceInfoList, DEVINST DevInst, PSP_DEVINFO_DATA DriverContext)
{
 char	Buffer[256];
 int	rc;

 rc= CM_Get_Device_ID(DevInst,Buffer,sizeof(Buffer),0);
 if (rc!=CR_SUCCESS)
 {
  DebugPrintf (("Error %d getting DeviceID for DevInst %X",rc,DevInst))
  return 0;
 }
 
 DebugPrintf (("Found DeviceID %s (%d)\n",Buffer,DevInst))
 DriverContext->cbSize= sizeof(*DriverContext);
  
 DriverContext->cbSize= sizeof(SP_DEVINFO_DATA);

 if (!SetupDiOpenDeviceInfo(DeviceInfoList,Buffer,NULL,0,DriverContext))
 {
  /* Looks like we can get here in success? [rc= 0 then] */
  rc= GetLastError();
  if (rc)
  {
   DebugPrintf (("SetupDiOpenDeviceInfo error %d\n",rc))
   return 0;
  }
 }
 return 1;
}

int GetDevices (HDEVINFO DeviceInfoList, DEVINST DevInst, PSP_DEVINFO_DATA DriverContexts, int MaxDrivers, int *NumDrivers)
{
 int		rc;
 DEVINST	ChildDev;
 int		myrc;

 if ((*NumDrivers)>=MaxDrivers)
  return 0;

 myrc= 1;

 if (AddDeviceContext(DeviceInfoList,DevInst,DriverContexts))
 {
  DriverContexts++;
  (*NumDrivers)++;
  myrc= 0;
 }

 rc= CM_Get_Child(&ChildDev,DevInst,0);
 while (rc==CR_SUCCESS)
 {
  rc= GetDevices (DeviceInfoList,ChildDev,DriverContexts,MaxDrivers,NumDrivers);
  if (!rc)
   myrc= 0;
  rc= CM_Get_Sibling(&ChildDev,ChildDev,0);
 }
 if (rc!=CR_NO_SUCH_DEVNODE)
 {
  DebugPrintf (("CM_Get_Child/CM_Get_Sibling error %d\n",rc))
  myrc= 0;
 }

 return myrc;
}

int DeleteDeviceID (char *DeviceID, int Recursive)
{
 SP_DEVINFO_DATA	DriverContexts[MAX_DRIVERS];
 HDEVINFO			DeviceInfoList;
 DEVINST			RootDev;
 int				rc;
 int				NumDrivers;

 DeviceInfoList= SetupDiCreateDeviceInfoList(NULL,NULL);
 if (DeviceInfoList==INVALID_HANDLE_VALUE)
 {
  DebugPrintf (("SetupDiCreateDeviceInfoList error %d\n",GetLastError()))
  return 0;
 }

 rc= CM_Locate_DevNode(&RootDev,DeviceID,CM_LOCATE_DEVNODE_PHANTOM);
 if (rc!=CR_SUCCESS)
 {
  DebugPrintf (("CM_Locate_DevNode failed, error %d\n",rc))
  return 0;
 }

 if (Recursive)
 {
  NumDrivers= 0;
  rc= GetDevices (DeviceInfoList,RootDev,DriverContexts,MAX_DRIVERS,&NumDrivers);
 }
 else
 {
  NumDrivers= 1;
  rc= AddDeviceContext (DeviceInfoList,RootDev,DriverContexts);
 }

 DebugPrintf (("Found %d drivers\n",NumDrivers))

 while (NumDrivers--)
 {
  DebugPrintf (("Atempting to remove DevInst %d",DriverContexts[NumDrivers].DevInst))
  if (!SetupDiCallClassInstaller(DIF_REMOVE,DeviceInfoList,DriverContexts+NumDrivers))
  {
   DebugPrintf ((" SetupDiCallClassInstaller error %d\n",GetLastError()))
   rc= 0;
  }
  else
   DebugPrintf ((", done\n"))
 }

 SetupDiDestroyDeviceInfoList (DeviceInfoList);
 return rc;
}

int FindDeviceID (char *HardwareID, char *DeviceID, int	DevIDSize)
{
 HDEVINFO			DeviceInfoSet;
 SP_DEVINFO_DATA	DeviceInfoData;

 DWORD				Index;
 char				Buffer[4096];

 int				rc;
 char				*Search;

 *DeviceID= 0;

 /* Are we sure we want to only get present devices here??? */
 /* Currently this is used by the remove device function only... */
 
 /* Create a Device Information Set with all present devices. */
 DeviceInfoSet= SetupDiGetClassDevs(NULL,0,0,DIGCF_ALLCLASSES);
 if (DeviceInfoSet==INVALID_HANDLE_VALUE)
 {
  DebugPrintf (("SetupDiGetClassDevs error %d\n",GetLastError()))
  return 0;
 }
    
 /* Search through all the devices and look for a hardware ID match */
 Index= 0;
 DeviceInfoData.cbSize= sizeof(DeviceInfoData);
 while (SetupDiEnumDeviceInfo(DeviceInfoSet,Index++,&DeviceInfoData))
 {
  if (SetupDiGetDeviceRegistryProperty(DeviceInfoSet,&DeviceInfoData,SPDRP_DEVICEDESC,NULL,(PBYTE)Buffer,sizeof(Buffer),NULL))
  {
   DebugPrintf (("Checking device %s\n",Buffer))
  }

  if (!SetupDiGetDeviceRegistryProperty(DeviceInfoSet,&DeviceInfoData,SPDRP_HARDWAREID,NULL,(PBYTE)Buffer,sizeof(Buffer),NULL))
  {
   rc= GetLastError();
   if (rc==ERROR_INVALID_DATA) 
    continue;

   DebugPrintf (("SetupDiGetDeviceRegistryProperty error %d\n",rc))
   continue;
  }
  
  Buffer[sizeof(Buffer)-1]= 0;
  Buffer[sizeof(Buffer)-2]= 0;

  Search= Buffer;
  while (*Search)
  {
   if (!stricmp(Search,HardwareID))
   {
    /* Found a match for the hardware ID */
	if (!SetupDiGetDeviceInstanceId(DeviceInfoSet,&DeviceInfoData,DeviceID,DevIDSize,NULL))
	{
     DebugPrintf (("SetupDiGetDeviceInstanceId error %d\n",GetLastError()))
	 SetupDiDestroyDeviceInfoList(DeviceInfoSet);
     return 0;
	}

	SetupDiDestroyDeviceInfoList(DeviceInfoSet);
    return 1;
   }
   Search+= strlen(Search)+1;
  }
 }      

 SetupDiDestroyDeviceInfoList(DeviceInfoSet);
 return 0;
}

int SetFriendlyName (char *DeviceID, char *FriendlyName)
{
 SP_DEVINFO_DATA	DriverContext;
 HDEVINFO			DeviceInfoList;
 DEVINST			DevInst;
 int				rc;

 DeviceInfoList= SetupDiCreateDeviceInfoList(NULL,NULL);
 if (DeviceInfoList==INVALID_HANDLE_VALUE)
 {
  DebugPrintf (("SetupDiCreateDeviceInfoList error %d\n",GetLastError()))
  return 0;
 }

 rc= CM_Locate_DevNode(&DevInst,DeviceID,CM_LOCATE_DEVNODE_PHANTOM);
 if (rc!=CR_SUCCESS)
 {
  DebugPrintf (("CM_Locate_DevNode failed, error %d\n",rc))
  return 0;
 }

 if (!AddDeviceContext (DeviceInfoList,DevInst,&DriverContext))
 {
  DebugPrintf (("AddDeviceContext() failed\n"))
  return 0;
 }

 if (!SetupDiSetDeviceRegistryProperty(DeviceInfoList,&DriverContext,SPDRP_FRIENDLYNAME,(UCHAR*) FriendlyName,strlen(FriendlyName)+1))
 {
  DebugPrintf (("SetupDiSetDeviceRegistryProperty() error %d\n",GetLastError()))
  return 0;
 }

 SetupDiDestroyDeviceInfoList (DeviceInfoList);
 return 1;
}

void MakeDeviceID (char *DeviceID, USHORT VendorID, USHORT ProductID)
{
 sprintf (DeviceID,"PPJOYBUS\\VID_%04X&PID_%04X\\PPJoy",VendorID,ProductID);
}

int RestartDevice (char *DeviceID)
{
 SP_PROPCHANGE_PARAMS	PropChangeParams;
 SP_DEVINFO_DATA		DriverContext;
 HDEVINFO				DeviceInfoList;
 int					rc;
 DEVINST				DeviceNode;
 char					DeviceInstanceID[128];

 DeviceInfoList= SetupDiCreateDeviceInfoList(NULL,NULL);
 if (DeviceInfoList==INVALID_HANDLE_VALUE)
 {
  DebugPrintf (("SetupDiCreateDeviceInfoList error %d\n",GetLastError()))
  return 0;
 }

 rc= CM_Locate_DevNode(&DeviceNode,DeviceID,CM_LOCATE_DEVNODE_PHANTOM);
 if (rc!=CR_SUCCESS)
 {
  DebugPrintf (("CM_Locate_DevNode failed, error %d\n",rc))
  goto Exit;
 }

 rc= CM_Get_Device_ID(DeviceNode,DeviceInstanceID,sizeof(DeviceInstanceID),0);
 if (rc!=CR_SUCCESS)
 {
  DebugPrintf (("Error %d getting DeviceInstanceID for DeviceNode %X",rc,DeviceNode))
  goto Exit;
 }
 
 DebugPrintf (("Found DeviceID %s (%d)\n",DeviceID,DeviceInstanceID))

 DriverContext.cbSize= sizeof(SP_DEVINFO_DATA);
 if (!SetupDiOpenDeviceInfo(DeviceInfoList,DeviceInstanceID,NULL,0,&DriverContext))
 {
  rc= GetLastError();
  DebugPrintf (("SetupDiOpenDeviceInfo error %d\n",rc))
  goto Exit;
 }

 PropChangeParams.ClassInstallHeader.cbSize= sizeof(SP_CLASSINSTALL_HEADER);
 PropChangeParams.ClassInstallHeader.InstallFunction= DIF_PROPERTYCHANGE;
 PropChangeParams.StateChange= DICS_PROPCHANGE;
 PropChangeParams.Scope= DICS_FLAG_GLOBAL;
 PropChangeParams.HwProfile= 0;

 if (!SetupDiSetClassInstallParams(DeviceInfoList,&DriverContext,&PropChangeParams.ClassInstallHeader,sizeof(PropChangeParams)))
 {
  rc= GetLastError();
  DebugPrintf ((" SetupDiSetClassInstallParams error %d\n",rc))
  goto Exit;
 }

 if (!SetupDiCallClassInstaller(DIF_PROPERTYCHANGE,DeviceInfoList,&DriverContext))
 {
  rc= GetLastError();
  DebugPrintf ((" SetupDiCallClassInstaller - DIF_PROPERTYCHANGE error %d\n",rc))
  goto Exit;
 }

 rc= 0;

Exit:
 SetupDiDestroyDeviceInfoList (DeviceInfoList);
 return rc;
}

int GetDeviceIRQ (char *DeviceID)
{
 int					rc;
 DEVINST				DeviceNode;
 LOG_CONF		LogConf;
 RES_DES		ResDesc;
 int			IRQ;
 IRQ_RESOURCE	ResBuffer;

 rc= CM_Locate_DevNode(&DeviceNode,DeviceID,CM_LOCATE_DEVNODE_PHANTOM);
 if (rc!=CR_SUCCESS)
 {
  DebugPrintf (("CM_Locate_DevNode failed, error %d\n",rc))
  return -3;
 }
 
 rc= CM_Get_First_Log_Conf(&LogConf,DeviceNode,ALLOC_LOG_CONF);
 if (rc!=CR_SUCCESS)
 {
  DebugPrintf (("CM_Get_First_Log_Conf failed, error %d\n",rc))
  return -4;
 }

 rc= CM_Get_Next_Res_Des(&ResDesc,LogConf,ResType_IRQ,NULL,0);
 if (rc!=CR_SUCCESS)
 {
  if (rc==CR_NO_MORE_RES_DES)
  {
   CM_Free_Log_Conf_Handle (LogConf);
   return -5;
  }
  DebugPrintf (("CM_Get_Next_Res_Des failed, error %d\n",rc))
  CM_Free_Log_Conf_Handle (LogConf);
  return -6;
 }
 
 rc= CM_Get_Res_Des_Data(ResDesc,&ResBuffer,sizeof(ResBuffer),0);
 if (rc!=CR_SUCCESS)
 {
  DebugPrintf (("CM_Get_Res_Des_Data failed, error %d\n",rc))
  CM_Free_Res_Des_Handle (ResDesc);
  CM_Free_Log_Conf_Handle (LogConf);
  return -7;
 }

 IRQ= ResBuffer.IRQ_Header.IRQD_Alloc_Num;

 CM_Free_Res_Des_Handle (ResDesc);
 CM_Free_Log_Conf_Handle (LogConf);
 return IRQ;
}

int GetInterruptAssignment(char *DeviceName)
{
 char	DevInstID[128];

 if (!GetDeviceInstanceID (DeviceName,DevInstID,sizeof(DevInstID)))
  return -2;

 return GetDeviceIRQ (DevInstID);
}

int GetConnectInterruptSetting(char *DeviceName)
{
 DWORD	RegValue;
 DWORD	ValueType;
 DWORD	BufSize;
 HKEY	PortDeviceRegKey;
 int	rc;

 PortDeviceRegKey= OpenDeviceRegKey(DeviceName);

 if (PortDeviceRegKey==INVALID_HANDLE_VALUE)
 {
  DebugPrintf(("Cannot open parallel port device registry"))
  return -1;
 }

 BufSize= sizeof(RegValue);
 rc= RegQueryValueEx (PortDeviceRegKey,"EnableConnectInterruptIoctl",NULL,&ValueType,(LPBYTE)&RegValue,&BufSize);
 CloseHandle (PortDeviceRegKey);

 if (rc==2)		// Reg entry not found. Same effect as value being 0
  return 0;

 if (rc!=ERROR_SUCCESS)
 {
  DebugPrintf(("Error %d reading EnableConnectInterruptIoctl value",rc))
  return -1;
 }

 if (ValueType!=REG_DWORD)
 {
  DebugPrintf (("Datatype for EnableConnectInterruptIoctl is not DWORD!!!",ValueType))
  return -1;
 }

 return RegValue;
}

int SetConnectInterruptSetting(char *DeviceName, DWORD Connect)
{
 HKEY	PortDeviceRegKey;
 int	rc;

 PortDeviceRegKey= OpenDeviceRegKey(DeviceName);

 if (PortDeviceRegKey==INVALID_HANDLE_VALUE)
 {
  DebugPrintf(("Cannot open parallel port device registry"))
  return -1;
 }

 rc= RegSetValueEx (PortDeviceRegKey,"EnableConnectInterruptIoctl",NULL,REG_DWORD,(LPBYTE)&Connect,sizeof(Connect));
 CloseHandle (PortDeviceRegKey);

 return rc==ERROR_SUCCESS;
}
