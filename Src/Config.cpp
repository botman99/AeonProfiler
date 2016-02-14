
#include <Windows.h>
#include <WinBase.h>
#include <tchar.h>
#include <ShlObj.h>
#include <KnownFolders.h>
#include <iostream>
#include <fstream>
#include <string>

#include "DebugLog.h"

#include "Config.h"

#define AeonSection "[AeonProfiler]"

ConfigValueStruct ConfigValues[] =
{
	ConfigValueStruct(CONFIG_WINDOW_POS_X, CONFIG_INT, 40, "window_pos_x"),
	ConfigValueStruct(CONFIG_WINDOW_POS_Y, CONFIG_INT, 10, "window_pos_y"),
	ConfigValueStruct(CONFIG_WINDOW_SIZE_WIDTH, CONFIG_INT, 1024, "window_width"),
	ConfigValueStruct(CONFIG_WINDOW_SIZE_HEIGHT, CONFIG_INT, 768, "window_height"),
	ConfigValueStruct(CONFIG_MIDDLE_SPLITTER_PERCENT, CONFIG_FLOAT, 0.60f, "middle_splitter_percent"),
	ConfigValueStruct(CONFIG_LEFT_SPLITTER_PERCENT, CONFIG_FLOAT, 0.60f, "left_splitter_percent"),
	ConfigValueStruct(CONFIG_RIGHT_SPLITTER_PERCENT, CONFIG_FLOAT, 0.50f, "right_splitter_percent"),
	ConfigValueStruct(CONFIG_FIND_FUNCTION_CASE_SENSITIVE, CONFIG_INT, 0, "find_function_case_sensitive"),
};


CConfig::CConfig()
	: PreviousTickCount(0)
	, DirtyTimerCount(0)
{
	bIsInitializing = true;

	m_filename[0] = 0;

	TCHAR AppDataFolder[MAX_PATH];
	TCHAR Buffer[MAX_PATH];

	HRESULT result = SHGetFolderPath(NULL, CSIDL_APPDATA, NULL, 0, AppDataFolder);  // get the current user's AppData folder

	if( result == S_OK )
	{
		swprintf(Buffer, MAX_PATH, TEXT("%s\\AeonProfiler"), AppDataFolder);

		CreateDirectory(Buffer, NULL);  // create the folder for the .ini file

		swprintf(Buffer, MAX_PATH, TEXT("%s\\AeonProfiler\\AeonProfiler.ini"), AppDataFolder);

		_tcscpy_s(m_filename, Buffer);
	}
	else
	{
		DebugLog("SHGetFolderPath() failed, result = %d", result);
	}

	// https://blogs.msdn.microsoft.com/oldnewthing/20071023-00/?p=24713/
	DWORD file_attributes = GetFileAttributes(m_filename);

	if( file_attributes != 0xffffffff )  // does the config .ini file exist?
	{
		// populate the config values (this will overwrite any default values with config file settings)
		ReadConfigFile();
	}
	else
	{
		WriteConfigFile();
	}

	bIsInitializing = false;
}

CConfig::~CConfig()
{
	if( DirtyTimerCount > 0 )  // if we are currently dirty, save the config file
	{
		WriteConfigFile();
	}
}

void CConfig::SetDirty()
{
	if( !bIsInitializing )  // we don't set dirty while initializing
	{
		DirtyTimerCount = 1000;  // every time we dirty the config, we delay for 1000ms before writing the config file
	}
}

void CConfig::Timer()
{
	ULONGLONG CurrentTickCount = GetTickCount64();

	if( PreviousTickCount == 0 )
	{
		PreviousTickCount = CurrentTickCount;
		return;
	}

	ULONGLONG DeltaTickCount = CurrentTickCount - PreviousTickCount;
	PreviousTickCount = CurrentTickCount;

	if( DirtyTimerCount > 0 )
	{
		if( DirtyTimerCount <= DeltaTickCount )
		{
			WriteConfigFile();
		}
		else
		{
			DirtyTimerCount -= DeltaTickCount;
		}
	}
}

void CConfig::ReadConfigFile()
{
	string buffer;
	char scan_format[64];  // config .ini file lines can be no more than 63 characters in length

	ifstream file_stream(m_filename);

	if( file_stream.is_open() )
	{
		while( std::getline(file_stream, buffer) )
		{
			for( int index = 0; index < _countof(ConfigValues); ++index )
			{
				if( ConfigValues[index].Type == CONFIG_INT )
				{
					sprintf_s(scan_format, sizeof(scan_format), "%s=%s", ConfigValues[index].Key, "%d");

					int value;
					if( sscanf_s(buffer.c_str(), scan_format, &value) == 1 )
					{
						ConfigValues[index].Value.int_val = value;
						break;
					}
				}
				else if( ConfigValues[index].Type == CONFIG_FLOAT )
				{
					sprintf_s(scan_format, sizeof(scan_format), "%s=%s", ConfigValues[index].Key, "%f");

					float value;
					if( sscanf_s(buffer.c_str(), scan_format, &value) == 1 )
					{
						ConfigValues[index].Value.float_val = value;
						break;
					}
				}
			}
		}
	}
}

void CConfig::WriteConfigFile()
{
	ofstream file_stream;

	file_stream.open(m_filename);

	if( file_stream.is_open() )
	{
		file_stream << AeonSection << endl;

		for( int index = 0; index < _countof(ConfigValues); ++index )
		{
			if( ConfigValues[index].Type == CONFIG_INT )
			{
				file_stream << ConfigValues[index].Key << "=" << ConfigValues[index].Value.int_val << endl;
			}
			else if( ConfigValues[index].Type == CONFIG_FLOAT )
			{
				file_stream << ConfigValues[index].Key << "=" << ConfigValues[index].Value.float_val << endl;
			}
		}
	}

	file_stream.close();

	DirtyTimerCount = 0;
}

int CConfig::GetInt(ConfigValueId Id)
{
	for( int index = 0; index < _countof(ConfigValues); ++index )
	{
		if( (Id == ConfigValues[index].Id) && (ConfigValues[index].Type == CONFIG_INT) )
		{
			return ConfigValues[index].Value.int_val;
		}
	}

	return -1;
}

void CConfig::SetInt(ConfigValueId Id, int value)
{
	for( int index = 0; index < _countof(ConfigValues); ++index )
	{
		if( (Id == ConfigValues[index].Id) && (ConfigValues[index].Type == CONFIG_INT) )
		{
			if( value != ConfigValues[index].Value.int_val )  // don't set and make dirty if value didn't change
			{
				ConfigValues[index].Value.int_val = value;
				SetDirty();
			}
			break;
		}
	}
}

float CConfig::GetFloat(ConfigValueId Id)
{
	for( int index = 0; index < _countof(ConfigValues); ++index )
	{
		if( (Id == ConfigValues[index].Id) && (ConfigValues[index].Type == CONFIG_FLOAT) )
		{
			return ConfigValues[index].Value.float_val;
		}
	}

	return -1.0f;
}

void CConfig::SetFloat(ConfigValueId Id, float value)
{
	for( int index = 0; index < _countof(ConfigValues); ++index )
	{
		if( (Id == ConfigValues[index].Id) && (ConfigValues[index].Type == CONFIG_FLOAT) )
		{
			ConfigValues[index].Value.float_val = value;
			SetDirty();
			break;
		}
	}
}
