
#pragma once

#include <Windows.h>

enum ConfigValueType
{
	CONFIG_INT,
	CONFIG_FLOAT,
};

union ConfigValueUnion
{
	int int_val;
	float float_val;
};

enum ConfigValueId  // this is the list of the known configuration settings
{
	CONFIG_WINDOW_POS_X,
	CONFIG_WINDOW_POS_Y,
	CONFIG_WINDOW_SIZE_WIDTH,
	CONFIG_WINDOW_SIZE_HEIGHT,
	CONFIG_MIDDLE_SPLITTER_PERCENT,
	CONFIG_LEFT_SPLITTER_PERCENT,
	CONFIG_RIGHT_SPLITTER_PERCENT,
	CONFIG_FIND_FUNCTION_CASE_SENSITIVE,
};

struct ConfigValueStruct
{
	ConfigValueId Id;
	ConfigValueType Type;
	ConfigValueUnion Value;
	char* Key;  // name of the key in the config .ini file

	explicit ConfigValueStruct(ConfigValueId InId, ConfigValueType InType, int InValue, char* InKey)
		: Id(InId)
		, Type(InType)
		, Key(InKey)
	{
		Value.int_val = InValue;
	}

	explicit ConfigValueStruct(ConfigValueId InId, ConfigValueType InType, float InValue, char* InKey)
		: Id(InId)
		, Type(InType)
		, Key(InKey)
	{
		Value.float_val = InValue;
	}
};

extern ConfigValueStruct ConfigValues[];

class CConfig
{
private:
	bool bIsInitializing;

	ULONGLONG PreviousTickCount;
	ULONGLONG DirtyTimerCount;  // amount of time remaining after becoming dirty before the config file will be saved

	TCHAR m_filename[MAX_PATH];

	void SetDirty();

	void ReadConfigFile();
	void WriteConfigFile();

public:
	CConfig();
	~CConfig();

	void Timer();  // timer function to check if config is dirty and needs to be saved

	int GetInt(ConfigValueId Id);  // return value of -1 indicates Id was not found or type Type is not CONFIG_INT
	void SetInt(ConfigValueId Id, int value);

	float GetFloat(ConfigValueId Id);  // return value of -1.0f indicates Id was not found or type Type is not CONFIG_FLOAT
	void SetFloat(ConfigValueId Id, float value);
};
